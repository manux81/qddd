#pragma once

// Displayed debug state: WHAT THE UI SHOWS, not what the target does.
//
// UI display state != target execution state. When the user selects an older
// stop in History, views render the recorded ExecutionSnapshot for that stop
// while the live target keeps running untouched:
//
//   Displayed state = snapshot N-3   (Historical mode)
//   Target state    = current/live N
//
// This model is the single authority for that distinction. Source, variables,
// Data Display and details all follow displayedStateChanged(); nothing
// subscribes to HistorySession selection signals directly for display
// purposes. Selecting a stop means "inspect the state captured at that
// stop" — never "the target has rewound".

#include "DebugSession.h"
#include "HistorySession.h"

#include <QObject>

#include <optional>

namespace qddd {
namespace history {

struct DisplayedDebugState {
    enum class Mode {
        Live,
        Historical
    };

    Mode mode = Mode::Live;

    // Snapshot currently on display. In Live mode this is the latest
    // recorded snapshot (empty when nothing was captured yet); in
    // Historical mode it is the snapshot linked to the selected event
    // (empty when the event has no captured snapshot).
    std::optional<ExecutionSnapshot> snapshot;

    // Canonical identity shown by every view. This is the HistorySession
    // point/event id, never a row, vector index, sequence, or snapshot step.
    HistoryPointId historyPointId = InvalidHistoryPointId;

    // Internal snapshot correlation only. Never present this as the history
    // point number; it is not the public identity.
    int snapshotStep = -1;

    bool isLive() const { return mode == Mode::Live; }
};

class DisplayedStateModel : public QObject {
    Q_OBJECT
public:
    // Neither session is owned. The resolver-free session API is used:
    // snapshots resolve through HistorySession::snapshotForEvent, and the
    // latest live snapshot through DebuggerSession::executionHistory().
    DisplayedStateModel(DebuggerSession *debugSession, HistorySession *historySession,
                        QObject *parent = nullptr);

    DisplayedDebugState displayedState() const { return m_state; }

    // TemporalState reuses the backend vocabulary (HistoryTypes.h) instead
    // of duplicating it: Live while following the present; Replayed when a
    // seek-capable backend drives the selection (the inferior was actually
    // moved); Historic when only a recorded snapshot is inspected while the
    // inferior stays elsewhere. Current backends yield Historic.
    TemporalState temporalState() const;

    // Return to the live/current presentation.
    void goLive();

signals:
    void displayedStateChanged(const DisplayedDebugState &state);

private:
    void refresh();
    bool statesEqual(const DisplayedDebugState &a, const DisplayedDebugState &b) const;

    DebuggerSession *m_debug = nullptr;     // not owned
    HistorySession *m_history = nullptr;    // not owned
    DisplayedDebugState m_state;
};

} // namespace history
} // namespace qddd

Q_DECLARE_METATYPE(qddd::history::DisplayedDebugState)
