// Regression tests for Time Travel view behavior (offscreen Widgets).
//
// Bug 1: after navigating history, returning to live must realign the Data
// Display chip and all views with the live debugger state (LIVE != last
// snapshot index).
// Bug 2: clicking any timeline record - including the first - must not
// move, reorder or recenter the timeline; only selection + inspector change.
#include "history/DisplayedState.h"
#include "history/HistorySession.h"
#include "history/HistoryView.h"
#include "history/TimeMachineModel.h"
#include "GraphicalVariablesView.h"
#include "VariablesView.h"

#include <QApplication>
#include <QLabel>
#include <QListView>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QtTest/QTest>
#include <QSignalSpy>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; return 1; } } while(false)

using namespace qddd::history;

static ExecutionSnapshot makeSnapshot(int step, const QString &func,
                                      const QString &file, int line)
{
    ExecutionSnapshot s;
    s.stepIndex = step;
    s.timestampNs = qint64(step) * 1000000LL;
    s.function = func;
    s.file = file;
    s.line = line;
    s.threadId = QStringLiteral("1");
    s.variableValues.insert(QStringLiteral("x"), QString::number(step * 10));
    return s;
}

static TraceEvent makeStopEvent(TimePoint t, int step, const QString &func,
                                const QString &file, int line)
{
    TraceEvent e;
    e.time = t;
    e.type = TraceEventType::Stop;
    e.trackId = QStringLiteral("cpu");
    SourceLocation loc;
    loc.function = func;
    loc.file = file;
    loc.line = line;
    e.source = loc;
    e.metadata.insert(QStringLiteral("snapshotStep"), step);
    return e;
}

// Session preloaded with `stops` historical stops (steps 1..stops) whose
// snapshots carry x = step*10. No backend: pure snapshot inspection.
static void fillSession(HistorySession &session, int stops)
{
    session.setSnapshotResolver([](int step) -> std::optional<ExecutionSnapshot> {
        if (step < 1 || step > 99)
            return std::nullopt;
        return makeSnapshot(step, QStringLiteral("func%1").arg(step),
                            QStringLiteral("file%1.c").arg(step), 100 + step);
    });
    for (int i = 1; i <= stops; ++i)
        session.addEvent(makeStopEvent(TimePoint(i) * 1000000LL, i,
                                       QStringLiteral("func%1").arg(i),
                                       QStringLiteral("file%1.c").arg(i), 100 + i));
    session.goLive();
    QCoreApplication::processEvents();
}

static int rowTop(QListView *list, int row)
{
    return list->visualRect(list->model()->index(row, 0)).top();
}

// Bug 2: clicking the first (oldest) record must leave timeline geometry alone.
static int checkFirstRecordClickStable()
{
    HistorySession session;
    fillSession(session, 30);
    HistoryView view(&session);
    view.resize(420, 520);
    view.show();
    QTest::qWaitForWindowExposed(&view);
    QCoreApplication::processEvents();

    QListView *list = view.findChild<QListView *>();
    CHECK(list);
    // Start live (row 0 = present) with the oldest record (last row) visible.
    CHECK(session.isLive());
    const int lastRow = list->model()->rowCount() - 1;
    list->scrollTo(list->model()->index(lastRow, 0),
                   QAbstractItemView::PositionAtBottom);
    QCoreApplication::processEvents();

    const int scrollBefore = list->verticalScrollBar()->value();
    const int rowTopBefore = rowTop(list, lastRow);
    const int rows = list->model()->rowCount();
    QVector<int> topsBefore;
    for (int row = 0; row < rows; ++row)
        topsBefore.append(rowTop(list, row));

    // Click the first (oldest) record.
    const QRect rowRect = list->visualRect(list->model()->index(lastRow, 0));
    CHECK(rowRect.isValid());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                       rowRect.center());
    QCoreApplication::processEvents();

    // The historic stop is selected...
    CHECK(!session.isLive());
    CHECK(session.selectedEventId() == session.storedEvents().front().id);
    // ...but timeline rows neither moved nor reordered, and scrolling did
    // not jump: selection is highlight-only.
    CHECK(list->verticalScrollBar()->value() == scrollBefore);
    CHECK(rowTop(list, lastRow) == rowTopBefore);
    for (int row = 0; row < rows; ++row)
        CHECK(rowTop(list, row) == topsBefore[row]);
    // The inspector may claim space for its content, but the timeline keeps
    // a usable height: rows are never crushed out of existence.
    CHECK(list->viewport()->height() >= 120);
    return 0;
}

// Expanding show-all / captured values scrolls inside the inspector and
// never crushes the timeline out of existence.
static int checkInspectorGrowthBounded()
{
    HistorySession session;
    fillSession(session, 3);
    // Snapshots with 8 values changing every stop: show-all appears.
    session.setSnapshotResolver([](int step) -> std::optional<ExecutionSnapshot> {
        if (step < 1 || step > 3)
            return std::nullopt;
        ExecutionSnapshot s;
        s.stepIndex = step;
        s.timestampNs = qint64(step) * 1000000LL;
        s.function = QStringLiteral("func");
        s.file = QStringLiteral("file.c");
        s.line = 1;
        for (int i = 0; i < 8; ++i)
            s.variableValues.insert(QStringLiteral("v%1").arg(i),
                                    QString::number(step * 10 + i));
        return s;
    });
    HistoryView view(&session);
    view.resize(420, 520);
    view.show();
    QTest::qWaitForWindowExposed(&view);
    QCoreApplication::processEvents();

    QListView *list = view.findChild<QListView *>();
    CHECK(list);
    session.selectEvent(session.storedEvents().front().id);
    QCoreApplication::processEvents();
    CHECK(!session.isLive());

    const int scrollBefore = list->verticalScrollBar()->value();
    const int rows = list->model()->rowCount();
    QVector<int> topsBefore;
    for (int row = 0; row < rows; ++row)
        topsBefore.append(rowTop(list, row));

    // Expand everything through the view's own toggle buttons.
    const QList<QToolButton *> buttons = view.findChildren<QToolButton *>();
    bool expandedShowAll = false;
    for (QToolButton *button : buttons) {
        if (button->text().startsWith(QStringLiteral("Show all"))) {
            button->click();
            expandedShowAll = true;
        } else if (button->text().startsWith(QStringLiteral("Captured values"))
                   || button->text().startsWith(QStringLiteral("More details"))) {
            if (!button->isChecked())
                button->click();
        }
    }
    QCoreApplication::processEvents();
    CHECK(expandedShowAll);

    // Rows neither moved nor reordered; scrolling did not jump.
    CHECK(list->verticalScrollBar()->value() == scrollBefore);
    for (int row = 0; row < rows; ++row)
        CHECK(rowTop(list, row) == topsBefore[row]);
    // The timeline keeps a usable height...
    CHECK(list->viewport()->height() >= 120);
    // ...because show-all growth stays inside its own capped region instead
    // of collapsing the list: the region never exceeds its cap, so any
    // overflow scrolls internally (where platform fonts require it).
    auto *changesScroll =
        view.findChild<QScrollArea *>(QStringLiteral("changesScroll"));
    CHECK(changesScroll);
    const int rowHeight = list->fontMetrics().height() + 2;
    const int expectedCap = 6 * rowHeight + 4;
    CHECK(changesScroll->height() <= expectedCap + 12);
    // Expanded state is honestly reported: all rows exist, toggle offers
    // to collapse again.
    bool showFewer = false;
    for (QToolButton *button : view.findChildren<QToolButton *>()) {
        if (button->text() == QStringLiteral("Show fewer changes"))
            showFewer = true;
    }
    CHECK(showFewer);
    return 0;
}

// Bug 1: returning to LIVE realigns chip + views with live state.
static QString graphChipText(GraphicalVariablesView &graph)
{
    const QList<QLabel *> labels = graph.findChildren<QLabel *>();
    for (QLabel *label : labels) {
        const QString text = label->text();
        if (text == QStringLiteral("LIVE") || text.startsWith(QStringLiteral("S"))
            || text.startsWith(QStringLiteral("TRACE")))
            return text;
    }
    return {};
}

static int checkReturnToLiveRealigns()
{
    HistorySession session;
    fillSession(session, 5);

    VariablesView variables;
    GraphicalVariablesView graph;
    // Live views with no history selected show the live (empty-script) state.
    DisplayedStateModel display(nullptr, &session);
    variables.setDisplayedSnapshot(display.displayedState().snapshot,
                                   display.displayedState().snapshotStep,
                                   !display.displayedState().isLive(),
                                   display.temporalState());
    graph.setDisplayedSnapshot(display.displayedState().snapshot,
                               display.displayedState().snapshotStep,
                               !display.displayedState().isLive(),
                               display.temporalState());
    CHECK(!variables.isHistorical());
    CHECK(!graph.isHistorical());
    CHECK(graphChipText(graph) == QStringLiteral("LIVE"));

    // Navigate into history: both views become historical.
    session.selectEvent(session.storedEvents().front().id);
    QCoreApplication::processEvents();
    variables.setDisplayedSnapshot(display.displayedState().snapshot,
                                   display.displayedState().snapshotStep,
                                   !display.displayedState().isLive(),
                                   display.temporalState());
    graph.setDisplayedSnapshot(display.displayedState().snapshot,
                               display.displayedState().snapshotStep,
                               !display.displayedState().isLive(),
                               display.temporalState());
    CHECK(variables.isHistorical());
    CHECK(graph.isHistorical());
    CHECK(display.displayedState().snapshotStep == 1);
    // Snapshot identity, not a fake history index.
    CHECK(graphChipText(graph) == QStringLiteral("S1"));

    // Return to live: chip/index must reset to LIVE even though a snapshot
    // (the latest) is present. LIVE != last snapshot index.
    session.goLive();
    QCoreApplication::processEvents();
    variables.setDisplayedSnapshot(display.displayedState().snapshot,
                                   display.displayedState().snapshotStep,
                                   !display.displayedState().isLive(),
                                   display.temporalState());
    graph.setDisplayedSnapshot(display.displayedState().snapshot,
                               display.displayedState().snapshotStep,
                               !display.displayedState().isLive(),
                               display.temporalState());
    CHECK(display.displayedState().isLive());
    CHECK(!variables.isHistorical());
    CHECK(!graph.isHistorical());
    CHECK(graphChipText(graph) == QStringLiteral("LIVE"));
    return 0;
}

// Invariants: LIVE carries no snapshot index; selection never reorders.
static int checkNavigationInvariants()
{
    HistorySession session;
    fillSession(session, 4);
    DisplayedStateModel display(nullptr, &session);

    // 1. LIVE never has a snapshot index.
    CHECK(display.displayedState().isLive());
    CHECK(display.displayedState().historyPointId == InvalidHistoryPointId);

    // Record chronological order.
    std::vector<TraceEventId> before;
    for (const auto &e : session.storedEvents())
        before.push_back(e.id);

    // 2/3. Selecting events never alters chronological position.
    session.selectEvent(before.front());
    session.selectEvent(before.back());
    session.selectPreviousEvent();
    session.goLive();
    std::vector<TraceEventId> after;
    for (const auto &e : session.storedEvents())
        after.push_back(e.id);
    CHECK(before == after);

    // 8. First item behaves like any other: no reorder, no geometry churn
    // at model level (row mapping is stable across selections). Row 0 is
    // the present; history rows follow newest-first.
    HistoryPointModel model;
    model.setSession(&session);
    const int rowsBefore = model.rowCount();
    session.selectEvent(before.front());
    CHECK(model.rowCount() == rowsBefore);
    CHECK(model.pointIdAtRow(1) == before.back() || model.rowCount() < 2);
    CHECK(model.pointIdAtRow(model.rowCount() - 1) == before.front());
    session.goLive();
    CHECK(model.rowCount() == rowsBefore);
    return 0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (int rc = checkFirstRecordClickStable()) {
        std::cerr << "first-record stability FAILED\n";
        return rc;
    }
    if (int rc = checkInspectorGrowthBounded()) {
        std::cerr << "inspector growth FAILED\n";
        return rc;
    }
    if (int rc = checkReturnToLiveRealigns()) {
        std::cerr << "return-to-live FAILED\n";
        return rc;
    }
    if (int rc = checkNavigationInvariants()) {
        std::cerr << "navigation invariants FAILED\n";
        return rc;
    }
    std::cout << "time travel view tests passed\n";
    return 0;
}
