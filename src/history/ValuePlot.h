#pragma once

// Numeric value-history plot, extracted from the former DataHistoryView so
// the unified History dock can reuse it. Plots ValueHistoryPoint sequences;
// pure view, no debugger coupling beyond ValueHistory.h.

#include "ValueHistory.h"

#include <QWidget>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

class ValueHistoryPlot : public QWidget {
public:
    QVector<ValueHistoryPoint> points;
    explicit ValueHistoryPlot(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(110);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        double low = 0, high = 0;
        bool any = false;
        for (const auto &p : points) {
            if (!p.numeric)
                continue;
            if (!any)
                low = high = p.numericValue;
            any = true;
            low = std::min(low, p.numericValue);
            high = std::max(high, p.numericValue);
        }
        if (!any) {
            painter.drawText(rect(), Qt::AlignCenter, tr("No numeric history"));
            return;
        }
        const QRectF area = QRectF(rect()).adjusted(55, 15, -15, -20);
        painter.setPen(palette().text().color());
        painter.drawText(3, 18, QString::number(high));
        painter.drawText(3, height() - 20, QString::number(low));
        QPainterPath path;
        bool connected = false;
        for (int i = 0; i < points.size(); ++i) {
            if (!points[i].numeric) {
                connected = false;
                continue;
            }
            QPointF point(area.left() + area.width() * i / std::max(1, points.size() - 1),
                          high == low ? area.center().y()
                                      : area.bottom() - area.height() * (points[i].numericValue - low) / (high - low));
            if (connected)
                path.lineTo(point);
            else
                path.moveTo(point);
            connected = true;
            painter.drawEllipse(point, 2, 2);
        }
        painter.setPen(QPen(palette().highlight().color(), 2));
        painter.drawPath(path);
    }
};
