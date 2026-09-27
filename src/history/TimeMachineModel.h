#pragma once

// Time Machine presentation layer shared by the timeline, menus and badges.
//
// This file contains only QtCore-based, backend-agnostic presentation logic:
// a list model over HistorySession points, the single LIVE/HISTORIC/REPLAY
// badge vocabulary, and one capability-gating function used by every command
// surface so menus, transport buttons and tooltips can never disagree.
// Widget painting lives in HistoryView; no GDB/MI knowledge lives here.

#include "HistorySession.h"

#include <QAbstractListModel>
#include <QString>

#include <optional>

class DebuggerSession;

namespace qddd {
namespace history {

// Compact, explicit temporal indicator text. Never color-only.
inline QString temporalBadgeText(TemporalState state,
                                 HistoryPointId pointId = InvalidHistoryPointId)
{
    switch (state) {
    case TemporalState::Live:
        return QStringLiteral("LIVE");
    case TemporalState::Replayed:
        return pointId != InvalidHistoryPointId
            ? QStringLiteral("TIME MACHINE \u00B7 REPLAY \u00B7 #%1").arg(pointId)
            : QStringLiteral("TIME MACHINE \u00B7 REPLAY");
    case TemporalState::Historic:
        return pointId != InvalidHistoryPointId
            ? QStringLiteral("TIME MACHINE \u00B7 HISTORIC \u00B7 #%1").arg(pointId)
            : QStringLiteral("TIME MACHINE \u00B7 HISTORIC");
    }
    return QStringLiteral("LIVE");
}

inline QString historyPointTypeName(HistoryPointType type)
{
    switch (type) {
    case HistoryPointType::Stop:
        return QStringLiteral("Stop");
    case HistoryPointType::Checkpoint:
        return QStringLiteral("Checkpoint");
    case HistoryPointType::RecordingBoundary:
        return QStringLiteral("Recording boundary");
    case HistoryPointType::Marker:
        return QStringLiteral("Marker");
    }
    return QStringLiteral("Stop");
}

// Every Time Machine command surface (menu, transport, context menu).
enum class TimeMachineAction {
    StartRecording,
    StopRecording,
    Previous,
    Next,
    ReverseContinue,
    ReturnToPresent,
    ShowTimeline,
    ReverseStepIn,
    ReverseStepOver,
    PreviousWrite,
    MakeCheckpoint,
    BackendInfo
};

struct TimeMachineActionState {
    bool enabled = false;
    QString reason;
};

// Single capability-gating decision point. `reason` always explains WHY an
// action is unavailable so tooltips can quote it verbatim.
TimeMachineActionState timeMachineActionState(TimeMachineAction action,
                                              HistorySession *history,
                                              DebuggerSession *debug);

// Compact position chip text for the Data Display and status indicators.
// Distinct strings for semantically different things: LIVE is the actual
// target, S42 is recorded snapshot 42, TRACE is a replayed execution
// position. Never the raw history-point id.
inline QString positionChipText(TemporalState temporal,
                                const std::optional<ExecutionSnapshot> &snapshot)
{
    switch (temporal) {
    case TemporalState::Live:
        return QStringLiteral("LIVE");
    case TemporalState::Replayed: {
        const double seconds =
            snapshot ? snapshot->timestampNs / 1e9 : 0.0;
        return QStringLiteral("TRACE \u00B7 %1s")
            .arg(seconds, 0, 'f', snapshot ? 6 : 3);
    }
    case TemporalState::Historic:
        if (snapshot)
            return QStringLiteral("S%1").arg(snapshot->stepIndex);
        return QStringLiteral("SNAPSHOT");
    }
    return QStringLiteral("LIVE");
}

// A single old → new value difference between two consecutive recorded
// snapshots. `hadBefore`/`hasNow` distinguish added/removed values from
// unchanged ones; unchanged paths never appear here.
struct HistoryChange {
    QString path;
    QString oldValue;
    QString newValue;
    bool hadBefore = false;
    bool hasNow = false;
};

struct HistoryChangeSet {
    bool hasSnapshot = false;
    QList<HistoryChange> changes;
    QHash<QString, QString> values;
};

// Ordered old → new differences for one history point, from the union of
// the point's snapshot and the previous recorded snapshot. Pure model
// logic: the inspector renders whatever this returns.
HistoryChangeSet computeHistoryChanges(HistorySession *session, TraceEventId id);

// One authoritative selected debugging position for the whole UI.
//
// Live is a separate state, never "the last history index": selecting a
// snapshot inspects recorded state while the inferior keeps running
// (Snapshot), while a seek-capable backend actually moves the inferior to
// the recorded position (RecordedPosition). Views must never maintain
// their own selected index; they react to TimeTravelController instead.
struct TimeTravelPosition {
    enum class Type {
        Live,
        Snapshot,
        RecordedPosition
    };

    Type type = Type::Live;
    TraceEventId id = InvalidTraceEventId; // event for Snapshot/RecordedPosition

    bool operator==(const TimeTravelPosition &other) const
    {
        return type == other.type && id == other.id;
    }
    bool operator!=(const TimeTravelPosition &other) const { return !(*this == other); }
};

// Single navigation entry point for Time Travel UI. The position is derived
// from HistorySession on every read (no duplicated selection storage):
// changing it once updates timeline, Data Display, source and inspector
// consistently through the session signals.
class TimeTravelController : public QObject {
    Q_OBJECT
public:
    explicit TimeTravelController(HistorySession *session, QObject *parent = nullptr);

    TimeTravelPosition position() const;

    void showLive();
    // Inspect a recorded snapshot; never moves the target.
    void showSnapshot(TraceEventId id);
    // Move to a recorded position when the backend supports seeking,
    // otherwise fall back to snapshot inspection (honest semantics).
    void showRecordedPosition(TraceEventId id);
    void showPrevious();
    void showNext();
    // Return to the present, letting a replay backend restore it when it
    // owns execution; otherwise identical to showLive().
    void showPresent();
    // Snapshot fallback for "find previous change" (see free function
    // below); selects the result for inspection when found.
    TraceEventId findPreviousChange(const QString &expression) const;
signals:
    void positionChanged(const TimeTravelPosition &position);

private:
    void refresh();
    static TimeTravelPosition positionFor(const HistorySession *session);

    HistorySession *m_session = nullptr; // not owned
    TimeTravelPosition m_last;
};

// Snapshot fallback for "find previous change": nearest earlier stop
// whose snapshot value for `expression` differs, relative to the session's
// current selection (or latest stop when live). Returns Invalid id when
// nothing earlier differs. Never claims an exact write.
TraceEventId findPreviousChange(HistorySession *session,
                                const QString &expression);

// List model over HistorySession points for QTreeView/QListView timelines.
// Row 0 is always the present (live target); rows 1..N are history points
// newest-first. Generic over point/event types: future backends only add
// rows, never widget code.
class HistoryPointModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PointIdRole = Qt::UserRole + 1,
        SequenceRole,
        LocationRole,
        FunctionRole,
        ThreadRole,
        TypeRole,
        TimeRole,
        SubtitleRole,
        IsPresentRole,
        IsSelectedRole,
        IsCheckpointRole,
        HasSnapshotRole
    };

    explicit HistoryPointModel(QObject *parent = nullptr);

    void setSession(HistorySession *session);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool isPresentRow(int row) const { return row == 0; }
    TraceEventId pointIdAtRow(int row) const;
    int rowForPoint(TraceEventId id) const;

public slots:
    void refreshAll();
    void refreshSelection();

private:
    HistorySession *m_session = nullptr;
    int m_selectedRow = -1;
};

} // namespace history
} // namespace qddd

Q_DECLARE_METATYPE(qddd::history::TimeTravelPosition)
