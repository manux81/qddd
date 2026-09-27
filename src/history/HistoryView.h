#pragma once

// Time Travel view: compact header, execution timeline, selected-point
// inspector. Three areas only; no metadata dump, no duplicate value tools.
//
// Selection goes through the Time Machine session API (seekToHistoryPoint /
// returnToPresent), never around it, so snapshot-only and seek-capable
// backends share one interaction with honest semantics.

#include "history/HistorySession.h"
#include "history/TimeMachineModel.h"

#include <QStyledItemDelegate>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QBoxLayout;
class QScrollArea;
class QTableWidget;
class QToolButton;
class QVBoxLayout;

namespace qddd {
namespace history {

class TimeTravelController;

// Hierarchical timeline rows: marker + sequence/function/location.
// Roughly three text lines per row; NOW is distinct and never numbered.
class TimelineDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit TimelineDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
};

class HistoryView : public QWidget {
    Q_OBJECT
public:
    explicit HistoryView(HistorySession *session, QWidget *parent = nullptr);

    // Single navigation entry for timeline UI: all user-initiated moves go
    // through the controller so timeline, inspector and menus stay on one
    // authoritative position. Falls back to direct session calls when null
    // (tests).
    void setController(TimeTravelController *controller);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refreshAll();
    void activateRow(const QModelIndex &index);
    void showContextMenu(const QPoint &pos);
    void goToPrevious();
    void goToNext();
    void navigatePrevious();
    void navigateNext();
    void navigatePresent();
    void toggleCaptured();
    void toggleDetails();
    void toggleShowAllChanges();
    void filterCaptured(const QString &text);

private:
    struct InspectorData {
        bool live = true;
        bool hasSnapshot = false;
        QString title;
        QString function;
        QString location;
        QString liveHint;
        QList<HistoryChange> changes;
        int totalChanges = 0;
        QHash<QString, QString> values;
    };
    InspectorData collectInspectorData() const;
    void rebuildInspector(const InspectorData &data);
    void buildInspector(QBoxLayout *layout);
    void rebuildDetails(const InspectorData &data);
    void clearLayout(QLayout *layout);

    void setupHeader(QBoxLayout *layout);
    QToolButton *makeButton(const QString &text, const QString &tooltip);

    HistorySession *m_session = nullptr;
    TimeTravelController *m_controller = nullptr;

    QLabel *m_badge = nullptr;
    QLabel *m_countLabel = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QListView *m_list = nullptr;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_functionLabel = nullptr;
    QLabel *m_locationLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_changesTitle = nullptr;
    QFrame *m_changesSeparator = nullptr;
    // The whole inspector scrolls internally past a fixed cap, so even a
    // fully expanded inspector can never crush the timeline above.
    QScrollArea *m_inspectorScroll = nullptr;
    // Change rows live in a scroll region capped at a few rows: expanding
    // "show all" must scroll internally, never crush the timeline above.
    QScrollArea *m_changesScroll = nullptr;
    QVBoxLayout *m_changesBox = nullptr;
    QToolButton *m_showAllButton = nullptr;
    QToolButton *m_capturedButton = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QTableWidget *m_capturedTable = nullptr;
    QToolButton *m_detailsButton = nullptr;
    QWidget *m_detailsBox = nullptr;

    bool m_capturedExpanded = false;
    bool m_detailsExpanded = false;
    bool m_showAllChanges = false;
};

} // namespace history
} // namespace qddd
