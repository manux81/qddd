#pragma once

// Generic execution-history value types.
//
// This header is intentionally target-agnostic: it knows CPU, interrupt,
// exception, memory access and peripheral *concepts*, but no specific MCU,
// peripheral map or vector table. Target-specific decoding belongs behind
// TargetDescriptor / PeripheralEventProvider, never in these structs.

#include <QtGlobal>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <optional>
#include <vector>

namespace qddd {
namespace history {

// Monotonic nanosecond timestamp. The current stop-recording adapter uses
// wall-clock time of the stop event; a future deterministic replay backend
// may use a target-cycle-derived clock instead. Consumers must treat it as
// an ordering key, not as an absolute target time.
using TimePoint = qint64;
inline constexpr TimePoint InvalidTime = -1;

using TraceEventId = quint64;
inline constexpr TraceEventId InvalidTraceEventId = 0;

struct TimeRange {
    TimePoint begin = InvalidTime;
    TimePoint end = InvalidTime;

    bool isValid() const { return begin != InvalidTime && end != InvalidTime && begin <= end; }
    bool contains(TimePoint t) const { return isValid() && t >= begin && t <= end; }
};

inline bool operator<(const TimeRange &a, const TimeRange &b)
{
    if (a.begin != b.begin)
        return a.begin < b.begin;
    return a.end < b.end;
}

struct SourceLocation {
    QString file;
    int line = 0;
    QString function;

    bool isValid() const { return !file.isEmpty() && line > 0; }
};

// Generic register set: register name -> value text. Names are backend
// specific ("pc", "sp", "r0", "x0", ...) and never interpreted by the core.
struct RegisterSnapshot {
    QHash<QString, QString> values;

    QString value(const QString &name) const { return values.value(name); }
    bool isEmpty() const { return values.isEmpty(); }
};

struct ExecutionState {
    TimePoint time = InvalidTime;

    bool hasPc = false;
    quint64 pc = 0;
    bool hasSp = false;
    quint64 sp = 0;

    RegisterSnapshot registers;
    std::optional<SourceLocation> sourceLocation;
};

// Keep generic: no peripheral, bus or vendor concepts here.
enum class TraceEventType {
    Instruction,
    FunctionEnter,
    FunctionExit,
    Breakpoint,
    Watchpoint,
    InterruptEnter,
    InterruptExit,
    Exception,
    MemoryRead,
    MemoryWrite,
    Peripheral,
    UserMarker,
    Stop,      // Live-target stop (breakpoint hit, step, signal, ...). The only
               // event kind the current GDB stop-recording backend can produce.
    Unknown
};

QString traceEventTypeName(TraceEventType type);

struct TraceEvent {
    TraceEventId id = InvalidTraceEventId;
    TimePoint time = InvalidTime;
    TraceEventType type = TraceEventType::Unknown;

    // Track this event belongs to (see TimelineTrack built-in ids).
    QString trackId;

    std::optional<quint64> address;
    std::optional<SourceLocation> source;

    QVariantMap metadata;

    QString label() const;
};

// Metadata key carrying the DebuggerSession stop sequence number
// (ExecutionSnapshot::stepIndex) of the stop that produced this event.
// This is the stable link between a TraceEvent and its snapshot: unlike
// executionHistory() vector indices it survives front-eviction, and unlike
// signal-arrival order it does not depend on async fetch timing. Events
// without a captured snapshot (e.g. reverse-replay stops, capture skipped
// on session reset) simply carry no usable step.
inline constexpr const char *SnapshotStepKey = "snapshotStep";

inline std::optional<int> traceSnapshotStep(const TraceEvent &event)
{
    const QVariant value = event.metadata.value(QString::fromLatin1(SnapshotStepKey));
    if (!value.isValid())
        return std::nullopt;
    bool ok = false;
    const int step = value.toInt(&ok);
    if (!ok)
        return std::nullopt;
    return step;
}

inline bool operator<(const TraceEvent &a, const TraceEvent &b)
{
    if (a.time != b.time)
        return a.time < b.time;
    return a.id < b.id;
}

// Future "find previous write" support. Data model only: no backend can
// populate this yet (see HistoryBackend::findPreviousWrite).
enum class MemoryAccessType {
    Read,
    Write
};

struct MemoryAccessEvent {
    TimePoint time = InvalidTime;
    quint64 address = 0;
    quint32 size = 0;
    MemoryAccessType access = MemoryAccessType::Write;

    QByteArray previousValue;
    QByteArray newValue;

    std::optional<quint64> pc;
    std::optional<SourceLocation> source;
};

} // namespace history
} // namespace qddd

Q_DECLARE_METATYPE(qddd::history::ExecutionState)
Q_DECLARE_METATYPE(qddd::history::TraceEvent)
