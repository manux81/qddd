#include "TimeMachineModel.h"

#include "DebugSession.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <iterator>

namespace {
QString modelTr(const char *text)
{
    return QCoreApplication::translate("HistoryPointModel", text);
}
} // namespace


namespace qddd {
namespace history {

TimeMachineActionState timeMachineActionState(TimeMachineAction action,
                                              HistorySession *history,
                                              DebuggerSession *debug)
{
    TimeMachineActionState state;
    const HistoryCapabilities caps = history ? history->capabilities() : HistoryCapabilities{};
    const bool hasHistory = history && !history->storedEvents().empty();
    const bool isLive = !history || history->isLive();
    const bool reverseOk = debug && debug->supportsReverseExecution();
    const bool recording = history && history->isRecording();

    switch (action) {
    case TimeMachineAction::StartRecording:
        if (!caps.testFlag(HistoryCapability::RecordReplay)) {
            state.reason = QObject::tr(
                "Recording is unavailable for this target: the active backend owns no recorder. "
                "Stop snapshots are still recorded automatically.");
        } else if (recording) {
            state.reason = QObject::tr("Recording is already active.");
        } else {
            state.enabled = true;
        }
        return state;
    case TimeMachineAction::StopRecording:
        if (!caps.testFlag(HistoryCapability::RecordReplay)) {
            state.reason = QObject::tr(
                "Recording is unavailable for this target: the active backend owns no recorder.");
        } else if (!recording) {
            state.reason = QObject::tr("No recording is active.");
        } else {
            state.enabled = true;
        }
        return state;
    case TimeMachineAction::Previous:
        if (!hasHistory) {
            state.reason = QObject::tr("No execution history recorded yet.");
        } else if (history->canSelectPreviousEvent()) {
            state.enabled = true;
        } else {
            state.reason = QObject::tr("Already at the oldest recorded stop.");
        }
        return state;
    case TimeMachineAction::Next:
        if (!hasHistory) {
            state.reason = QObject::tr("No execution history recorded yet.");
        } else if (history->canSelectNextEvent()) {
            state.enabled = true;
        } else {
            state.reason = QObject::tr("Already at the present. Use Return to Present to follow live execution.");
        }
        return state;
    case TimeMachineAction::ReverseContinue:
        if (caps.testFlag(HistoryCapability::ReverseContinue) || reverseOk) {
            state.enabled = true;
        } else {
            state.reason = QObject::tr(
                "Reverse execution is unavailable for this target. "
                "Snapshot history is still available.");
        }
        return state;
    case TimeMachineAction::ReturnToPresent:
        if (!hasHistory) {
            state.reason = QObject::tr("No execution history recorded yet.");
        } else if (!isLive) {
            state.enabled = true;
        } else {
            state.reason = QObject::tr("Already at the present.");
        }
        return state;
    case TimeMachineAction::ShowTimeline:
        state.enabled = true;
        return state;
    case TimeMachineAction::ReverseStepIn:
    case TimeMachineAction::ReverseStepOver:
        if (reverseOk) {
            state.enabled = true;
        } else {
            state.reason = QObject::tr(
                "Reverse execution is unavailable for this target. "
                "Snapshot history is still available.");
        }
        return state;
    case TimeMachineAction::PreviousWrite:
        if (caps.testFlag(HistoryCapability::MemoryWriteHistory)) {
            state.enabled = true;
        } else {
            state.reason = QObject::tr("Requires a backend with memory-write history.");
        }
        return state;
    case TimeMachineAction::MakeCheckpoint:
        if (!caps.testFlag(HistoryCapability::Checkpoints)) {
            state.reason = QObject::tr("The active backend cannot retain checkpoints.");
        } else if (!hasHistory || (history->selectedEventId() == InvalidTraceEventId && isLive)) {
            state.reason = QObject::tr("Select a recorded stop first.");
        } else {
            state.enabled = true;
        }
        return state;
    case TimeMachineAction::BackendInfo:
        state.enabled = true;
        return state;
    }
    return state;
}

HistoryPointModel::HistoryPointModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

HistoryChangeSet computeHistoryChanges(HistorySession *session, TraceEventId id)
{
    HistoryChangeSet result;
    if (!session)
        return result;
    const auto snapshot = session->snapshotForEvent(id);
    if (!snapshot)
        return result;
    result.hasSnapshot = true;
    result.values = snapshot->variableValues;

    std::optional<ExecutionSnapshot> previous;
    const auto &events = session->storedEvents();
    for (auto it = events.begin(); it != events.end(); ++it) {
        if (it->id != id)
            continue;
        for (auto back = std::make_reverse_iterator(it);
             back != events.rend(); ++back) {
            if (back->id == id)
                continue;
            previous = session->snapshotForEvent(back->id);
            if (previous)
                break;
        }
        break;
    }

    QStringList keys = snapshot->variableValues.keys();
    if (previous) {
        for (const QString &key : previous->variableValues.keys()) {
            if (!keys.contains(key))
                keys.append(key);
        }
    }
    keys.sort();
    const QString missing = QStringLiteral("—");
    for (const QString &key : keys) {
        const bool hadBefore = previous && previous->variableValues.contains(key);
        const bool hasNow = snapshot->variableValues.contains(key);
        const QString was = hadBefore ? previous->variableValues.value(key) : missing;
        const QString now = hasNow ? snapshot->variableValues.value(key) : missing;
        if (hadBefore && hasNow && was == now)
            continue;
        HistoryChange change;
        change.path = key;
        change.oldValue = was;
        change.newValue = now;
        change.hadBefore = hadBefore;
        change.hasNow = hasNow;
        result.changes.append(change);
    }
    return result;
}

void HistoryPointModel::setSession(HistorySession *session)
{
    beginResetModel();
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    m_selectedRow = -1;
    if (m_session) {
        connect(m_session, &HistorySession::historyChanged,
                this, &HistoryPointModel::refreshAll);
        connect(m_session, &HistorySession::eventsAppended,
                this, &HistoryPointModel::refreshAll);
        connect(m_session, &HistorySession::selectedEventChanged,
                this, &HistoryPointModel::refreshSelection);
        connect(m_session, &HistorySession::currentTimeChanged,
                this, &HistoryPointModel::refreshSelection);
    }
    endResetModel();
    refreshSelection();
}

int HistoryPointModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_session)
        return 0;
    // Row 0 is the present; history points follow newest-first.
    return int(m_session->historyPoints().size()) + 1;
}

TraceEventId HistoryPointModel::pointIdAtRow(int row) const
{
    if (!m_session || row <= 0)
        return InvalidTraceEventId;
    const auto &points = m_session->historyPoints();
    const size_t index = points.size() - size_t(row);
    if (index >= points.size())
        return InvalidTraceEventId;
    return points[index].id;
}

int HistoryPointModel::rowForPoint(TraceEventId id) const
{
    if (!m_session || id == InvalidTraceEventId)
        return -1;
    const auto &points = m_session->historyPoints();
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].id == id)
            return int(points.size() - i);
    }
    return -1;
}

QVariant HistoryPointModel::data(const QModelIndex &index, int role) const
{
    if (!m_session || !index.isValid())
        return {};
    const int row = index.row();
    if (row < 0 || row >= rowCount())
        return {};

    const bool isLive = m_session->isLive();
    if (row == 0) {
        switch (role) {
        case Qt::DisplayRole:
            return modelTr("NOW — live target state");
        case SubtitleRole:
            return modelTr("Following the present");
        case IsPresentRole:
            return true;
        case IsSelectedRole:
            return isLive;
        case IsCheckpointRole:
            return false;
        case HasSnapshotRole:
            return false;
        case PointIdRole:
            return QVariant::fromValue<quint64>(InvalidTraceEventId);
        case SequenceRole:
            return -1;
        }
        return {};
    }

    const TraceEventId id = pointIdAtRow(row);
    const ExecutionHistoryPoint *point = m_session->historyPoint(id);
    if (!point)
        return {};
    const bool selected = !isLive && m_session->selectedEventId() == id;

    switch (role) {
    case Qt::DisplayRole: {
        QString main = QStringLiteral("#%1").arg(point->id);
        if (point->location && !point->location->function.isEmpty())
            main += QStringLiteral("  %1()").arg(point->location->function);
        else if (const TraceEvent *event = m_session->eventById(id))
            main += QStringLiteral("  %1").arg(event->label());
        return main;
    }
    case LocationRole:
        if (point->location && point->location->isValid())
            return QStringLiteral("%1:%2")
                .arg(QFileInfo(point->location->file).fileName())
                .arg(point->location->line);
        return modelTr("—");
    case FunctionRole:
        return point->location ? point->location->function : QString();
    case ThreadRole: {
        if (!point->threadId.isEmpty())
            return point->threadId;
        if (const auto snapshot = m_session->snapshotForEvent(id))
            return snapshot->threadId.isEmpty() ? modelTr("—") : snapshot->threadId;
        return modelTr("—");
    }
    case TypeRole: {
        QString type = historyPointTypeName(point->type);
        if (const TraceEvent *event = m_session->eventById(id))
            type += QStringLiteral(" · %1").arg(traceEventTypeName(event->type));
        return type;
    }
    case TimeRole:
        return QVariant::fromValue<qint64>(point->timestamp);
    case SubtitleRole: {
        QStringList parts;
        if (point->location && point->location->isValid())
            parts << QStringLiteral("%1:%2")
                         .arg(QFileInfo(point->location->file).fileName())
                         .arg(point->location->line);
        QString thread = point->threadId;
        if (thread.isEmpty()) {
            if (const auto snapshot = m_session->snapshotForEvent(id))
                thread = snapshot->threadId;
        }
        if (!thread.isEmpty())
            parts << thread;
        parts << historyPointTypeName(point->type);
        const TimeRange full = m_session->fullRange();
        if (full.isValid()) {
            const double relMs = (point->timestamp - full.end) / 1e6;
            parts << (relMs >= -0.0005 ? modelTr("now")
                                       : modelTr("%1 ms ago").arg(-relMs, 0, 'f', 0));
        }
        if (!point->snapshotStep)
            parts << modelTr("no snapshot");
        return parts.join(QStringLiteral(" · "));
    }
    case PointIdRole:
        return QVariant::fromValue<quint64>(id);
    case SequenceRole:
        return QVariant::fromValue<quint64>(point->sequence);
    case IsPresentRole:
        return false;
    case IsSelectedRole:
        return selected;
    case IsCheckpointRole:
        return point->type == HistoryPointType::Checkpoint;
    case HasSnapshotRole:
        return point->snapshotStep.has_value();
    case Qt::ToolTipRole:
        if (point->location && point->location->isValid())
            return QStringLiteral("%1:%2")
                .arg(point->location->file).arg(point->location->line);
        return {};
    }
    return {};
}

void HistoryPointModel::refreshAll()
{
    beginResetModel();
    endResetModel();
    refreshSelection();
}

void HistoryPointModel::refreshSelection()
{
    if (!m_session)
        return;
    const int selected = m_session->isLive() ? 0 : rowForPoint(m_session->selectedEventId());
    if (selected == m_selectedRow)
        return;
    const int previous = m_selectedRow;
    m_selectedRow = selected;
    const int last = rowCount() - 1;
    if (previous >= 0 && previous <= last)
        emit dataChanged(index(previous), index(previous), {IsSelectedRole});
    if (selected >= 0 && selected <= last)
        emit dataChanged(index(selected), index(selected), {IsSelectedRole});
}

TimeTravelController::TimeTravelController(HistorySession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    qRegisterMetaType<TimeTravelPosition>();
    if (m_session) {
        connect(m_session, &HistorySession::selectedEventChanged,
                this, [this] { refresh(); });
        connect(m_session, &HistorySession::currentTimeChanged,
                this, [this] { refresh(); });
        connect(m_session, &HistorySession::capabilitiesChanged,
                this, [this] { refresh(); });
    }
    refresh();
}

TimeTravelPosition TimeTravelController::positionFor(const HistorySession *session)
{
    TimeTravelPosition position;
    if (!session || session->isLive())
        return position;
    position.id = session->selectedEventId();
    position.type = session->capabilities().testFlag(HistoryCapability::Seek)
        ? TimeTravelPosition::Type::RecordedPosition
        : TimeTravelPosition::Type::Snapshot;
    return position;
}

TimeTravelPosition TimeTravelController::position() const
{
    return positionFor(m_session);
}

void TimeTravelController::refresh()
{
    const TimeTravelPosition next = position();
    if (next == m_last)
        return;
    m_last = next;
    emit positionChanged(m_last);
}

void TimeTravelController::showLive()
{
    if (m_session)
        m_session->goLive();
}

void TimeTravelController::showSnapshot(TraceEventId id)
{
    if (m_session)
        m_session->selectEvent(id);
}

void TimeTravelController::showRecordedPosition(TraceEventId id)
{
    if (m_session)
        m_session->seekToHistoryPoint(id);
}

void TimeTravelController::showPrevious()
{
    if (m_session)
        m_session->selectPreviousEvent();
}

void TimeTravelController::showNext()
{
    if (m_session)
        m_session->selectNextEvent();
}

void TimeTravelController::showPresent()
{
    if (m_session)
        m_session->returnToPresent();
}

TraceEventId findPreviousChange(HistorySession *session,
                                const QString &expression)
{
    if (!session || expression.isEmpty() || session->storedEvents().empty())
        return InvalidTraceEventId;
    const TraceEventId current =
        session->isLive() ? session->storedEvents().back().id
                          : session->selectedEventId();
    const auto anchor = session->snapshotForEvent(current);
    if (!anchor)
        return InvalidTraceEventId;
    const QString currentValue = anchor->variableValues.value(expression);
    const bool currentHas = anchor->variableValues.contains(expression);
    const auto &events = session->storedEvents();
    for (auto it = events.begin(); it != events.end(); ++it) {
        if (it->id != current)
            continue;
        // Walk backward over earlier stops; the first stop whose value
        // differs is where the change happened between snapshots.
        for (auto back = std::make_reverse_iterator(it);
             back != events.rend(); ++back) {
            if (back->id == current)
                continue;
            const auto candidate = session->snapshotForEvent(back->id);
            if (!candidate)
                continue;
            const bool hadBefore = candidate->variableValues.contains(expression);
            if (hadBefore != currentHas
                || candidate->variableValues.value(expression) != currentValue)
                return back->id;
        }
        return InvalidTraceEventId;
    }
    // Current position unknown: fall back to the latest differing stop.
    for (auto back = events.rbegin(); back != events.rend(); ++back) {
        const auto candidate = session->snapshotForEvent(back->id);
        if (!candidate)
            continue;
        const bool hadBefore = candidate->variableValues.contains(expression);
        if (hadBefore != currentHas
            || candidate->variableValues.value(expression) != currentValue)
            return back->id;
    }
    return InvalidTraceEventId;
}

TraceEventId TimeTravelController::findPreviousChange(const QString &expression) const
{
    const TraceEventId id = qddd::history::findPreviousChange(m_session, expression);
    // Inspect-only: finding a change never moves the target.
    if (id != InvalidTraceEventId && m_session)
        m_session->selectEvent(id);
    return id;
}

} // namespace history
} // namespace qddd
