#pragma once
#include "DebugSession.h"
#include <cmath>

struct ValueHistoryPoint {
    int snapshotIndex = 0;
    QString value;
    double numericValue = 0;
    bool numeric = false;
    bool available = false;
    bool changed = false;
};
inline QVector<ValueHistoryPoint> valueHistory(const QVector<ExecutionSnapshot>& snapshots,
                                               const QString& path)
{
    QVector<ValueHistoryPoint> points;
    bool previousAvailable = false;
    QString previous;
    for (int i = 0; i < snapshots.size(); ++i) {
        ValueHistoryPoint p;
        p.snapshotIndex = i;
        p.available = snapshots[i].variableValues.contains(path);
        p.value = snapshots[i].variableValues.value(path);
        p.changed = i > 0 && (p.available != previousAvailable || p.value != previous);
        if (p.available) {
            p.numericValue = p.value.toDouble(&p.numeric);
            if (!p.numeric) {
                bool ok = false;
                const auto integer = p.value.toLongLong(&ok, 0);
                if (ok) { p.numeric = true; p.numericValue = double(integer); }
            }
            p.numeric = p.numeric && std::isfinite(p.numericValue);
        }
        previousAvailable = p.available;
        previous = p.value;
        points.append(p);
    }
    return points;
}
