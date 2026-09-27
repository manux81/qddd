#include "HistorySession.h"

#include <algorithm>

namespace qddd {
namespace history {

HistorySession::HistorySession(QObject *parent)
    : QObject(parent)
{
}

void HistorySession::setBackend(HistoryBackend *backend)
{
    m_backend = backend;
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
    if (m_live)
        m_currentTime = m_events.back().time;
    emit eventsAppended(appended);
    if (m_live)
        emit currentTimeChanged(m_currentTime);
}

void HistorySession::clear()
{
    m_events.clear();
    m_currentTime = InvalidTime;
    m_selectedId = InvalidTraceEventId;
    m_live = true;
    emit eventsAppended(0);
    emit selectedEventChanged(m_selectedId);
    emit currentTimeChanged(m_currentTime);
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
    emit currentTimeChanged(m_currentTime);
    emitStateForTime(m_currentTime);
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
    emit currentTimeChanged(m_currentTime);
    emitStateForTime(m_currentTime);
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
