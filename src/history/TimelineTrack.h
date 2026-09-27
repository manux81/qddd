#pragma once

// Generic, extensible timeline tracks.
//
// The timeline itself only knows track ids + display names + events.
// New domains (CAN, UART, SPI, GPIO, DMA, timers, RTOS tasks, ...) are added
// as new TimelineTrackProvider implementations, never by editing the widget.
// No track id below is target-specific.

#include "HistoryTypes.h"

#include <QString>

#include <vector>

namespace qddd {
namespace history {

namespace TrackIds {
inline const char *Cpu = "cpu";
inline const char *Interrupts = "interrupts";
inline const char *Exceptions = "exceptions";
inline const char *Breakpoints = "breakpoints";
inline const char *Watchpoints = "watchpoints";
inline const char *Memory = "memory";
inline const char *Peripherals = "peripherals";
inline const char *UserEvents = "user";
} // namespace TrackIds

class TimelineTrackProvider {
public:
    virtual ~TimelineTrackProvider() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    // Track ordering hint for the timeline (lower sorts first).
    virtual int order() const { return 100; }

    virtual std::vector<TraceEvent> events(TimeRange range) const = 0;
};

// Convenience provider that filters a shared event store by track id.
// Adapters feed one HistorySession store; each visible row is a lightweight
// filter over it, so new tracks cost no new storage or backend protocol.
class FilterTrackProvider : public TimelineTrackProvider {
public:
    FilterTrackProvider(QString trackId, QString displayName, int order,
                        const std::vector<TraceEvent> *store)
        : m_trackId(std::move(trackId))
        , m_displayName(std::move(displayName))
        , m_order(order)
        , m_store(store)
    {
    }

    QString id() const override { return m_trackId; }
    QString displayName() const override { return m_displayName; }
    int order() const override { return m_order; }

    std::vector<TraceEvent> events(TimeRange range) const override
    {
        std::vector<TraceEvent> out;
        if (!m_store)
            return out;
        for (const TraceEvent &event : *m_store) {
            if (event.trackId == m_trackId && range.contains(event.time))
                out.push_back(event);
        }
        return out;
    }

private:
    QString m_trackId;
    QString m_displayName;
    int m_order = 100;
    const std::vector<TraceEvent> *m_store = nullptr;
};

} // namespace history
} // namespace qddd
