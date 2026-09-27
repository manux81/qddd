// Non-GUI tests for the generic execution-history subsystem.
#include "history/DisplayedState.h"
#include "history/ExecutionComparison.h"
#include "history/FlowModel.h"
#include "history/GdbStopHistoryBackend.h"
#include "history/GdbRecordTimeMachineBackend.h"
#include "history/TimeMachineModel.h"
#include "history/HistorySession.h"
#include "history/RecordingMetadata.h"
#include "history/TargetDescriptor.h"
#include "history/TimelineTrack.h"
#include "ValueHistory.h"

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

// Minimal DebuggerSession stub: scripted snapshots, records every
// target-commanding call so tests can prove history browsing is read-only.
class FakeDebuggerSession : public DebuggerSession {
public:
    QVector<ExecutionSnapshot> scriptedHistory;
    int scriptedStep = 0;
    QStringList targetCommands; // non-empty iff the live target was commanded
    bool recorderSupported = false;
    bool recorderActive = false;
    bool reverseSupported = false;

    const QVector<ExecutionSnapshot> &executionHistory() const override
    {
        return scriptedHistory;
    }
    const ExecutionSnapshot *snapshotAt(int index) const override
    {
        if (index < 0 || index >= scriptedHistory.size())
            return nullptr;
        return &scriptedHistory[index];
    }
    int lastStopStepIndex() const override { return scriptedStep; }

    // Everything below is an inert stub that logs target commands.
    void setBackend(Backend) override {}
    void setGdbExecutable(const QString &) override {}
    void setLldbMiExecutable(const QString &) override {}
    void setTargetType(TargetType) override {}
    void setReverseMode(ReverseMode) override {}
    void setRemoteEndpoint(const QString &, int) override {}
    void setRemoteConnectCommands(const QStringList &, bool = false) override {}
    void setStlinkServerPath(const QString &) override {}
    void setStlinkGdbPort(int) override {}
    void setCommandTimeoutMs(int) override {}
    void startSession(const QString &) override { targetCommands << QStringLiteral("start"); }
    void terminateSession() override { targetCommands << QStringLiteral("terminate"); }
    bool isRunning() const override { return true; }
    void run() override { targetCommands << QStringLiteral("run"); }
    void continueExecution() override { targetCommands << QStringLiteral("continue"); }
    void stepInto() override { targetCommands << QStringLiteral("stepInto"); }
    void stepOver() override { targetCommands << QStringLiteral("stepOver"); }
    void stepOut() override { targetCommands << QStringLiteral("stepOut"); }
    void interruptExecution() override { targetCommands << QStringLiteral("interrupt"); }
    void runToCursor(const QString &) override { targetCommands << QStringLiteral("runToCursor"); }
    void reverseContinueExecution() override { targetCommands << QStringLiteral("reverseContinue"); }
    void reverseStepInto() override { targetCommands << QStringLiteral("reverseStepInto"); }
    void reverseStepOver() override { targetCommands << QStringLiteral("reverseStepOver"); }
    bool supportsReverseExecution() const override { return false; }
    bool startTimeMachineRecording() override
    {
        if (!recorderSupported || recorderActive)
            return false;
        targetCommands << QStringLiteral("startRecording");
        return true;
    }
    bool stopTimeMachineRecording() override
    {
        if (!recorderActive)
            return false;
        targetCommands << QStringLiteral("stopRecording");
        return true;
    }
    bool reverseFinishExecution() override
    {
        if (!reverseSupported)
            return false;
        targetCommands << QStringLiteral("reverseFinish");
        return true;
    }
    bool returnToPresentExecution() override
    {
        if (!reverseSupported)
            return false;
        targetCommands << QStringLiteral("returnToPresent");
        return true;
    }
    bool timeMachineRecordingActive() const override { return recorderActive; }
    bool timeMachineRecordingSupported() const override { return recorderSupported; }
    void insertBreakpoint(const BreakpointRequest &) override { targetCommands << QStringLiteral("break"); }
    void insertBreakpoint(const QString &) override { targetCommands << QStringLiteral("break"); }
    void removeBreakpoint(int) override {}
    void clearAllBreakpoints() override {}
    void setBreakpointEnabled(int, bool) override {}
    void toggleBreakpoint(const QString &) override {}
    void updateBreakpointCondition(int, const QString &) override {}
    void updateBreakpointIgnoreCount(int, int) override {}
    void updateBreakpointTemporary(int, bool) override {}
    void selectStackFrame(int) override {}
    void selectThread(const QString &) override {}
    const QVector<DebugThread> &threads() const override { return m_threads; }
    QString selectedThread() const override { return {}; }
    const QVector<StackFrame> &stackFrames() const override { return m_frames; }
    const std::vector<std::unique_ptr<DebugVariable>> &variables() const override
    {
        return m_variables;
    }
    const RuntimeObjectGraph &objectGraph() const override { return m_graph; }
    const RuntimeGraphDiff &graphChanges() const override { return m_diff; }
    const QSet<QString> &changedPaths() const override { return m_changed; }
    const QVector<BreakpointInfo> &breakpoints() const override { return m_breakpoints; }
    void setMaxSnapshots(int) override {}
    int maxSnapshots() const override { return 500; }
    void inspectValue(const QString &, std::function<void(SemanticValue)>) override {}
    void readMemory(const QString &, int, std::function<void(MemoryRead)>) override
    {
        targetCommands << QStringLiteral("readMemory");
    }
    void evaluateExpression(const QString &) override {}
    void addWatchExpression(const QString &) override {}
    void removeWatchExpression(const QString &) override {}
    void replaceWatchExpression(const QString &, const QString &) override {}
    void setWatchExpressionEnabled(const QString &, bool) override {}
    bool isWatchExpressionEnabled(const QString &) const override { return false; }
    const QStringList &watchExpressions() const override { return m_watches; }
    void setValueFormat(const QString &, DebugValueFormat) override {}
    DebugValueFormat valueFormat(const QString &) const override
    {
        return DebugValueFormat::Natural;
    }
    QString formattedValue(const DebugVariable *v) const override
    {
        return v ? v->value : QString();
    }
    void sendRawCommand(const QString &, std::function<void(const QString &)> = nullptr) override
    {
        targetCommands << QStringLiteral("raw");
    }
    void setVariable(const QString &, const QString &) override
    {
        targetCommands << QStringLiteral("setVariable");
    }
    void requestDisassembly(const QString &, int, int = 80) override {}
    void requestDisassemblyAtLastStop(int = 80) override {}
    void dereferencePointer(const QString &, std::function<void(const QString &, const QString &)>) override
    {
        targetCommands << QStringLiteral("deref");
    }
    void evaluateExpressionValue(const QString &, std::function<void(const QString &, const QString &)>) override
    {
        targetCommands << QStringLiteral("eval");
    }
    void replaceExternalVariables(const QMap<QString, QString> &) override {}
    void replaceExternalStackFrames(const QVector<StackFrame> &) override {}

private:
    QVector<DebugThread> m_threads;
    QVector<StackFrame> m_frames;
    std::vector<std::unique_ptr<DebugVariable>> m_variables;
    RuntimeObjectGraph m_graph;
    RuntimeGraphDiff m_diff;
    QSet<QString> m_changed;
    QVector<BreakpointInfo> m_breakpoints;
    QStringList m_watches;
};

static ExecutionSnapshot makeSnapshot(int step, const QString &xValue, int line)
{
    ExecutionSnapshot snapshot;
    snapshot.stepIndex = step;
    snapshot.timestampNs = qint64(step) * 1000000;
    snapshot.file = QStringLiteral("main.c");
    snapshot.line = line;
    snapshot.function = QStringLiteral("main");
    snapshot.variableValues.insert(QStringLiteral("x"), xValue);
    return snapshot;
}

static TraceEvent makeLinkedEvent(TimePoint t, int step)
{
    TraceEvent event = makeEvent(t, TraceEventType::Stop, TrackIds::Cpu);
    event.metadata.insert(QString::fromLatin1(SnapshotStepKey), step);
    return event;
}

// Minimal seek-capable backend: selections drive the (fake) target, so the
// temporal state must read REPLAY rather than HISTORIC.
class FakeSeekBackend : public HistoryBackend {
public:
    bool isAvailable() const override { return true; }
    HistoryCapabilities capabilities() const override
    {
        return HistoryCapability::Recording | HistoryCapability::Seek;
    }
    std::optional<ExecutionState> currentState() override { return std::nullopt; }
    bool seek(TimePoint) override { return true; }
    bool stepForward() override { return false; }
    bool stepBackward() override { return false; }
    bool continueForward() override { return false; }
    bool continueBackward() override { return false; }
    bool returnToPresent() override { return true; }
    std::vector<TraceEvent> events(TimeRange) override { return {}; }
};

static int checkTemporalStates()
{
    // Live with no selection.
    {
        FakeDebuggerSession fake;
        HistorySession session;
        DisplayedStateModel display(&fake, &session);
        CHECK(display.temporalState() == TemporalState::Live);
        CHECK(temporalBadgeText(TemporalState::Live) == QStringLiteral("LIVE"));
    }
    // Snapshot-only backend: selecting history inspects, never rewinds.
    {
        FakeDebuggerSession fake;
        fake.scriptedHistory = {makeSnapshot(1, QStringLiteral("10"), 11),
                                makeSnapshot(2, QStringLiteral("20"), 12)};
        HistorySession session;
        session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
            for (const ExecutionSnapshot &s : fake.scriptedHistory) {
                if (s.stepIndex == step)
                    return s;
            }
            return std::nullopt;
        });
        session.setBackend(nullptr);
        session.addEvent(makeLinkedEvent(100, 1));
        session.addEvent(makeLinkedEvent(200, 2));
        DisplayedStateModel display(&fake, &session);
        session.selectEvent(session.storedEvents()[0].id);
        CHECK(display.temporalState() == TemporalState::Historic);
        CHECK(temporalBadgeText(TemporalState::Historic, 42)
		      == QStringLiteral("TIME MACHINE \u00B7 HISTORIC \u00B7 #42"));
        CHECK(temporalBadgeText(TemporalState::Replayed, 3)
              == QStringLiteral("TIME MACHINE \u00B7 REPLAY \u00B7 #3"));
    }
    // Seek-capable backend: selecting history moved the target.
    {
        FakeDebuggerSession fake;
        fake.scriptedHistory = {makeSnapshot(1, QStringLiteral("10"), 11),
                                makeSnapshot(2, QStringLiteral("20"), 12)};
        HistorySession session;
        FakeSeekBackend backend;
        session.setBackend(&backend);
        session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
            for (const ExecutionSnapshot &s : fake.scriptedHistory) {
                if (s.stepIndex == step)
                    return s;
            }
            return std::nullopt;
        });
        session.addEvent(makeLinkedEvent(100, 1));
        session.addEvent(makeLinkedEvent(200, 2));
        DisplayedStateModel display(&fake, &session);
        session.selectEvent(session.storedEvents()[0].id);
        CHECK(display.temporalState() == TemporalState::Replayed);
        display.goLive();
        CHECK(display.temporalState() == TemporalState::Live);
    }
    return 0;
}

static int checkActionStates()
{
    FakeDebuggerSession fake;
    HistorySession empty;
    empty.setBackend(nullptr);

    // Nothing recorded: only ShowTimeline works, with reasons everywhere.
    CHECK(!timeMachineActionState(TimeMachineAction::Previous, &empty, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::Next, &empty, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::ReturnToPresent, &empty, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::StartRecording, &empty, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::StopRecording, &empty, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::ReverseContinue, &empty, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::PreviousWrite, &empty, &fake).enabled);
    CHECK(timeMachineActionState(TimeMachineAction::ShowTimeline, &empty, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::Previous, &empty, &fake).reason.isEmpty());

    // Snapshot-only history: navigation + present work, reverse does not.
    HistorySession session;
    session.setBackend(nullptr);
    session.addEvent(makeLinkedEvent(100, 1));
    session.addEvent(makeLinkedEvent(200, 2));
    CHECK(timeMachineActionState(TimeMachineAction::Previous, &session, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::Next, &session, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::ReturnToPresent, &session, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::ReverseContinue, &session, &fake).enabled);
    CHECK(!timeMachineActionState(TimeMachineAction::ReverseContinue, &session, &fake)
               .reason.isEmpty());
    session.selectEvent(session.storedEvents()[0].id);
    CHECK(!timeMachineActionState(TimeMachineAction::Previous, &session, &fake).enabled);
    CHECK(timeMachineActionState(TimeMachineAction::Next, &session, &fake).enabled);
    CHECK(timeMachineActionState(TimeMachineAction::ReturnToPresent, &session, &fake).enabled);
    CHECK(timeMachineActionState(TimeMachineAction::MakeCheckpoint, &session, &fake).enabled
          == session.capabilities().testFlag(HistoryCapability::Checkpoints));
    return 0;
}

static int checkPointModel()
{
    FakeDebuggerSession fake;
    fake.scriptedHistory = {makeSnapshot(1, QStringLiteral("10"), 11),
                            makeSnapshot(2, QStringLiteral("20"), 12)};
    HistorySession session;
    session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
        for (const ExecutionSnapshot &s : fake.scriptedHistory) {
            if (s.stepIndex == step)
                return s;
        }
        return std::nullopt;
    });
    session.setBackend(nullptr);
    session.addEvent(makeLinkedEvent(100, 1));
    session.addEvent(makeLinkedEvent(200, 2));

    HistoryPointModel model;
    model.setSession(&session);
    // Row 0 is the present; history follows newest-first.
    CHECK(model.rowCount() == 3);
    CHECK(model.isPresentRow(0) && !model.isPresentRow(1));
    CHECK(model.pointIdAtRow(0) == InvalidTraceEventId);
    const TraceEventId newest = session.storedEvents()[1].id;
    const TraceEventId oldest = session.storedEvents()[0].id;
    CHECK(model.pointIdAtRow(1) == newest);
    CHECK(model.pointIdAtRow(2) == oldest);
    CHECK(model.rowForPoint(newest) == 1);
    CHECK(model.rowForPoint(oldest) == 2);
    CHECK(model.rowForPoint(999999) == -1);
    // Live session selects the present row.
    CHECK(model.data(model.index(0, 0), HistoryPointModel::IsSelectedRole).toBool());
    CHECK(!model.data(model.index(1, 0), HistoryPointModel::IsSelectedRole).toBool());
    // Selecting history moves selection and exposes point data.
    session.selectEvent(oldest);
    CHECK(model.data(model.index(2, 0), HistoryPointModel::IsSelectedRole).toBool());
    CHECK(!model.data(model.index(0, 0), HistoryPointModel::IsSelectedRole).toBool());
    CHECK(model.data(model.index(2, 0), HistoryPointModel::HasSnapshotRole).toBool());
    CHECK(model.data(model.index(2, 0), HistoryPointModel::SequenceRole).toUInt() == 0);
    CHECK(model.data(model.index(2, 0), HistoryPointModel::PointIdRole).toULongLong()
          == oldest);
    CHECK(model.data(model.index(2, 0), Qt::DisplayRole).toString().startsWith(
          QStringLiteral("#%1").arg(oldest)));
    CHECK(!model.data(model.index(2, 0), Qt::DisplayRole).toString().startsWith(
          QStringLiteral("#0"))); // sequence/row must never become identity
    CHECK(!model.data(model.index(1, 0), HistoryPointModel::IsCheckpointRole).toBool());
    CHECK(!model.data(model.index(2, 0), HistoryPointModel::LocationRole).toString().isEmpty());
    CHECK(!model.data(model.index(2, 0), HistoryPointModel::SubtitleRole).toString().isEmpty());
    return 0;
}

static int checkHistoryChanges()
{
    FakeDebuggerSession fake;
    ExecutionSnapshot first = makeSnapshot(1, QStringLiteral("10"), 11);
    first.variableValues.insert(QStringLiteral("gone"), QStringLiteral("old"));
    ExecutionSnapshot second = makeSnapshot(2, QStringLiteral("20"), 12);
    second.variableValues.insert(QStringLiteral("added"), QStringLiteral("new"));
    fake.scriptedHistory = {first, second};
    HistorySession session;
    session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
        for (const ExecutionSnapshot &s : fake.scriptedHistory) {
            if (s.stepIndex == step)
                return s;
        }
        return std::nullopt;
    });
    session.setBackend(nullptr);
    session.addEvent(makeLinkedEvent(100, 1));
    session.addEvent(makeLinkedEvent(200, 2));
    const TraceEventId firstId = session.storedEvents()[0].id;
    const TraceEventId secondId = session.storedEvents()[1].id;

    // Changed, added and removed paths are all reported with old → new.
    const HistoryChangeSet set = computeHistoryChanges(&session, secondId);
    CHECK(set.hasSnapshot);
    CHECK(set.values.value(QStringLiteral("x")) == QStringLiteral("20"));
    CHECK(set.changes.size() == 3);
    CHECK(set.changes[0].path == QStringLiteral("added"));
    CHECK(!set.changes[0].hadBefore && set.changes[0].hasNow);
    CHECK(set.changes[0].newValue == QStringLiteral("new"));
    CHECK(set.changes[1].path == QStringLiteral("gone"));
    CHECK(set.changes[1].hadBefore && !set.changes[1].hasNow);
    CHECK(set.changes[1].oldValue == QStringLiteral("old"));
    CHECK(set.changes[2].path == QStringLiteral("x"));
    CHECK(set.changes[2].oldValue == QStringLiteral("10"));
    CHECK(set.changes[2].newValue == QStringLiteral("20"));

    // First stop has no predecessor: everything reads as added.
    const HistoryChangeSet initial = computeHistoryChanges(&session, firstId);
    CHECK(initial.hasSnapshot);
    CHECK(initial.changes.size() == 2);
    CHECK(!initial.changes[0].hadBefore);

    // Unknown or unlinked events resolve to an empty set, never to live data.
    CHECK(!computeHistoryChanges(&session, 999999).hasSnapshot);
    CHECK(!computeHistoryChanges(nullptr, secondId).hasSnapshot);
    return 0;
}

static int checkTimeTravelController()
{
    FakeDebuggerSession fake;
    fake.scriptedHistory = {makeSnapshot(1, QStringLiteral("10"), 11),
                            makeSnapshot(2, QStringLiteral("20"), 12),
                            makeSnapshot(3, QStringLiteral("20"), 13)};
    HistorySession session;
    session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
        for (const ExecutionSnapshot &s : fake.scriptedHistory) {
            if (s.stepIndex == step)
                return s;
        }
        return std::nullopt;
    });
    session.setBackend(nullptr);
    session.addEvent(makeLinkedEvent(100, 1));
    session.addEvent(makeLinkedEvent(200, 2));
    session.addEvent(makeLinkedEvent(300, 3));
    const TraceEventId first = session.storedEvents()[0].id;
    const TraceEventId last = session.storedEvents()[2].id;

    TimeTravelController controller(&session);
    int positionSignals = 0;
    QObject::connect(&controller, &TimeTravelController::positionChanged,
                     [&](const TimeTravelPosition &) { ++positionSignals; });

    // Live is a separate state with no snapshot index.
    CHECK(controller.position().type == TimeTravelPosition::Type::Live);
    CHECK(controller.position().id == InvalidTraceEventId);
    CHECK(positionSignals == 0);

    // Snapshot navigation inspects without moving the target.
    controller.showSnapshot(first);
    CHECK(controller.position().type == TimeTravelPosition::Type::Snapshot);
    CHECK(controller.position().id == first);
    CHECK(positionSignals == 1);
    controller.showSnapshot(first);
    CHECK(positionSignals == 1); // no redundant updates
    controller.showPrevious();   // already oldest: stays
    CHECK(controller.position().id == first);
    controller.showNext();
    CHECK(controller.position().id == session.storedEvents()[1].id);
    controller.showLive();
    CHECK(controller.position().type == TimeTravelPosition::Type::Live);
    CHECK(fake.targetCommands.isEmpty());

    // Recorded-position navigation on a seek backend.
    FakeSeekBackend seekBackend;
    session.setBackend(&seekBackend);
    const TraceEventId middle = session.storedEvents()[1].id;
    controller.showRecordedPosition(middle);
    CHECK(controller.position().type == TimeTravelPosition::Type::RecordedPosition);
    CHECK(controller.position().id == middle);
    // Return to present restores live following.
    session.setBackend(nullptr);
    controller.showPresent();
    CHECK(controller.position().type == TimeTravelPosition::Type::Live);

    // Find previous change walks snapshots, never claims exact writes.
    controller.showSnapshot(last); // x == 20 at step 3
    const TraceEventId changedAt = controller.findPreviousChange(QStringLiteral("x"));
    CHECK(changedAt == session.storedEvents()[0].id); // x became 20 after step 1
    CHECK(controller.position().id == session.storedEvents()[0].id);
    CHECK(controller.findPreviousChange(QStringLiteral("missing")) == InvalidTraceEventId);
    CHECK(controller.findPreviousChange(QString()) == InvalidTraceEventId);
    CHECK(fake.targetCommands.isEmpty());
    return 0;
}

static int checkResumeReturnsLive()
{
    FakeDebuggerSession fake;
    fake.scriptedHistory = {makeSnapshot(1, QStringLiteral("10"), 11),
                            makeSnapshot(2, QStringLiteral("20"), 12)};
    HistorySession session;
    session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
        for (const ExecutionSnapshot &s : fake.scriptedHistory) {
            if (s.stepIndex == step)
                return s;
        }
        return std::nullopt;
    });
    session.setBackend(nullptr);
    session.addEvent(makeLinkedEvent(100, 1));
    session.addEvent(makeLinkedEvent(200, 2));
    DisplayedStateModel display(&fake, &session);

    // Browse history, then resume: the UI must realign to LIVE by itself.
    session.selectEvent(session.storedEvents()[0].id);
    CHECK(!display.displayedState().isLive());
    emit fake.targetRunning();
    CHECK(display.displayedState().isLive());
    CHECK(display.displayedState().historyPointId == InvalidHistoryPointId);

    // A new stop updates LIVE rather than mutating the old selection.
    fake.scriptedHistory.append(makeSnapshot(3, QStringLiteral("30"), 13));
    emit fake.snapshotCaptured(fake.scriptedHistory.back());
    CHECK(display.displayedState().isLive());
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
          == QStringLiteral("30"));
    return 0;
}

static int checkPositionChip()
{
    CHECK(positionChipText(TemporalState::Live, std::nullopt) == QStringLiteral("LIVE"));
    CHECK(positionChipText(TemporalState::Historic, std::nullopt)
          == QStringLiteral("SNAPSHOT"));
    ExecutionSnapshot snapshot;
    snapshot.stepIndex = 42;
    snapshot.timestampNs = 12483921LL;
    CHECK(positionChipText(TemporalState::Historic, snapshot) == QStringLiteral("S42"));
    const QString replayed = positionChipText(TemporalState::Replayed, snapshot);
    CHECK(replayed.startsWith(QStringLiteral("TRACE")));
    CHECK(replayed.contains(QStringLiteral("0.012484")));
    return 0;
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
        CHECK(session.selectedEventId() == session.storedEvents().back().id);
        CHECK(session.canSelectPreviousEvent());
        CHECK(!session.canSelectNextEvent());
        CHECK(session.selectPreviousEvent());
        CHECK(!session.isLive() && session.currentTime() == 200);
        CHECK(session.canSelectPreviousEvent() && session.canSelectNextEvent());
        CHECK(session.selectNextEvent());
        CHECK(session.isLive() && session.currentTime() == 300);
        CHECK(!session.selectNextEvent());

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

    // Explicit history points/checkpoints are ordered and retain the stable
    // snapshot link instead of embedding debugger state in the event list.
    {
        FakeDebuggerSession fake;
        fake.scriptedStep = 2;
        GdbRecordTimeMachineBackend backend(&fake);
        HistorySession session;
        session.setBackend(&backend);
        session.addEvent(makeLinkedEvent(200, 2));
        session.addEvent(makeLinkedEvent(100, 1));
        CHECK(session.historyPoints().size() == 2);
        CHECK(session.historyPoints()[0].sequence == 0);
        CHECK(session.historyPoints()[0].timestamp == 100);
        CHECK(session.historyPoints()[0].snapshotStep.value_or(-1) == 1);
        CHECK(session.createCheckpoint(session.historyPoints()[0].id));
        CHECK(session.checkpoint(session.historyPoints()[0].id)->snapshotStep.value_or(-1) == 1);
        CHECK(session.historyPoints()[0].type == HistoryPointType::Checkpoint);
        CHECK(!session.createCheckpoint(999999));
    }

    // GDB recorder capabilities are conservative until recording succeeds;
    // unsupported calls are rejected and accepted operations stay high-level.
    {
        FakeDebuggerSession fake;
        fake.recorderSupported = true;
        GdbRecordTimeMachineBackend backend(&fake);
        HistorySession session;
        session.setBackend(&backend);
        CHECK(session.capabilities().testFlag(HistoryCapability::RecordReplay));
        CHECK(!session.capabilities().testFlag(HistoryCapability::ReverseStep));
        CHECK(session.startRecording());
        CHECK(fake.targetCommands == QStringList{QStringLiteral("startRecording")});
        fake.recorderActive = true;
        fake.reverseSupported = true;
        // Fake session's legacy supportsReverseExecution remains false, so the
        // backend must not infer reverse capability merely from configuration.
        emit fake.reverseExecutionAvailabilityChanged();
        CHECK(!session.reverseStep());
        emit fake.timeMachineRecordingStateChanged(true);
        CHECK(session.isRecording());
        CHECK(session.stopRecording());
        CHECK(fake.targetCommands.contains(QStringLiteral("stopRecording")));
    }

    // Recording metadata: versioned round-trip + rejection.
    {
        HistoryRecordingMetadata meta;
        meta.target = QStringLiteral("generic-board");
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

    // Snapshot-step link carried in event metadata.
    {
        TraceEvent plain = makeEvent(10, TraceEventType::Stop, TrackIds::Cpu);
        CHECK(!traceSnapshotStep(plain).has_value());
        TraceEvent linked = makeLinkedEvent(20, 7);
        CHECK(traceSnapshotStep(linked).value_or(-1) == 7);
        TraceEvent garbage = makeEvent(30, TraceEventType::Stop, TrackIds::Cpu);
        garbage.metadata.insert(QString::fromLatin1(SnapshotStepKey),
                                QStringLiteral("not-a-step"));
        CHECK(!traceSnapshotStep(garbage).has_value());
    }

    // Backend records the stop sequence with the event: deterministic link,
    // no dependence on async snapshot timing.
    {
        FakeDebuggerSession fake;
        fake.scriptedStep = 7;
        GdbStopHistoryBackend backend(&fake);
        TraceEvent recorded;
        bool gotEvent = false;
        QObject::connect(&backend, &GdbStopHistoryBackend::stopRecorded,
                         [&](const TraceEvent &event) {
                             recorded = event;
                             gotEvent = true;
                         });
        emit fake.stoppedAt(QStringLiteral("main.c"), 42, QStringLiteral("main"));
        CHECK(gotEvent);
        CHECK(traceSnapshotStep(recorded).value_or(-1) == 7);
        CHECK(recorded.trackId == QString::fromLatin1(TrackIds::Cpu));
    }

    // snapshotForEvent resolves through the injected resolver by stable
    // stepIndex (eviction-safe), never by vector position.
    {
        HistorySession session;
        session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
            // Simulates an evicted history: only steps 5..6 retained.
            if (step == 5)
                return makeSnapshot(5, QStringLiteral("50"), 15);
            if (step == 6)
                return makeSnapshot(6, QStringLiteral("60"), 16);
            return std::nullopt;
        });
        session.addEvent(makeLinkedEvent(100, 5));
        session.addEvent(makeLinkedEvent(200, 6));
        session.addEvent(makeLinkedEvent(300, 4)); // evicted snapshot
        const TraceEventId first = session.storedEvents()[0].id;
        const auto snap5 = session.snapshotForEvent(first);
        CHECK(snap5.has_value() && snap5->variableValues.value(QStringLiteral("x"))
                  == QStringLiteral("50"));
        const auto evicted = session.snapshotForEvent(session.storedEvents()[2].id);
        CHECK(!evicted.has_value());
        CHECK(!session.snapshotForEvent(999999).has_value());

        HistorySession noResolver;
        noResolver.addEvent(makeLinkedEvent(100, 5));
        CHECK(!noResolver.snapshotForEvent(noResolver.storedEvents()[0].id).has_value());
    }

    // Displayed-state regression: x = 10 / 20 / 30 across three stops.
    // Selecting an old stop must display that stop's values; Go Live must
    // restore the latest. Browsing must never command the target.
    {
        FakeDebuggerSession fake;
        fake.scriptedHistory = {makeSnapshot(1, QStringLiteral("10"), 11),
                                makeSnapshot(2, QStringLiteral("20"), 12),
                                makeSnapshot(3, QStringLiteral("30"), 13)};
        HistorySession session;
        session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
            for (const ExecutionSnapshot &s : fake.scriptedHistory) {
                if (s.stepIndex == step)
                    return s;
            }
            return std::nullopt;
        });
        session.setBackend(nullptr);
        session.addEvent(makeLinkedEvent(100, 1));
        session.addEvent(makeLinkedEvent(200, 2));
        session.addEvent(makeLinkedEvent(300, 3));

        DisplayedStateModel display(&fake, &session);
        int stateSignals = 0;
        QObject::connect(&display, &DisplayedStateModel::displayedStateChanged,
                         [&](const DisplayedDebugState &) { ++stateSignals; });

        // Live shows the latest stop.
        CHECK(display.displayedState().isLive());
        CHECK(display.displayedState().snapshotStep == 3);
        CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
              == QStringLiteral("30"));

        // Browsing backward shows recorded values, not live ones.
        session.selectEvent(session.storedEvents()[0].id);
        CHECK(!display.displayedState().isLive());
        CHECK(display.displayedState().historyPointId ==
              session.storedEvents()[0].id);
        CHECK(display.displayedState().snapshotStep == 1);
        CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
              == QStringLiteral("10"));
        CHECK(display.displayedState().snapshot->line == 11);
        CHECK(display.displayedState().snapshot->file == QStringLiteral("main.c"));

        session.selectEvent(session.storedEvents()[1].id);
        CHECK(display.displayedState().historyPointId ==
              session.storedEvents()[1].id);
        CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
              == QStringLiteral("20"));

        // Go Live restores the latest/current presentation.
        display.goLive();
        CHECK(display.displayedState().isLive());
        CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
              == QStringLiteral("30"));
        CHECK(stateSignals >= 3);

        // History browsing never commands the live target.
        CHECK(fake.targetCommands.isEmpty());

        // A fresh stop while browsing history does not disturb the display.
        session.selectEvent(session.storedEvents()[0].id);
        const int signalsBefore = stateSignals;
        fake.scriptedHistory.append(makeSnapshot(4, QStringLiteral("40"), 14));
        emit fake.snapshotCaptured(fake.scriptedHistory.back());
        CHECK(!display.displayedState().isLive());
        CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
              == QStringLiteral("10"));
        CHECK(stateSignals == signalsBefore);

        // ...while live-following it moves to the latest snapshot.
        display.goLive();
        CHECK(display.displayedState().isLive());
        CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
              == QStringLiteral("40"));
    }

    // Value-history plot data still covers every recorded snapshot.
    {
        const QVector<ExecutionSnapshot> snapshots = {makeSnapshot(1, QStringLiteral("10"), 11),
                                                      makeSnapshot(2, QStringLiteral("20"), 12),
                                                      makeSnapshot(3, QStringLiteral("30"), 13)};
        const auto points = valueHistory(snapshots, QStringLiteral("x"));
        CHECK(points.size() == 3);
        CHECK(points[0].value == QStringLiteral("10") && !points[0].changed);
        CHECK(points[1].value == QStringLiteral("20") && points[1].changed);
        CHECK(points[2].value == QStringLiteral("30") && points[2].changed);
        CHECK(valueHistory(snapshots, QStringLiteral("missing"))[0].available == false);
    }

    // Recorded snapshots preserve variable structure: nested struct
    // children and pointers travel under their full paths plus object
    // identity, so historical views can render hierarchy, not flat lists.
    {
        ExecutionSnapshot structured;
        structured.stepIndex = 9;
        structured.timestampNs = 9000000;
        structured.variableValues.insert(QStringLiteral("x"), QStringLiteral("10"));
        structured.variableValues.insert(QStringLiteral("cfg"),
                                         QStringLiteral("{mode = 1, volt = 48}"));
        structured.variableValues.insert(QStringLiteral("cfg.mode"), QStringLiteral("1"));
        structured.variableValues.insert(QStringLiteral("cfg.volt"), QStringLiteral("48"));
        structured.variableValues.insert(QStringLiteral("cfg.sub.value"), QStringLiteral("7"));
        structured.variableValues.insert(QStringLiteral("p"), QStringLiteral("0x2000"));
        structured.objectIds.insert(QStringLiteral("cfg"), QStringLiteral("obj-1"));
        structured.objectIds.insert(QStringLiteral("p"), QStringLiteral("obj-2"));
        structured.changedPaths.insert(QStringLiteral("cfg.mode"));

        FakeDebuggerSession fake;
        fake.scriptedHistory = {structured};
        HistorySession session;
        session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
            for (const ExecutionSnapshot &s : fake.scriptedHistory) {
                if (s.stepIndex == step)
                    return s;
            }
            return std::nullopt;
        });
        TraceEvent event = makeEvent(100, TraceEventType::Stop, TrackIds::Cpu);
        event.metadata.insert(QString::fromLatin1(SnapshotStepKey), 9);
        session.addEvent(event);
        // A second (newer) event so selecting the first leaves live mode.
        TraceEvent newer = makeEvent(200, TraceEventType::Stop, TrackIds::Cpu);
        newer.metadata.insert(QString::fromLatin1(SnapshotStepKey), 10);
        session.addEvent(newer);

        DisplayedStateModel display(&fake, &session);
        session.selectEvent(session.storedEvents()[0].id);
        CHECK(!display.displayedState().isLive());
        const auto snapshot = display.displayedState().snapshot;
        CHECK(snapshot.has_value());
        // All hierarchy levels intact: parent, children, grandchild, pointer.
        CHECK(snapshot->variableValues.value(QStringLiteral("cfg.mode"))
              == QStringLiteral("1"));
        CHECK(snapshot->variableValues.value(QStringLiteral("cfg.volt"))
              == QStringLiteral("48"));
        CHECK(snapshot->variableValues.value(QStringLiteral("cfg.sub.value"))
              == QStringLiteral("7"));
        CHECK(snapshot->variableValues.value(QStringLiteral("p"))
              == QStringLiteral("0x2000"));
        CHECK(snapshot->objectIds.value(QStringLiteral("cfg")) == QStringLiteral("obj-1"));
        CHECK(snapshot->changedPaths.contains(QStringLiteral("cfg.mode")));
        // Nested paths are plottable per-stop like scalars.
        const auto points = valueHistory(fake.scriptedHistory, QStringLiteral("cfg.mode"));
        CHECK(points.size() == 1 && points[0].value == QStringLiteral("1"));
    }

    // An event whose snapshot was evicted still displays as HISTORY with
    // its step preserved (never silently rebound to live or to a sibling).
    {
        FakeDebuggerSession fake;
        fake.scriptedHistory = {makeSnapshot(2, QStringLiteral("20"), 12)};
        HistorySession session;
        session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
            for (const ExecutionSnapshot &s : fake.scriptedHistory) {
                if (s.stepIndex == step)
                    return s;
            }
            return std::nullopt; // step 1 evicted
        });
        session.setBackend(nullptr);
        session.addEvent(makeLinkedEvent(100, 1));
        session.addEvent(makeLinkedEvent(200, 2));

        DisplayedStateModel display(&fake, &session);
        session.selectEvent(session.storedEvents()[0].id);
        CHECK(!display.displayedState().isLive());
        CHECK(display.displayedState().snapshotStep == 1);
        CHECK(!display.displayedState().snapshot.has_value());
        CHECK(fake.targetCommands.isEmpty());

        // The surviving sibling still resolves normally.
        session.selectEvent(session.storedEvents()[1].id);
        CHECK(display.displayedState().isLive());
        CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
              == QStringLiteral("20"));
    }

    // Displayed-state updates are not redundant: re-selecting the same stop
    // or an already-live Go Live emits nothing new.
    {
        FakeDebuggerSession fake;
        fake.scriptedHistory = {makeSnapshot(1, QStringLiteral("10"), 11),
                                makeSnapshot(2, QStringLiteral("20"), 12)};
        HistorySession session;
        session.setSnapshotResolver([&](int step) -> std::optional<ExecutionSnapshot> {
            for (const ExecutionSnapshot &s : fake.scriptedHistory) {
                if (s.stepIndex == step)
                    return s;
            }
            return std::nullopt;
        });
        session.setBackend(nullptr);
        session.addEvent(makeLinkedEvent(100, 1));
        session.addEvent(makeLinkedEvent(200, 2));

        DisplayedStateModel display(&fake, &session);
        int stateSignals = 0;
        QObject::connect(&display, &DisplayedStateModel::displayedStateChanged,
                         [&](const DisplayedDebugState &) { ++stateSignals; });

        session.selectEvent(session.storedEvents()[0].id);
        CHECK(stateSignals == 1);
        session.selectEvent(session.storedEvents()[0].id);
        CHECK(stateSignals == 1);

        display.goLive();
        CHECK(stateSignals == 2);
        display.goLive();
        CHECK(stateSignals == 2);
        CHECK(display.displayedState().isLive());
        CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
              == QStringLiteral("20"));
        CHECK(fake.targetCommands.isEmpty());
    }

    std::cout << "history model tests passed\n";
    if (int rc = checkTemporalStates())
        return rc;
    if (int rc = checkActionStates())
        return rc;
    if (int rc = checkPointModel())
        return rc;
    if (int rc = checkHistoryChanges())
        return rc;
    if (int rc = checkTimeTravelController())
        return rc;
    if (int rc = checkResumeReturnsLive())
        return rc;
    if (int rc = checkPositionChip())
        return rc;
    std::cout << "time machine model tests passed\n";
    return 0;
}
