#pragma once

// stop-recording backend: the best history QDDD can build with the existing
// GDB/MI stop stream, without any QEMU changes.
//
// SUPPORTED NOW:
//   - Recording stop-level TraceEvents (breakpoint hit, step, signal stop).
//   - currentState() snapshot (time, PC when known, source location).
//   - events(range) queries over recorded stops.
//
// REQUIRES FUTURE BACKEND SUPPORT (cleanly disabled, never faked):
//   - Seek, ReverseStep, ReverseContinue, DeterministicReplay
//   - InstructionHistory, MemoryWriteHistory, InterruptEvents,
//     PeripheralEvents.

#include "HistoryBackend.h"

#include <QObject>

class DebuggerSession;

namespace qddd {
namespace history {

class GdbStopHistoryBackend : public QObject, public HistoryBackend {
    Q_OBJECT
public:
    explicit GdbStopHistoryBackend(DebuggerSession *session, QObject *parent = nullptr);

    bool isAvailable() const override;
    HistoryCapabilities capabilities() const override;

    std::optional<ExecutionState> currentState() override;

    bool seek(TimePoint time) override;
    bool stepForward() override;
    bool stepBackward() override;
    bool continueForward() override;
    bool continueBackward() override;

    std::vector<TraceEvent> events(TimeRange range) override;

    // Feed the shared HistorySession store. The session owns storage; this
    // backend only translates live stops into TraceEvents.
    void attachStore(std::vector<TraceEvent> *store);

signals:
    void stopRecorded(const qddd::history::TraceEvent &event);
    void backendChanged();

private:
    void onStoppedAt(const QString &file, int line, const QString &function);
    void onStoppedAtAddress(const QString &address);
    void onTargetStarted();
    void onTargetExited();

    static TimePoint nowNs();

    DebuggerSession *m_session = nullptr; // not owned
    std::vector<TraceEvent> *m_store = nullptr; // not owned
    TraceEventId m_nextId = 1;

    QString m_lastAddress;
    bool m_sessionActive = false;
};

} // namespace history
} // namespace qddd
