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
    }
    refresh();
}

void DisplayedStateModel::goLive()
{
    if (m_history)
        m_history->goLive();
    refresh();
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
    if (a.mode != b.mode || a.snapshotStep != b.snapshotStep)
        return false;
    if (a.snapshot.has_value() != b.snapshot.has_value())
        return false;
    if (a.snapshot && b.snapshot)
        return a.snapshot->timestampNs == b.snapshot->timestampNs;
    return true;
}

} // namespace history
} // namespace qddd
