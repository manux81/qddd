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
};

} // namespace history
} // namespace qddd
