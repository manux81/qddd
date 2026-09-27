#pragma once

#include "GdbStopHistoryBackend.h"

namespace qddd {
namespace history {

// GDB record/full + record/btrace adapter. It preserves stop snapshots from
// GdbStopHistoryBackend and adds recorder/replayer operations. No GDB command
// escapes this class/DebuggerSession boundary.
class GdbRecordTimeMachineBackend final : public GdbStopHistoryBackend {
    Q_OBJECT
public:
    explicit GdbRecordTimeMachineBackend(DebuggerSession *session, QObject *parent = nullptr);

    HistoryCapabilities capabilities() const override;
    bool startRecording() override;
    bool stopRecording() override;
    bool createCheckpoint(const ExecutionHistoryPoint &point, Checkpoint &checkpoint) override;
    bool stepBackward() override;
    bool continueBackward() override;
    bool reverseNext() override;
    bool reverseFinish() override;
    bool returnToPresent() override;
};

} // namespace history
} // namespace qddd
