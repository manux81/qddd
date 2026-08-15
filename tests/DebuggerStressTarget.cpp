#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <latch>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <semaphore>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace stress {

template <typename T>
class Generator
{
public:
	struct promise_type
	{
		T current{};

		Generator get_return_object()
		{
			return Generator{
				std::coroutine_handle<promise_type>::from_promise(*this)};
		}
		std::suspend_always initial_suspend() const noexcept { return {}; }
		std::suspend_always final_suspend() const noexcept { return {}; }
		std::suspend_always yield_value(T value) noexcept
		{
			current = std::move(value);
			return {};
		}
		void return_void() const noexcept {}
		[[noreturn]] void unhandled_exception() const { std::terminate(); }
	};

	class Iterator
	{
	public:
		explicit Iterator(std::coroutine_handle<promise_type> handle = {})
			: m_handle(handle)
		{
		}

		Iterator& operator++()
		{
			m_handle.resume();
			return *this;
		}
		const T& operator*() const { return m_handle.promise().current; }
		bool operator==(std::default_sentinel_t) const
		{
			return !m_handle || m_handle.done();
		}

	private:
		std::coroutine_handle<promise_type> m_handle;
	};

	explicit Generator(std::coroutine_handle<promise_type> handle)
		: m_handle(handle)
	{
	}
	Generator(const Generator&) = delete;
	Generator& operator=(const Generator&) = delete;
	Generator(Generator&& other) noexcept
		: m_handle(std::exchange(other.m_handle, {}))
	{
	}
	~Generator()
	{
		if (m_handle)
			m_handle.destroy();
	}

	Iterator begin()
	{
		if (m_handle)
			m_handle.resume();
		return Iterator{m_handle};
	}
	std::default_sentinel_t end() const noexcept { return {}; }

private:
	std::coroutine_handle<promise_type> m_handle;
};

Generator<std::uint64_t> fibonacci(std::size_t count)
{
	std::uint64_t current = 0;
	std::uint64_t next = 1;
	for (std::size_t i = 0; i < count; ++i) {
		co_yield current;
		const auto following = current + next;
		current = next;
		next = following;
	}
}

enum class SensorState : std::uint8_t
{
	Idle,
	Sampling,
	Fault
};

struct StatusBits
{
	std::uint8_t enabled : 1 = 1;
	std::uint8_t alarm : 1 = 0;
	std::uint8_t channel : 3 = 0;
	std::uint8_t reserved : 3 = 0;
};

using SensorReading = std::variant<std::int64_t, double, std::string>;

struct Sensor
{
	std::uint32_t id = 0;
	std::string name;
	SensorState state = SensorState::Idle;
	StatusBits status;
	std::optional<double> calibration;
	SensorReading reading;
	std::array<std::uint16_t, 4> rawSamples{};
};

struct GraphNode
{
	std::string label;
	int value = 0;
	GraphNode* next = nullptr;
	GraphNode* alias = nullptr;
	std::weak_ptr<GraphNode> previous;
};

struct Shape
{
	virtual ~Shape() = default;
	virtual double area() const = 0;
	std::string label;
};

struct Circle final : Shape
{
	explicit Circle(double value) : radius(value) { label = "circle"; }
	double area() const override { return 3.141592653589793 * radius * radius; }
	double radius = 0.0;
};

struct Rectangle final : Shape
{
	Rectangle(double widthValue, double heightValue)
		: width(widthValue), height(heightValue)
	{
		label = "rectangle";
	}
	double area() const override { return width * height; }
	double width = 0.0;
	double height = 0.0;
};

template <typename T>
concept Arithmetic = std::integral<T> || std::floating_point<T>;

template <Arithmetic T>
T sum(std::span<const T> values)
{
	return std::accumulate(values.begin(), values.end(), T{});
}

struct DebuggerSnapshot
{
	std::vector<Sensor> sensors;
	std::array<std::shared_ptr<GraphNode>, 3> graphOwners;
	GraphNode* graphEntry = nullptr;
	std::vector<std::unique_ptr<Shape>> shapes;
	std::map<std::string, std::vector<int>> indexedValues;
	std::unordered_map<std::uint32_t, Sensor*> sensorsById;
	std::variant<std::monostate, int, std::string> activeCommand;
	std::optional<std::string> lastError;
	std::any dynamicallyTypedValue;
	std::tuple<int, std::string, double> mixedTuple;
	std::vector<int> oddSquares;
	std::vector<std::uint64_t> fibonacciValues;
	std::array<int, 8> workerResults{};
	std::bitset<16> featureFlags{0b1010'0101'1100'0011};
	std::unique_ptr<std::atomic<int>> completedWorkers =
		std::make_unique<std::atomic<int>>(0);
	int mutableCounter = 42;
	const char* nullablePointer = nullptr;
	std::source_location creationSite = std::source_location::current();
};

DebuggerSnapshot makeSnapshot()
{
	DebuggerSnapshot snapshot;
	snapshot.sensors = {
		{0x10, "temperature", SensorState::Sampling, {1, 0, 1, 0},
		 0.125, 23.75, {0x0010, 0x00ff, 0x0abc, 0xffff}},
		{0x20, "pressure", SensorState::Idle, {1, 0, 2, 0},
		 std::nullopt, std::int64_t{101325}, {101, 325, 0, 1}},
		{0x30, "diagnostic", SensorState::Fault, {1, 1, 3, 0},
		 -1.5, std::string{"CRC mismatch"}, {0xdead, 0xbeef, 0, 0}}
	};

	for (std::size_t i = 0; i < snapshot.graphOwners.size(); ++i) {
		auto node = std::make_shared<GraphNode>();
		node->label = std::string{"node-"} + static_cast<char>('A' + i);
		node->value = static_cast<int>((i + 1) * 10);
		snapshot.graphOwners[i] = std::move(node);
	}
	for (std::size_t i = 0; i < snapshot.graphOwners.size(); ++i) {
		auto& node = snapshot.graphOwners[i];
		auto& next = snapshot.graphOwners[(i + 1) % snapshot.graphOwners.size()];
		node->next = next.get();
		node->alias = snapshot.graphOwners[0].get();
		next->previous = node;
	}
	snapshot.graphEntry = snapshot.graphOwners[0].get();

	snapshot.shapes.push_back(std::make_unique<Circle>(2.5));
	snapshot.shapes.push_back(std::make_unique<Rectangle>(3.0, 4.0));
	snapshot.indexedValues = {
		{"prime", {2, 3, 5, 7, 11}},
		{"signed", {-8, -1, 0, 1, 8}}
	};
	for (Sensor& sensor : snapshot.sensors)
		snapshot.sensorsById.emplace(sensor.id, &sensor);
	snapshot.activeCommand = std::string{"continue"};
	snapshot.lastError = "recoverable timeout";
	snapshot.dynamicallyTypedValue = std::vector<std::string>{"alpha", "beta", "gamma"};
	snapshot.mixedTuple = {7, "seven", 7.5};

	auto squares = std::views::iota(1, 16)
		| std::views::filter([](int value) { return value % 2 != 0; })
		| std::views::transform([](int value) { return value * value; });
	for (int value : squares)
		snapshot.oddSquares.push_back(value);
	for (std::uint64_t value : fibonacci(10))
		snapshot.fibonacciValues.push_back(value);
	return snapshot;
}

#if defined(_MSC_VER)
#define QDDD_NOINLINE __declspec(noinline)
#else
#define QDDD_NOINLINE __attribute__((noinline))
#endif

// Put a breakpoint here. PHASE identifies which fully-constructed state is
// available, and SNAPSHOT keeps every interesting object reachable.
QDDD_NOINLINE void debuggerCheckpoint(
	std::string_view phase, DebuggerSnapshot& snapshot)
{
	volatile std::size_t observable = phase.size()
		+ snapshot.sensors.size()
		+ static_cast<std::size_t>(snapshot.mutableCounter);
	if (observable == 0)
		std::abort();
}

bool verifySnapshot(const DebuggerSnapshot& snapshot)
{
	const std::array<int, 8> expectedSquares{0, 1, 4, 9, 16, 25, 36, 49};
	const bool graphIsCyclic = snapshot.graphEntry
		&& snapshot.graphEntry->next
		&& snapshot.graphEntry->next->next
		&& snapshot.graphEntry->next->next->next == snapshot.graphEntry;
	const bool workerValuesCorrect = std::equal(
		expectedSquares.begin(), expectedSquares.end(),
		snapshot.workerResults.begin());
	return snapshot.sensors.size() == 3
		&& snapshot.fibonacciValues ==
			std::vector<std::uint64_t>({0, 1, 1, 2, 3, 5, 8, 13, 21, 34})
		&& snapshot.oddSquares == std::vector<int>({1, 9, 25, 49, 81, 121, 169, 225})
		&& graphIsCyclic
		&& workerValuesCorrect
		&& snapshot.completedWorkers->load() == 2;
}

} // namespace stress

int main()
{
	using namespace stress;
	DebuggerSnapshot snapshot = makeSnapshot();
	debuggerCheckpoint("objects-ready", snapshot);

	std::latch workersReady{2};
	std::counting_semaphore<2> releaseWorkers{0};
	std::array<std::jthread, 2> workers;
	for (int worker = 0; worker < 2; ++worker) {
		workers[worker] = std::jthread([&, worker] {
			for (int index = worker; index < 8; index += 2)
				snapshot.workerResults[index] = index * index;
			snapshot.completedWorkers->fetch_add(1);
			workersReady.count_down();
			releaseWorkers.acquire();
		});
	}
	workersReady.wait();
	debuggerCheckpoint("threads-waiting", snapshot);
	releaseWorkers.release(2);
	for (std::jthread& worker : workers)
		worker.join();

	std::span<const int> firstHalf{snapshot.workerResults.data(), 4};
	const int firstHalfChecksum = sum(firstHalf);
	snapshot.mutableCounter += firstHalfChecksum;
	debuggerCheckpoint("final-state", snapshot);

	if (!verifySnapshot(snapshot)) {
		std::cerr << "debugger stress target verification failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "debugger stress target ok; checksum="
	          << snapshot.mutableCounter << '\n';
	return EXIT_SUCCESS;
}
