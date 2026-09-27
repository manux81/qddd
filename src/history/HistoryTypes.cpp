#include "HistoryTypes.h"

namespace qddd {
namespace history {

QString traceEventTypeName(TraceEventType type)
{
    switch (type) {
    case TraceEventType::Instruction: return QStringLiteral("Instruction");
    case TraceEventType::FunctionEnter: return QStringLiteral("Function enter");
    case TraceEventType::FunctionExit: return QStringLiteral("Function exit");
    case TraceEventType::Breakpoint: return QStringLiteral("Breakpoint");
    case TraceEventType::Watchpoint: return QStringLiteral("Watchpoint");
    case TraceEventType::InterruptEnter: return QStringLiteral("Interrupt enter");
    case TraceEventType::InterruptExit: return QStringLiteral("Interrupt exit");
    case TraceEventType::Exception: return QStringLiteral("Exception");
    case TraceEventType::MemoryRead: return QStringLiteral("Memory read");
    case TraceEventType::MemoryWrite: return QStringLiteral("Memory write");
    case TraceEventType::Peripheral: return QStringLiteral("Peripheral");
    case TraceEventType::UserMarker: return QStringLiteral("User marker");
    case TraceEventType::Stop: return QStringLiteral("Stop");
    case TraceEventType::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString TraceEvent::label() const
{
    if (source && !source->function.isEmpty())
        return source->function;
    const QString typed = traceEventTypeName(type);
    if (address)
        return QStringLiteral("%1 @ 0x%2").arg(typed).arg(*address, 0, 16);
    return typed;
}

} // namespace history
} // namespace qddd
