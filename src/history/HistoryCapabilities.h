#pragma once

// Capability flags for HistoryBackend implementations.
//
// The UI must query these instead of assuming functionality exists.
// Unsupported operations are disabled in the UX, never simulated.

#include <QFlags>
#include <QStringList>

namespace qddd {
namespace history {

enum class HistoryCapability {
    Recording            = 0x01, // Backend appends events while the target runs.
    Seek                 = 0x02, // seek(time) restores a historical state.
    ReverseStep          = 0x04, // stepForward/stepBackward move the target.
    ReverseContinue      = 0x08, // continueForward/continueBackward work.
    InstructionHistory   = 0x10, // Per-instruction trace events available.
    MemoryWriteHistory   = 0x20, // findPreviousWrite() is implemented.
    InterruptEvents      = 0x40, // Interrupt enter/exit events available.
    PeripheralEvents     = 0x80, // Peripheral events available.
    DeterministicReplay  = 0x100, // Seeks/steps are bit-exact, not best-effort.
    Checkpoints          = 0x200, // Stop snapshots can be retained as checkpoints.
    ReverseNext          = 0x400,
    ReverseFinish        = 0x800,
    RecordReplay         = 0x1000 // Backend owns an execution recorder/replayer.
};
Q_DECLARE_FLAGS(HistoryCapabilities, HistoryCapability)

inline QStringList historyCapabilityNames(HistoryCapabilities caps)
{
    QStringList names;
    if (caps.testFlag(HistoryCapability::Recording))
        names << QStringLiteral("recording");
    if (caps.testFlag(HistoryCapability::Seek))
        names << QStringLiteral("seek");
    if (caps.testFlag(HistoryCapability::ReverseStep))
        names << QStringLiteral("reverse-step");
    if (caps.testFlag(HistoryCapability::ReverseContinue))
        names << QStringLiteral("reverse-continue");
    if (caps.testFlag(HistoryCapability::InstructionHistory))
        names << QStringLiteral("instruction-history");
    if (caps.testFlag(HistoryCapability::MemoryWriteHistory))
        names << QStringLiteral("memory-write-history");
    if (caps.testFlag(HistoryCapability::InterruptEvents))
        names << QStringLiteral("interrupt-events");
    if (caps.testFlag(HistoryCapability::PeripheralEvents))
        names << QStringLiteral("peripheral-events");
    if (caps.testFlag(HistoryCapability::DeterministicReplay))
        names << QStringLiteral("deterministic-replay");
    if (caps.testFlag(HistoryCapability::Checkpoints))
        names << QStringLiteral("checkpoints");
    if (caps.testFlag(HistoryCapability::ReverseNext))
        names << QStringLiteral("reverse-next");
    if (caps.testFlag(HistoryCapability::ReverseFinish))
        names << QStringLiteral("reverse-finish");
    if (caps.testFlag(HistoryCapability::RecordReplay))
        names << QStringLiteral("record-replay");
    return names;
}

} // namespace history
} // namespace qddd

Q_DECLARE_OPERATORS_FOR_FLAGS(qddd::history::HistoryCapabilities)
