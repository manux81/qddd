#pragma once

// Unified History UX: one dock for timeline navigation, selected-stop state,
// historical variables and value history.
//
// Single authoritative selection: HistorySession's selected event, resolved
// to an ExecutionSnapshot via DisplayedStateModel. Every tab follows
// displayedStateChanged(); the live target is never commanded from here —
// selecting a stop inspects the state captured at that stop.

#include "history/DisplayedState.h"
#include "history/HistorySession.h"
#include "history/TimelineTrack.h"

#include <QWidget>

#include <memory>
#include <vector>

class DebuggerSession;
class QLabel;
class QBoxLayout;
class QLineEdit;
class QTableWidget;
class QToolButton;
class ValueHistoryPlot;

namespace qddd {
namespace history {

class HistoryTimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistoryTimelineWidget(QWidget *parent = nullptr);

    void setSession(HistorySession *session);

    // Visible track providers. Rebuilt from the track ids actually present
    // in the store, so empty future-capability tracks never show.
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
    void rebuildContentTracks();
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
    // debugSession feeds the value-history tab from executionHistory();
    // displayModel is the single displayed-state authority all tabs follow.
    explicit HistoryView(HistorySession *session, DebuggerSession *debugSession,
                         DisplayedStateModel *displayModel, QWidget *parent = nullptr);

    HistoryTimelineWidget *timeline() const { return m_timeline; }

private slots:
    void refreshTransport();
    void refreshStateTab();
    void refreshValueTab();
    void refreshEventTab();
    void refreshAll();
    void goToStart();
    void stepBackward();
    void stepForward();
    void goLive();

private:
    void setupTransport(QBoxLayout *layout);
    QWidget *buildStateTab();
    QWidget *buildValueTab();
    QWidget *buildEventTab();
    QToolButton *makeButton(const QString &text, const QString &tooltip);

    HistorySession *m_session = nullptr;
    DebuggerSession *m_debug = nullptr;
    DisplayedStateModel *m_display = nullptr;
    HistoryTimelineWidget *m_timeline = nullptr;

    QToolButton *m_jumpStart = nullptr;
    QToolButton *m_stepBack = nullptr;
    QToolButton *m_stepFwd = nullptr;
    QToolButton *m_jumpEnd = nullptr;
    QToolButton *m_liveButton = nullptr;
    QLabel *m_modeBadge = nullptr;
    QLabel *m_position = nullptr;

    QLabel *m_stateSummary = nullptr;
    QTableWidget *m_stateVariables = nullptr;

    QLineEdit *m_valuePath = nullptr;
    ValueHistoryPlot *m_plot = nullptr;
    QTableWidget *m_valueTable = nullptr;

    QWidget *m_eventDetails = nullptr;
};

} // namespace history
} // namespace qddd
