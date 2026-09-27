#pragma once

// Execution comparison model (Run A vs Run B).
//
// Only model boundaries + a trivial first-divergence scan over recorded
// event-type sequences are provided. A full comparison engine needs richer
// backends and is explicitly out of scope.

#include "HistoryTypes.h"
#include "RecordingMetadata.h"

#include <QString>

#include <vector>

namespace qddd {
namespace history {

using ExecutionRecordingId = QString;

struct ExecutionRecording {
    ExecutionRecordingId id;
    HistoryRecordingMetadata metadata;
    std::vector<TraceEvent> events;
};

struct DivergencePoint {
    bool hasDivergence = false;
    std::size_t indexA = 0;
    std::size_t indexB = 0;
    QString reason;
};

// First position where the two event streams differ in type/track.
// Compares recorded model data only; no backend interaction.
inline DivergencePoint findFirstDivergence(const ExecutionRecording &a,
                                           const ExecutionRecording &b)
{
    DivergencePoint result;
    const std::size_t common = std::min(a.events.size(), b.events.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (a.events[i].type != b.events[i].type
            || a.events[i].trackId != b.events[i].trackId) {
            result.hasDivergence = true;
            result.indexA = i;
            result.indexB = i;
            result.reason = QStringLiteral("Event %1 differs: %2 vs %3")
                                .arg(i)
                                .arg(traceEventTypeName(a.events[i].type))
                                .arg(traceEventTypeName(b.events[i].type));
            return result;
        }
    }
    if (a.events.size() != b.events.size()) {
        result.hasDivergence = true;
        result.indexA = common;
        result.indexB = common;
        result.reason = QStringLiteral("Different recording lengths: %1 vs %2")
                            .arg(a.events.size())
                            .arg(b.events.size());
    }
    return result;
}

} // namespace history
} // namespace qddd
