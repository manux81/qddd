#include "GdbRecordTimeMachineBackend.h"

#include "DebugSession.h"

namespace qddd {
namespace history {

GdbRecordTimeMachineBackend::GdbRecordTimeMachineBackend(DebuggerSession *session,
                                                         QObject *parent)
    : GdbStopHistoryBackend(session, parent)
{
    if (!session)
        return;
    connect(session, &DebuggerSession::reverseExecutionAvailabilityChanged,
            this, [this] { notifyCapabilitiesChanged(); emit backendChanged(); });
    connect(session, &DebuggerSession::timeMachineRecordingStateChanged,
            this, [this](bool active) {
                notifyRecordingChanged(active);
                notifyCapabilitiesChanged();
                emit backendChanged();
            });
    connect(session, &DebuggerSession::timeMachineOperationFinished,
            this, [this](const QString &operation, bool success, const QString &message) {
                notifyOperationFinished(operation, success, message);
            });
}

HistoryCapabilities GdbRecordTimeMachineBackend::capabilities() const
{
    HistoryCapabilities caps = HistoryCapability::Recording | HistoryCapability::Checkpoints;
    DebuggerSession *session = debuggerSession();
    if (!session || !session->timeMachineRecordingSupported())
        return caps;
    caps |= HistoryCapability::RecordReplay;
    if (session->supportsReverseExecution()) {
        caps |= HistoryCapability::ReverseStep | HistoryCapability::ReverseNext
             | HistoryCapability::ReverseContinue | HistoryCapability::ReverseFinish;
    }
    return caps;
}

bool GdbRecordTimeMachineBackend::startRecording()
{
    return debuggerSession() && debuggerSession()->startTimeMachineRecording();
}

bool GdbRecordTimeMachineBackend::stopRecording()
{
    return debuggerSession() && debuggerSession()->stopTimeMachineRecording();
}

bool GdbRecordTimeMachineBackend::createCheckpoint(const ExecutionHistoryPoint &point,
                                                   Checkpoint &checkpoint)
{
    checkpoint.historyPointId = point.id;
    checkpoint.snapshotStep = point.snapshotStep;
    return checkpoint.snapshotStep.has_value();
}

bool GdbRecordTimeMachineBackend::stepBackward()
{
    if (!debuggerSession() || !debuggerSession()->supportsReverseExecution())
        return false;
    debuggerSession()->reverseStepInto();
    return true;
}

bool GdbRecordTimeMachineBackend::continueBackward()
{
    if (!debuggerSession() || !debuggerSession()->supportsReverseExecution())
        return false;
    debuggerSession()->reverseContinueExecution();
    return true;
}

bool GdbRecordTimeMachineBackend::reverseNext()
{
    if (!debuggerSession() || !debuggerSession()->supportsReverseExecution())
        return false;
    debuggerSession()->reverseStepOver();
    return true;
}

bool GdbRecordTimeMachineBackend::reverseFinish()
{
    return debuggerSession() && debuggerSession()->reverseFinishExecution();
}

bool GdbRecordTimeMachineBackend::returnToPresent()
{
    return debuggerSession() && debuggerSession()->returnToPresentExecution();
}

} // namespace history
} // namespace qddd
