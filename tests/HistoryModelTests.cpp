// Non-GUI tests for the generic execution-history subsystem.
#include "history/ExecutionComparison.h"
#include "history/FlowModel.h"
#include "history/GdbStopHistoryBackend.h"
#include "history/HistorySession.h"
#include "history/RecordingMetadata.h"
#include "history/TargetDescriptor.h"
#include "history/TimelineTrack.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; return 1; } } while(false)

using namespace qddd::history;

static TraceEvent makeEvent(TimePoint t, TraceEventType type, const char *track)
{
    TraceEvent e;
    e.time = t;
    e.type = type;
    e.trackId = QString::fromLatin1(track);
    return e;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // TimePoint / TraceEvent ordering.
    {
        TraceEvent a = makeEvent(200, TraceEventType::Stop, TrackIds::Cpu);
        a.id = 5;
        TraceEvent b = makeEvent(100, TraceEventType::Stop, TrackIds::Cpu);
        b.id = 9;
        CHECK(b < a);
        CHECK(!(a < b));
        TraceEvent c = makeEvent(100, TraceEventType::Stop, TrackIds::Cpu);
        c.id = 3;
        CHECK(c < b); // same time -> id tiebreak
    }

    // Capability flags.
    {
        HistoryCapabilities caps = HistoryCapability::Recording | HistoryCapability::Seek;
        CHECK(caps.testFlag(HistoryCapability::Recording));
        CHECK(caps.testFlag(HistoryCapability::Seek));
        CHECK(!caps.testFlag(HistoryCapability::ReverseStep));
        const QStringList names = historyCapabilityNames(caps);
        CHECK(names.contains(QStringLiteral("recording")));
        CHECK(names.contains(QStringLiteral("seek")));
        CHECK(!names.contains(QStringLiteral("reverse-step")));
        CHECK(historyCapabilityNames({}).isEmpty());
    }

    // Session: store, range queries, track filtering, selection, seek.
    {
        HistorySession session;
        CHECK(!session.isAvailable()); // no backend
        CHECK(session.fullRange().isValid() == false);

        session.addEvent(makeEvent(100, TraceEventType::Stop, TrackIds::Cpu));
        session.addEvent(makeEvent(300, TraceEventType::Breakpoint, TrackIds::Breakpoints));
        session.addEvent(makeEvent(200, TraceEventType::Stop, TrackIds::Cpu));
        CHECK(session.storedEvents().size() == 3);
        // Out-of-order insert is sorted.
        CHECK(session.storedEvents()[0].time == 100);
        CHECK(session.storedEvents()[2].time == 300);
        // Ids auto-assigned and unique.
        CHECK(session.storedEvents()[0].id != session.storedEvents()[1].id);

        const TimeRange full = session.fullRange();
        CHECK(full.begin == 100 && full.end == 300);

        auto slice = session.events({150, 300});
        CHECK(slice.size() == 2);
        auto cpuOnly = session.events({0, 1000}, QString::fromLatin1(TrackIds::Cpu));
        CHECK(cpuOnly.size() == 2);
        CHECK(session.events({500, 600}).empty());
        CHECK(session.events({}).empty()); // invalid range

        const TraceEventId firstId = session.storedEvents()[0].id;
        session.selectEvent(firstId);
        CHECK(session.selectedEventId() == firstId);
        CHECK(session.currentTime() == 100);
        CHECK(!session.isLive());

        session.goLive();
        CHECK(session.isLive());
        CHECK(session.currentTime() == 300);

        // Seek clamps to the recorded range and moves the cursor.
        CHECK(session.seek(100000));
        CHECK(session.currentTime() == 300);
        CHECK(session.seek(0));
        CHECK(session.currentTime() == 100);
        CHECK(!session.seek(InvalidTime));

        // Undated events are rejected.
        TraceEvent undated;
        session.addEvent(undated);
        CHECK(session.storedEvents().size() == 3);

        session.clear();
        CHECK(session.storedEvents().empty());
        CHECK(session.selectedEventId() == InvalidTraceEventId);
    }

    // Session signals.
    {
        HistorySession session;
        int timeSignals = 0, selectSignals = 0, appendedSignals = 0;
        QObject::connect(&session, &HistorySession::currentTimeChanged,
                         [&](TimePoint) { ++timeSignals; });
        QObject::connect(&session, &HistorySession::selectedEventChanged,
                         [&](TraceEventId) { ++selectSignals; });
        QObject::connect(&session, &HistorySession::eventsAppended,
                         [&](int) { ++appendedSignals; });
        session.addEvent(makeEvent(10, TraceEventType::Stop, TrackIds::Cpu));
        CHECK(appendedSignals == 1 && timeSignals == 1);
        session.selectEvent(session.storedEvents()[0].id);
        CHECK(selectSignals == 1);
    }

    // FilterTrackProvider: generic track filtering over a shared store.
    {
        std::vector<TraceEvent> store = {
            makeEvent(10, TraceEventType::Stop, TrackIds::Cpu),
            makeEvent(20, TraceEventType::InterruptEnter, TrackIds::Interrupts),
            makeEvent(30, TraceEventType::MemoryWrite, TrackIds::Memory),
        };
        FilterTrackProvider interrupts(QString::fromLatin1(TrackIds::Interrupts),
                                       QStringLiteral("Interrupts"), 1, &store);
        CHECK(interrupts.id() == QString::fromLatin1(TrackIds::Interrupts));
        auto got = interrupts.events({0, 100});
        CHECK(got.size() == 1 && got[0].type == TraceEventType::InterruptEnter);
        CHECK(interrupts.events({25, 100}).empty());
        FilterTrackProvider nullProvider(QStringLiteral("x"), QStringLiteral("X"), 9, nullptr);
        CHECK(nullProvider.events({0, 100}).empty());
    }

    // Stop-recording backend without a live session: honest capability gate.
    {
        GdbStopHistoryBackend backend(nullptr);
        CHECK(!backend.isAvailable());
        CHECK(backend.capabilities() == HistoryCapabilities(HistoryCapability::Recording));
        CHECK(!backend.capabilities().testFlag(HistoryCapability::Seek));
        CHECK(!backend.capabilities().testFlag(HistoryCapability::ReverseStep));
        CHECK(!backend.capabilities().testFlag(HistoryCapability::ReverseContinue));
        CHECK(!backend.capabilities().testFlag(HistoryCapability::DeterministicReplay));
        CHECK(!backend.capabilities().testFlag(HistoryCapability::MemoryWriteHistory));
        CHECK(!backend.seek(100));
        CHECK(!backend.stepForward() && !backend.stepBackward());
        CHECK(!backend.continueForward() && !backend.continueBackward());
        CHECK(!backend.findPreviousWrite(0x20000000, 100).has_value());
        CHECK(!backend.currentState().has_value());
        CHECK(backend.events({0, 100}).empty());
    }

    // Recording metadata: versioned round-trip + rejection.
    {
        HistoryRecordingMetadata meta;
        meta.target = QStringLiteral("stm32f4-discovery");
        meta.backend = QStringLiteral("gdb-stop-recording");
        meta.firmwarePath = QStringLiteral("/fw/app.elf");
        meta.firmwareBuildId = QStringLiteral("abc123");
        meta.capabilities = historyCapabilityNames(HistoryCapability::Recording);
        bool ok = false;
        const HistoryRecordingMetadata back =
            HistoryRecordingMetadata::deserialize(meta.serialize(), &ok);
        CHECK(ok);
        CHECK(back.version == HistoryFileVersion);
        CHECK(back.target == meta.target && back.backend == meta.backend);
        CHECK(back.firmwarePath == meta.firmwarePath);
        CHECK(back.firmwareBuildId == meta.firmwareBuildId);
        CHECK(back.capabilities == meta.capabilities);

        bool badOk = true;
        HistoryRecordingMetadata::deserialize(QByteArray("not json"), &badOk);
        CHECK(!badOk);
        bool wrongFormat = true;
        HistoryRecordingMetadata::fromJson(QJsonObject(), &wrongFormat);
        CHECK(!wrongFormat);
    }

    // Comparison: first divergence over recorded model data.
    {
        ExecutionRecording a, b;
        a.id = QStringLiteral("A");
        b.id = QStringLiteral("B");
        a.events = {makeEvent(1, TraceEventType::Stop, TrackIds::Cpu),
                    makeEvent(2, TraceEventType::Stop, TrackIds::Cpu)};
        b.events = a.events;
        CHECK(!findFirstDivergence(a, b).hasDivergence);
        b.events[1].type = TraceEventType::Exception;
        const DivergencePoint div = findFirstDivergence(a, b);
        CHECK(div.hasDivergence && div.indexA == 1 && !div.reason.isEmpty());
        b.events = {a.events[0]};
        CHECK(findFirstDivergence(a, b).hasDivergence);
    }

    // Flow model: call tree from function enter/exit events.
    {
        std::vector<TraceEvent> events;
        TraceEvent enter = makeEvent(1, TraceEventType::FunctionEnter, TrackIds::Cpu);
        SourceLocation mainLoc{QStringLiteral("main.c"), 10, QStringLiteral("main")};
        enter.source = mainLoc;
        TraceEvent nested = makeEvent(2, TraceEventType::FunctionEnter, TrackIds::Cpu);
        SourceLocation subLoc{QStringLiteral("sub.c"), 4, QStringLiteral("sub")};
        nested.source = subLoc;
        events.push_back(enter);
        events.push_back(nested);
        events.push_back(makeEvent(3, TraceEventType::FunctionExit, TrackIds::Cpu));
        events.push_back(makeEvent(4, TraceEventType::FunctionExit, TrackIds::Cpu));
        const auto root = buildCallTree(events);
        CHECK(root->children.size() == 1);
        CHECK(root->children[0]->function == QStringLiteral("main"));
        CHECK(root->children[0]->children.size() == 1);
        CHECK(root->children[0]->children[0]->function == QStringLiteral("sub"));
        const auto empty = buildCallTree({});
        CHECK(empty->children.empty());
    }

    // Target descriptors: generic fallback + architecture-level Cortex-M.
    {
        GenericTargetDescriptor generic(QStringLiteral("ARM"), QStringLiteral("Cortex-M4"),
                                        QStringLiteral("board"));
        CHECK(generic.architecture() == QStringLiteral("ARM"));
        CHECK(!generic.interruptName(11).has_value());
        CHECK(!generic.peripheralName(0x40000000).has_value());

        ArmCortexMTargetDescriptor cortexM;
        CHECK(cortexM.architecture() == QStringLiteral("ARM"));
        CHECK(cortexM.interruptName(3).value_or(QString()) == QStringLiteral("HardFault"));
        CHECK(cortexM.interruptName(15).value_or(QString()) == QStringLiteral("SysTick"));
        // External vectors stay SoC-agnostic: no STM32 vector names in core.
        CHECK(cortexM.interruptName(16 + 20).value_or(QString()) == QStringLiteral("IRQ20"));
    }

    std::cout << "history model tests passed\n";
    return 0;
}
