#pragma once
#include "ValueHistory.h"
#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

class HistoryPlot : public QWidget {
public:
    QVector<ValueHistoryPoint> points;
    explicit HistoryPlot(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(48);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        double low = 0, high = 0; bool any = false;
        for (const auto& p : points) if (p.numeric) {
            if (!any) low = high = p.numericValue;
            any = true; low = std::min(low, p.numericValue); high = std::max(high, p.numericValue);
        }
        if (!any) { painter.drawText(rect(), Qt::AlignCenter, tr("No numeric history")); return; }
        const QRectF area = QRectF(rect()).adjusted(55, 15, -15, -20);
        painter.setPen(palette().text().color());
        painter.drawText(3, 18, QString::number(high));
        painter.drawText(3, height()-20, QString::number(low));
        QPainterPath path; bool connected = false;
        for (int i = 0; i < points.size(); ++i) {
            if (!points[i].numeric) { connected = false; continue; }
            QPointF point(area.left() + area.width()*i/std::max(1, points.size()-1),
                high == low ? area.center().y() : area.bottom()-area.height()*(points[i].numericValue-low)/(high-low));
            if (connected) path.lineTo(point); else path.moveTo(point);
            connected = true;
            painter.drawEllipse(point, 2, 2);
        }
        painter.setPen(QPen(palette().highlight().color(), 2)); painter.drawPath(path);
    }
};
class DataHistoryView : public QWidget {
public:
    explicit DataHistoryView(DebuggerSession* session, QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(0, 0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(tr("Recorded values — selecting a stop does not rewind execution."), this));
        auto* stops = new QComboBox(this); layout->addWidget(stops);
        auto* path = new QLineEdit(this); path->setPlaceholderText(tr("Value path, e.g. controller.state.temperature")); layout->addWidget(path);
        auto* values = new QTableWidget(this); values->setColumnCount(2);
        values->setMinimumHeight(45);
        values->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        values->setHorizontalHeaderLabels({tr("Path"), tr("Recorded value")});
        values->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(values);
        auto* plot = new HistoryPlot(this); layout->addWidget(plot);
        auto* history = new QTableWidget(this); history->setColumnCount(3);
        history->setMinimumHeight(45);
        history->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        history->setHorizontalHeaderLabels({tr("Snapshot"), tr("Value"), tr("Changed")});
        history->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(history);
        auto updateHistory = [session, path, plot, history] {
            plot->points = valueHistory(session->executionHistory(), path->text()); plot->update();
            history->setRowCount(plot->points.size());
            for (int i=0; i<plot->points.size(); ++i) {
                const auto& p = plot->points[i];
                history->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
                history->setItem(i, 1, new QTableWidgetItem(p.available ? p.value : tr("<unavailable>")));
                history->setItem(i, 2, new QTableWidgetItem(p.changed ? tr("Yes") : tr("No")));
            }
        };
        connect(path, &QLineEdit::textChanged, this, updateHistory);
        connect(values, &QTableWidget::cellClicked, this, [values, path](int row, int) { path->setText(values->item(row, 0)->text()); });
        connect(stops, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [session, values](int index) {
            const auto* snapshot = session->snapshotAt(index); values->setRowCount(0);
            if (!snapshot) return;
            auto keys = snapshot->variableValues.keys(); keys.sort(); values->setRowCount(keys.size());
            for (int i=0; i<keys.size(); ++i) {
                values->setItem(i, 0, new QTableWidgetItem(keys[i]));
                values->setItem(i, 1, new QTableWidgetItem(snapshot->variableValues.value(keys[i])));
            }
        });
        connect(session, &DebuggerSession::snapshotCaptured, this, [session, stops, updateHistory](const ExecutionSnapshot& snapshot) {
            if (stops->count() >= session->executionHistory().size()) stops->clear();
            stops->addItem(tr("Stop %1 · %2:%3 · thread %4 · frame %5").arg(snapshot.stepIndex).arg(snapshot.file).arg(snapshot.line).arg(snapshot.threadId).arg(snapshot.frame));
            stops->setCurrentIndex(stops->count()-1); updateHistory();
        });
    }

    QSize minimumSizeHint() const override { return QSize(0, 0); }
    QSize sizeHint() const override { return QSize(520, 220); }
};
