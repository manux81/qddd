#pragma once

// Abstract execution-history backend.
//
// A backend exposes whatever history the debugger/emulator interface can
// actually provide. Backends must NOT fake unsupported semantics: operations
// without backend support return false / nullopt, and the UI disables them
// via HistoryCapabilities.
//
// Future deterministic QEMU record/replay support connects here as a new
// QemuHistoryBackend subclass without touching the GUI.

#include "HistoryCapabilities.h"
#include "HistoryTypes.h"

#include <optional>
#include <vector>
#include <functional>

namespace qddd {
namespace history {

class HistoryBackend {
public:
    virtual ~HistoryBackend() = default;

    virtual bool isAvailable() const = 0;
    virtual HistoryCapabilities capabilities() const = 0;

    virtual std::optional<ExecutionState> currentState() = 0;

    // Restore the historical state at `time`. Returns false when the backend
    // cannot seek (capability flag Seek unset).
    virtual bool seek(TimePoint time) = 0;

    virtual bool stepForward() = 0;
    virtual bool stepBackward() = 0;

    virtual bool continueForward() = 0;
    virtual bool continueBackward() = 0;

    virtual std::vector<TraceEvent> events(TimeRange range) = 0;

    // Asynchronous implementations return true when an operation was
    // accepted. Completion is reported by their owning adapter/session.
    virtual bool startRecording() { return false; }
    virtual bool stopRecording() { return false; }
    virtual bool createCheckpoint(const ExecutionHistoryPoint &, Checkpoint &) { return false; }
    virtual bool restoreCheckpoint(const Checkpoint &) { return false; }
    virtual bool reverseNext() { return false; }
    virtual bool reverseFinish() { return false; }
    virtual bool returnToPresent() { return continueForward(); }

    // Arbitrary point seeking is semantic: a backend may directly move its
    // recorder or later restore a checkpoint and replay forward.
    virtual bool seekToHistoryPoint(const ExecutionHistoryPoint &point)
    {
        return point.timestamp != InvalidTime && seek(point.timestamp);
    }

    // Memory-write history. Default: unsupported. Backends with
    // MemoryWriteHistory implement these; the "Find Previous Write" UX
    // enables itself only then.
    virtual std::optional<MemoryAccessEvent> findPreviousWrite(quint64 /*address*/,
                                                               TimePoint /*before*/)
    {
        return std::nullopt;
    }

    virtual std::optional<MemoryAccessEvent> findNextWrite(quint64 /*address*/,
                                                           TimePoint /*after*/)
    {
        return std::nullopt;
    }

    virtual std::vector<MemoryAccessEvent> valueHistory(quint64 /*address*/,
                                                        TimeRange /*range*/)
    {
        return {};
    }

    // Optional user annotation support.
    virtual bool recordMarker(const QString & /*label*/) { return false; }

    using CapabilitiesChangedHandler = std::function<void()>;
    using RecordingChangedHandler = std::function<void(bool)>;
    using OperationFinishedHandler = std::function<void(const QString &, bool, const QString &)>;
    void setCapabilitiesChangedHandler(CapabilitiesChangedHandler handler)
    { m_capabilitiesChanged = std::move(handler); }
    void setRecordingChangedHandler(RecordingChangedHandler handler)
    { m_recordingChanged = std::move(handler); }
    void setOperationFinishedHandler(OperationFinishedHandler handler)
    { m_operationFinished = std::move(handler); }

protected:
    void notifyCapabilitiesChanged() { if (m_capabilitiesChanged) m_capabilitiesChanged(); }
    void notifyRecordingChanged(bool active) { if (m_recordingChanged) m_recordingChanged(active); }
    void notifyOperationFinished(const QString &name, bool ok, const QString &message = {})
    { if (m_operationFinished) m_operationFinished(name, ok, message); }

private:
    CapabilitiesChangedHandler m_capabilitiesChanged;
    RecordingChangedHandler m_recordingChanged;
    OperationFinishedHandler m_operationFinished;
};

} // namespace history
} // namespace qddd
