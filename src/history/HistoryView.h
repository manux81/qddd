#pragma once

// Execution History UI: timeline-centric navigation over recorded stops.
//
// The timeline is a first-class debugging component, not a hidden dialog.
// It talks to HistorySession only. Backend operations without capability
// support are visibly disabled, never simulated.

#include "history/HistorySession.h"
#include "history/TimelineTrack.h"

#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QBoxLayout;
class QToolButton;
class QTreeWidget;

namespace qddd {
namespace history {

class HistoryTimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistoryTimelineWidget(QWidget *parent = nullptr);

    void setSession(HistorySession *session);

    // Visible track providers. Defaults to the eight built-in generic tracks
    // filtering the session store; future domains add providers here.
    void setTracks(std::vector<std::unique_ptr<TimelineTrackProvider>> tracks);

signals:
    void timeClicked(qddd::history::TimePoint time);
    void eventClicked(qddd::history::TraceEventId id);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    QSize sizeHint() const override;

private:
    struct Layout {
        int trackTop = 0;
        int trackHeight = 0;
        int axisTop = 0;
        int leftGutter = 110;
        int rightMargin = 12;
    };

    Layout computeLayout() const;
    double pxPerNs() const { return m_pxPerMs / 1e6; }
    TimePoint timeAtX(int x) const;
    int xAtTime(TimePoint t) const;
    const TraceEvent *eventAt(const QPoint &pos) const;
    void ensureVisible(TimePoint t);
    void selectOffset(int delta);
    void rebuildDefaultTracks();
    TimeRange visibleRange() const;

    HistorySession *m_session = nullptr;
    std::vector<std::unique_ptr<TimelineTrackProvider>> m_tracks;

    double m_pxPerMs = 50.0; // zoom level
    TimePoint m_viewStart = InvalidTime; // ns at left edge; Invalid = follow live
    bool m_followLive = true;
    // Press-drag-release gesture state: a press seeks/selects only if the
    // pointer is released without dragging (drag pans instead).
    QPoint m_pressPos{-1, -1};
    TimePoint m_pressViewStart = InvalidTime;
    bool m_panning = false;
};

class HistoryView : public QWidget {
    Q_OBJECT
public:
    explicit HistoryView(HistorySession *session, QWidget *parent = nullptr);

    HistoryTimelineWidget *timeline() const { return m_timeline; }

private slots:
    void refreshControls();
    void refreshDetails();
    void refreshFlowPlaceholder();
    void goToStart();
    void stepBackward();
    void stepForward();
    void continueBackward();
    void continueForward();
    void goToEnd();

private:
    void setupTransport(QBoxLayout *layout);
    void setupDetails(QBoxLayout *layout);
    QToolButton *makeButton(const QString &text, const QString &tooltip);

    HistorySession *m_session = nullptr;
    HistoryTimelineWidget *m_timeline = nullptr;

    QToolButton *m_jumpStart = nullptr;
    QToolButton *m_stepBack = nullptr;
    QToolButton *m_stepFwd = nullptr;
    QToolButton *m_contBack = nullptr;
    QToolButton *m_contFwd = nullptr;
    QToolButton *m_jumpEnd = nullptr;
    QLabel *m_status = nullptr;
    QWidget *m_details = nullptr;
    QTreeWidget *m_flowTree = nullptr;
};

} // namespace history
} // namespace qddd
