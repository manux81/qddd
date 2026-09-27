#pragma once

// GUI-independent execution-history / session model.
//
//   DebugSession (live debugger, owns GDB/QEMU transport)
//     |
//   HistorySession (owns backend, timeline events, time cursor, selection)
//
// Widgets talk to HistorySession only, never to GDB/QEMU objects.

#include "HistoryBackend.h"
#include "HistoryTypes.h"

#include "DebugSession.h"

#include <QObject>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace qddd {
namespace history {

class HistorySession : public QObject {
    Q_OBJECT
public:
    explicit HistorySession(QObject *parent = nullptr);

    void setBackend(HistoryBackend *backend); // not owned
    HistoryBackend *backend() const { return m_backend; }
    HistoryCapabilities capabilities() const;

    // Event store. Fed by backend adapters (e.g. GdbStopHistoryBackend).
    void addEvent(TraceEvent event);
    void addEvents(const std::vector<TraceEvent> &events);
    void clear();
    const std::vector<TraceEvent> &storedEvents() const { return m_events; }
    // Adapter hook for co-located backends (e.g. GdbStopHistoryBackend::
    // attachStore). Regular consumers use addEvent()/events().
    std::vector<TraceEvent> &eventStore() { return m_events; }

    std::vector<TraceEvent> events(TimeRange range) const;
    std::vector<TraceEvent> events(TimeRange range, const QString &trackId) const;
    const TraceEvent *eventById(TraceEventId id) const;

    // Snapshot resolution. The session stores stop-level TraceEvents; the
    // authoritative per-stop variable state lives in the debugger's
    // ExecutionSnapshot list and is looked up by stable stepIndex (see
    // SnapshotStepKey), never by shifting vector position and never by
    // duplicating values into the event. The resolver is injected so this
    // model stays independent of any concrete debugger implementation.
    using SnapshotResolver = std::function<std::optional<ExecutionSnapshot>(int stepIndex)>;
    void setSnapshotResolver(SnapshotResolver resolver);
    std::optional<ExecutionSnapshot> snapshotForEvent(TraceEventId id) const;

    TimeRange fullRange() const;

    // Time cursor. seek() asks the backend to restore state when it supports
    // Seek; otherwise it moves the cursor/selection only (navigation preview).
    bool seek(TimePoint time);
    TimePoint currentTime() const { return m_currentTime; }
    bool isLive() const { return m_live; }
    void goLive();

    void selectEvent(TraceEventId id);
    TraceEventId selectedEventId() const { return m_selectedId; }
    const TraceEvent *selectedEvent() const { return eventById(m_selectedId); }

    bool isAvailable() const;
    bool isRecording() const { return m_recording; }
    void setRecording(bool recording);

signals:
    void currentTimeChanged(qddd::history::TimePoint time);
    void executionStateChanged(const qddd::history::ExecutionState &state);
    void selectedEventChanged(qddd::history::TraceEventId id);
    void historyAvailabilityChanged(bool available);
    void recordingChanged(bool recording);
    void capabilitiesChanged(qddd::history::HistoryCapabilities capabilities);
    void eventsAppended(int count);

private:
    void emitStateForTime(TimePoint time);

    HistoryBackend *m_backend = nullptr; // not owned
    SnapshotResolver m_snapshotResolver;
    HistoryCapabilities m_lastCapabilities{};
    bool m_lastAvailable = false;

    std::vector<TraceEvent> m_events;
    TraceEventId m_nextId = 1;

    TimePoint m_currentTime = InvalidTime;
    TraceEventId m_selectedId = InvalidTraceEventId;
    bool m_live = true;
    bool m_recording = false;
};

} // namespace history
} // namespace qddd
