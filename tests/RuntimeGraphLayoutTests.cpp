#include "RuntimeGraphLayout.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRectF>

#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n"; return EXIT_FAILURE; } } while (false)

namespace {

RuntimeLayoutNode node(const char* id, qreal width = 120.0, qreal height = 70.0)
{
	return {QString::fromLatin1(id), QSizeF(width, height), {}, false, false};
}

RuntimeLayoutEdge edge(const char* id, const char* source, const char* destination)
{
	return {QString::fromLatin1(id), QString::fromLatin1(source),
	        QString::fromLatin1(destination)};
}

bool overlaps(const RuntimeLayoutResult& result,
	          const QHash<QString, QSizeF>& sizes,
	          const QString& a,
	          const QString& b)
{
	return QRectF(result.positions.value(a), sizes.value(a))
		.intersects(QRectF(result.positions.value(b), sizes.value(b)));
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QVector<RuntimeLayoutNode> chainNodes{node("a"), node("b"), node("c")};
	const QVector<RuntimeLayoutEdge> chainEdges{
		edge("ab", "a", "b"), edge("bc", "b", "c")};
	const RuntimeLayoutResult chain = RuntimeGraphLayout::compute(chainNodes, chainEdges);
	CHECK(chain.layers.value("a") == 0);
	CHECK(chain.layers.value("b") == 1);
	CHECK(chain.layers.value("c") == 2);
	CHECK(chain.positions.value("a").x() < chain.positions.value("b").x());
	CHECK(chain.positions.value("b").x() < chain.positions.value("c").x());

	const QVector<RuntimeLayoutNode> crossingNodes{
		node("root"), node("a"), node("b"), node("c"), node("d"), node("sink")};
	const RuntimeLayoutResult crossing = RuntimeGraphLayout::compute(
		crossingNodes, {edge("ra", "root", "a"), edge("rb", "root", "b"),
		                edge("ad", "a", "d"), edge("bc", "b", "c"),
		                edge("cs", "c", "sink"), edge("ds", "d", "sink")});
	CHECK(crossing.positions.value("a").y() < crossing.positions.value("b").y());
	CHECK(crossing.positions.value("d").y() < crossing.positions.value("c").y());

	const QVector<RuntimeLayoutNode> cycleNodes{node("a"), node("b"), node("c")};
	const QVector<RuntimeLayoutEdge> cycleEdges{
		edge("ab", "a", "b"), edge("bc", "b", "c"), edge("ca", "c", "a")};
	const RuntimeLayoutResult cycle = RuntimeGraphLayout::compute(cycleNodes, cycleEdges);
	CHECK(cycle.layers.value("a") == cycle.layers.value("b"));
	CHECK(cycle.layers.value("b") == cycle.layers.value("c"));
	QHash<QString, QSizeF> standardSizes{{"a", QSizeF(120, 70)},
	                                    {"b", QSizeF(120, 70)},
	                                    {"c", QSizeF(120, 70)}};
	CHECK(!overlaps(cycle, standardSizes, "a", "b"));
	CHECK(!overlaps(cycle, standardSizes, "b", "c"));

	QVector<RuntimeLayoutNode> disconnectedNodes{
		node("a", 180, 90), node("b", 100, 160), node("x", 240, 60), node("y", 80, 80)};
	QVector<RuntimeLayoutEdge> disconnectedEdges{
		edge("ab", "a", "b"), edge("xy", "x", "y")};
	const RuntimeLayoutResult disconnected =
		RuntimeGraphLayout::compute(disconnectedNodes, disconnectedEdges);
	QHash<QString, QSizeF> variedSizes{{"a", QSizeF(180, 90)}, {"b", QSizeF(100, 160)},
	                                  {"x", QSizeF(240, 60)}, {"y", QSizeF(80, 80)}};
	for (const QString& left : variedSizes.keys())
		for (const QString& right : variedSizes.keys())
			if (left < right)
				CHECK(!overlaps(disconnected, variedSizes, left, right));

	std::reverse(disconnectedNodes.begin(), disconnectedNodes.end());
	std::reverse(disconnectedEdges.begin(), disconnectedEdges.end());
	const RuntimeLayoutResult reordered =
		RuntimeGraphLayout::compute(disconnectedNodes, disconnectedEdges);
	CHECK(disconnected.positions == reordered.positions);
	CHECK(disconnected.layers == reordered.layers);

	RuntimeLayoutNode pinned = node("p", 140, 80);
	pinned.previousPosition = QPointF(500, 250);
	pinned.hasPreviousPosition = true;
	pinned.pinned = true;
	const QVector<RuntimeLayoutNode> pinnedNodes{pinned, node("q", 140, 80)};
	const RuntimeLayoutResult pinnedResult = RuntimeGraphLayout::compute(
		pinnedNodes, {edge("pq", "p", "q")});
	CHECK(pinnedResult.positions.value("p") == QPointF(500, 250));
	QHash<QString, QSizeF> pinnedSizes{{"p", QSizeF(140, 80)}, {"q", QSizeF(140, 80)}};
	CHECK(!overlaps(pinnedResult, pinnedSizes, "p", "q"));

	const QVector<RuntimeLayoutNode> aliasNodes{node("root"), node("shared")};
	const RuntimeLayoutResult aliases = RuntimeGraphLayout::compute(
		aliasNodes, {edge("left", "root", "shared"), edge("right", "root", "shared")});
	CHECK(aliases.positions.size() == 2);
	CHECK(aliases.layers.value("shared") == aliases.layers.value("root") + 1);

	QVector<RuntimeLayoutNode> largeNodes;
	QVector<RuntimeLayoutEdge> largeEdges;
	largeNodes.reserve(1000);
	largeEdges.reserve(999);
	for (int i = 0; i < 1000; ++i) {
		const QString id = QStringLiteral("n%1").arg(i, 4, 10, QLatin1Char('0'));
		largeNodes.push_back({id, QSizeF(80 + (i % 5) * 10, 50 + (i % 7) * 5)});
		if (i > 0) {
			const QString previous = QStringLiteral("n%1").arg(i - 1, 4, 10, QLatin1Char('0'));
			largeEdges.push_back({QStringLiteral("e%1").arg(i), previous, id});
		}
	}
	QElapsedTimer timer;
	timer.start();
	const RuntimeLayoutResult large = RuntimeGraphLayout::compute(largeNodes, largeEdges);
	CHECK(large.positions.size() == 1000);
	CHECK(large.layers.value("n0999") == 999);
	CHECK(timer.elapsed() < 5000);

	return EXIT_SUCCESS;
}
