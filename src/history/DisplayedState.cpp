#include "DisplayedState.h"

namespace qddd {
namespace history {

DisplayedStateModel::DisplayedStateModel(DebuggerSession *debugSession,
                                         HistorySession *historySession,
                                         QObject *parent)
    : QObject(parent)
    , m_debug(debugSession)
    , m_history(historySession)
{
    qRegisterMetaType<DisplayedDebugState>();
    if (m_history) {
        connect(m_history, &HistorySession::selectedEventChanged,
                this, [this] { refresh(); });
        connect(m_history, &HistorySession::currentTimeChanged,
                this, [this] { refresh(); });
        connect(m_history, &HistorySession::eventsAppended,
                this, [this] { refresh(); });
    }
    if (m_debug) {
        // A fresh snapshot only changes the display while live-following;
        // refresh() itself decides (browsing history is never disturbed).
        connect(m_debug, &DebuggerSession::snapshotCaptured,
                this, [this] { refresh(); });
        connect(m_debug, &DebuggerSession::targetExited,
                this, [this] { refresh(); });
        // Resuming normal execution returns the UI to LIVE: a new stop (or
        // continued running) updates the present rather than silently
        // mutating a historical snapshot view.
        connect(m_debug, &DebuggerSession::targetRunning, this, [this] {
            if (m_history && !m_history->isLive())
                m_history->goLive();
        });
    }
    refresh();
}

void DisplayedStateModel::goLive()
{
    if (m_history)
        m_history->goLive();
    refresh();
}

TemporalState DisplayedStateModel::temporalState() const
{
    if (m_state.isLive())
        return TemporalState::Live;
    if (m_history && m_history->capabilities().testFlag(HistoryCapability::Seek))
        return TemporalState::Replayed;
    return TemporalState::Historic;
}

void DisplayedStateModel::refresh()
{
    DisplayedDebugState next;
    if (!m_history || m_history->isLive()) {
        next.mode = DisplayedDebugState::Mode::Live;
        if (m_debug && !m_debug->executionHistory().isEmpty()) {
            const ExecutionSnapshot &latest = m_debug->executionHistory().back();
            next.snapshot = latest;
            next.snapshotStep = latest.stepIndex;
        }
    } else {
        next.mode = DisplayedDebugState::Mode::Historical;
        if (const TraceEvent *event = m_history->selectedEvent()) {
            next.historyPointId = event->id;
            if (const std::optional<int> step = traceSnapshotStep(*event))
                next.snapshotStep = *step;
            next.snapshot = m_history->snapshotForEvent(event->id);
            if (next.snapshot)
                next.snapshotStep = next.snapshot->stepIndex;
        }
    }
    if (statesEqual(m_state, next))
        return;
    m_state = next;
    emit displayedStateChanged(m_state);
}

bool DisplayedStateModel::statesEqual(const DisplayedDebugState &a,
                                      const DisplayedDebugState &b) const
{
    if (a.mode != b.mode || a.historyPointId != b.historyPointId ||
        a.snapshotStep != b.snapshotStep)
        return false;
    if (a.snapshot.has_value() != b.snapshot.has_value())
        return false;
    if (a.snapshot && b.snapshot)
        return a.snapshot->timestampNs == b.snapshot->timestampNs;
    return true;
}

} // namespace history
} // namespace qddd
