#include "HistorySession.h"

#include <algorithm>
#include <iterator>

namespace qddd {
namespace history {

HistorySession::HistorySession(QObject *parent)
    : QObject(parent)
{
}

HistorySession::~HistorySession()
{
	if (!m_backend)
		return;
	m_backend->setCapabilitiesChangedHandler({});
	m_backend->setRecordingChangedHandler({});
	m_backend->setOperationFinishedHandler({});
}

void HistorySession::setBackend(HistoryBackend *backend)
{
	if (m_backend) {
		m_backend->setCapabilitiesChangedHandler({});
		m_backend->setRecordingChangedHandler({});
		m_backend->setOperationFinishedHandler({});
	}
    m_backend = backend;
	if (m_backend) {
		m_backend->setCapabilitiesChangedHandler([this] { refreshBackendState(); });
		m_backend->setRecordingChangedHandler([this](bool active) { setRecording(active); });
		m_backend->setOperationFinishedHandler(
		    [this](const QString &name, bool ok, const QString &message) {
			    emit operationFinished(name, ok, message);
		    });
	}
	refreshBackendState();
}

void HistorySession::refreshBackendState()
{
    const HistoryCapabilities caps = capabilities();
    const bool available = isAvailable();
    if (caps != m_lastCapabilities) {
        m_lastCapabilities = caps;
        emit capabilitiesChanged(caps);
    }
    if (available != m_lastAvailable) {
        m_lastAvailable = available;
        emit historyAvailabilityChanged(available);
    }
}

HistoryCapabilities HistorySession::capabilities() const
{
    if (!m_backend)
        return {};
    return m_backend->capabilities();
}

bool HistorySession::isAvailable() const
{
    return m_backend && m_backend->isAvailable();
}

void HistorySession::addEvent(TraceEvent event)
{
    if (event.id == InvalidTraceEventId)
        event.id = m_nextId++;
    else
        m_nextId = std::max(m_nextId, TraceEventId(event.id + 1));
    if (event.time == InvalidTime)
        return; // Undated events cannot be placed on the timeline.
    m_events.push_back(std::move(event));
    std::sort(m_events.begin(), m_events.end());
	rebuildPoints();
    if (m_live)
        m_currentTime = m_events.back().time;
    emit eventsAppended(1);
    if (m_live)
        emit currentTimeChanged(m_currentTime);
}

void HistorySession::addEvents(const std::vector<TraceEvent> &events)
{
    int appended = 0;
    for (auto event : events) {
        if (event.id == InvalidTraceEventId)
            event.id = m_nextId++;
        else
            m_nextId = std::max(m_nextId, TraceEventId(event.id + 1));
        if (event.time == InvalidTime)
            continue;
        m_events.push_back(std::move(event));
        ++appended;
    }
    if (appended == 0)
        return;
    std::sort(m_events.begin(), m_events.end());
	rebuildPoints();
    if (m_live)
        m_currentTime = m_events.back().time;
    emit eventsAppended(appended);
    if (m_live)
        emit currentTimeChanged(m_currentTime);
}

void HistorySession::clear()
{
    m_events.clear();
	m_points.clear();
	m_checkpoints.clear();
    m_currentTime = InvalidTime;
    m_selectedId = InvalidTraceEventId;
    m_live = true;
    emit eventsAppended(0);
    emit selectedEventChanged(m_selectedId);
    emit currentTimeChanged(m_currentTime);
	emit historyChanged();
}

void HistorySession::rebuildPoints()
{
	m_points.clear();
	m_points.reserve(m_events.size());
	quint64 sequence = 0;
	for (const TraceEvent &event : m_events) {
		ExecutionHistoryPoint point;
		point.id = event.id;
		point.sequence = sequence++;
		point.timestamp = event.time;
		point.location = event.source;
		point.programCounter = event.address;
		point.snapshotStep = traceSnapshotStep(event);
		point.temporalState = (&event == &m_events.back())
		    ? TemporalState::Live : TemporalState::Historic;
		point.type = m_checkpoints.contains(event.id)
		    ? HistoryPointType::Checkpoint : HistoryPointType::Stop;
		m_points.push_back(std::move(point));
	}
	emit historyChanged();
}

const ExecutionHistoryPoint *HistorySession::historyPoint(TraceEventId id) const
{
	for (const auto &point : m_points)
		if (point.id == id)
			return &point;
	return nullptr;
}

std::optional<Checkpoint> HistorySession::checkpoint(TraceEventId id) const
{
	const auto it = m_checkpoints.constFind(id);
	return it == m_checkpoints.constEnd() ? std::nullopt
	                                    : std::optional<Checkpoint>(it.value());
}

bool HistorySession::createCheckpoint(TraceEventId id)
{
	const ExecutionHistoryPoint *point = historyPoint(id);
	if (!point || !capabilities().testFlag(HistoryCapability::Checkpoints))
		return false;
	Checkpoint value;
	value.historyPointId = id;
	value.snapshotStep = point->snapshotStep;
	if (m_backend && !m_backend->createCheckpoint(*point, value) && !value.snapshotStep)
		return false;
	m_checkpoints.insert(id, value);
	rebuildPoints();
	return true;
}

std::vector<TraceEvent> HistorySession::events(TimeRange range) const
{
    std::vector<TraceEvent> out;
    if (!range.isValid())
        return out;
    for (const TraceEvent &event : m_events) {
        if (range.contains(event.time))
            out.push_back(event);
    }
    return out;
}

std::vector<TraceEvent> HistorySession::events(TimeRange range, const QString &trackId) const
{
    std::vector<TraceEvent> out;
    if (!range.isValid())
        return out;
    for (const TraceEvent &event : m_events) {
        if (event.trackId == trackId && range.contains(event.time))
            out.push_back(event);
    }
    return out;
}

const TraceEvent *HistorySession::eventById(TraceEventId id) const
{
    if (id == InvalidTraceEventId)
        return nullptr;
    for (const TraceEvent &event : m_events) {
        if (event.id == id)
            return &event;
    }
    return nullptr;
}

void HistorySession::setSnapshotResolver(SnapshotResolver resolver)
{
    m_snapshotResolver = std::move(resolver);
}

std::optional<ExecutionSnapshot> HistorySession::snapshotForEvent(TraceEventId id) const
{
    const TraceEvent *event = eventById(id);
    if (!event || !m_snapshotResolver)
        return std::nullopt;
    const std::optional<int> step = traceSnapshotStep(*event);
    if (!step)
        return std::nullopt;
    return m_snapshotResolver(*step);
}

TimeRange HistorySession::fullRange() const
{
    if (m_events.empty())
        return {};
    return {m_events.front().time, m_events.back().time};
}

bool HistorySession::seek(TimePoint time)
{
    if (m_events.empty() || time == InvalidTime)
        return false;
    // Clamp to the recorded range: seeking outside history is meaningless.
    const TimeRange range = fullRange();
    const TimePoint clamped = std::max(range.begin, std::min(range.end, time));
    m_live = (clamped == range.end);
    m_currentTime = clamped;
    emit currentTimeChanged(m_currentTime);

    // Select the nearest event at or before the cursor for details display.
    TraceEventId nearest = InvalidTraceEventId;
    for (const TraceEvent &event : m_events) {
        if (event.time <= clamped)
            nearest = event.id;
        else
            break;
    }
    if (nearest != m_selectedId) {
        m_selectedId = nearest;
        emit selectedEventChanged(m_selectedId);
    }

    if (m_backend && m_backend->capabilities().testFlag(HistoryCapability::Seek)) {
        if (m_backend->seek(clamped)) {
            emitStateForTime(clamped);
            return true;
        }
        return false;
    }
    // No seek-capable backend: cursor/selection move is the whole behavior
    // (source preview follows the selected event; live state is untouched).
    emitStateForTime(clamped);
    return true;
}

void HistorySession::goLive()
{
    if (m_events.empty()) {
        m_live = true;
        return;
    }
    m_live = true;
    m_currentTime = m_events.back().time;
    const TraceEventId latestId = m_events.back().id;
    if (m_selectedId != latestId) {
        m_selectedId = latestId;
        emit selectedEventChanged(m_selectedId);
    }
    emit currentTimeChanged(m_currentTime);
    emitStateForTime(m_currentTime);
	emit temporalStateChanged(TemporalState::Live);
}

bool HistorySession::seekToHistoryPoint(TraceEventId id)
{
	const ExecutionHistoryPoint *point = historyPoint(id);
	if (!point)
		return false;
	if (id == m_events.back().id) {
		goLive();
		return true;
	}
	if (m_backend && capabilities().testFlag(HistoryCapability::Seek) &&
	    !m_backend->seekToHistoryPoint(*point))
		return false;
	selectEvent(id);
	return true;
}

bool HistorySession::returnToPresent()
{
	if (m_backend && capabilities().testFlag(HistoryCapability::Seek) &&
	    !isLive())
		return m_backend->returnToPresent();
	goLive();
	return true;
}

bool HistorySession::startRecording()
{
	return m_backend && capabilities().testFlag(HistoryCapability::RecordReplay)
	    && m_backend->startRecording();
}

bool HistorySession::stopRecording()
{
	return m_backend && m_recording && m_backend->stopRecording();
}

bool HistorySession::reverseStep()
{
	return m_backend && capabilities().testFlag(HistoryCapability::ReverseStep)
	    && m_backend->stepBackward();
}

bool HistorySession::reverseNext()
{
	return m_backend && capabilities().testFlag(HistoryCapability::ReverseNext)
	    && m_backend->reverseNext();
}

bool HistorySession::reverseContinue()
{
	return m_backend && capabilities().testFlag(HistoryCapability::ReverseContinue)
	    && m_backend->continueBackward();
}

bool HistorySession::reverseFinish()
{
	return m_backend && capabilities().testFlag(HistoryCapability::ReverseFinish)
	    && m_backend->reverseFinish();
}

bool HistorySession::canSelectPreviousEvent() const
{
    if (m_events.empty())
        return false;
    if (m_live || m_selectedId == InvalidTraceEventId)
        return m_events.size() > 1;
    const auto it = std::find_if(m_events.begin(), m_events.end(), [this](const TraceEvent &event) {
        return event.id == m_selectedId;
    });
    return it != m_events.end() && it != m_events.begin();
}

bool HistorySession::canSelectNextEvent() const
{
    if (m_events.empty() || m_live)
        return false;
    const auto it = std::find_if(m_events.begin(), m_events.end(), [this](const TraceEvent &event) {
        return event.id == m_selectedId;
    });
    return it != m_events.end() && std::next(it) != m_events.end();
}

bool HistorySession::selectPreviousEvent()
{
    if (!canSelectPreviousEvent())
        return false;
    if (m_live || m_selectedId == InvalidTraceEventId) {
        selectEvent(m_events[m_events.size() - 2].id);
        return true;
    }
    const auto it = std::find_if(m_events.begin(), m_events.end(), [this](const TraceEvent &event) {
        return event.id == m_selectedId;
    });
    selectEvent(std::prev(it)->id);
    return true;
}

bool HistorySession::selectNextEvent()
{
    if (!canSelectNextEvent())
        return false;
    const auto it = std::find_if(m_events.begin(), m_events.end(), [this](const TraceEvent &event) {
        return event.id == m_selectedId;
    });
    if (std::next(it) == std::prev(m_events.end()))
        goLive();
    else
        selectEvent(std::next(it)->id);
    return true;
}

void HistorySession::selectEvent(TraceEventId id)
{
    const TraceEvent *event = eventById(id);
    if (!event)
        return;
    m_selectedId = id;
    m_currentTime = event->time;
    m_live = (event->time == fullRange().end);
    emit selectedEventChanged(m_selectedId);
	emit historyPointSelected(m_selectedId);
    emit currentTimeChanged(m_currentTime);
    emitStateForTime(m_currentTime);
	emit temporalStateChanged(m_live ? TemporalState::Live : TemporalState::Historic);
}

void HistorySession::setRecording(bool recording)
{
    if (m_recording == recording)
        return;
    m_recording = recording;
    emit recordingChanged(m_recording);
}

void HistorySession::emitStateForTime(TimePoint time)
{
    if (m_backend) {
        if (auto state = m_backend->currentState()) {
            ExecutionState preview = *state;
            preview.time = time;
            emit executionStateChanged(preview);
            return;
        }
    }
    // Fallback: synthesize a cursor-only state from the selected event so
    // views can show source/PC context without a seek-capable backend.
    ExecutionState state;
    state.time = time;
    if (const TraceEvent *event = selectedEvent()) {
        if (event->address) {
            state.hasPc = true;
            state.pc = *event->address;
        }
        state.sourceLocation = event->source;
    }
    emit executionStateChanged(state);
}

} // namespace history
} // namespace qddd
