#include "HistoryView.h"

#include "DebugSession.h"
#include "history/ValuePlot.h"

#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace qddd {
namespace history {
namespace {

constexpr double kMinPxPerMs = 0.5;
constexpr double kMaxPxPerMs = 5000.0;
constexpr int kTrackHeight = 26;
constexpr int kAxisHeight = 26;
constexpr int kMarkerRadius = 5;
constexpr int kClickTolerancePx = 7;

QString formatTimeMs(TimePoint t)
{
    if (t == InvalidTime)
        return HistoryTimelineWidget::tr("—");
    return QStringLiteral("%1 ms").arg(t / 1e6, 0, 'f', 3);
}

QString formatAddress(const std::optional<quint64> &address)
{
    if (!address)
        return HistoryTimelineWidget::tr("—");
    return QStringLiteral("0x%1").arg(*address, 0, 16, QLatin1Char('0')).toUpper();
}

QColor trackColor(const QString &trackId)
{
    if (trackId == QLatin1String(TrackIds::Stops))
        return QColor(0x4c, 0xaf, 0x50);
    if (trackId == QLatin1String(TrackIds::Cpu))
        return QColor(0x4c, 0xaf, 0x50);
    if (trackId == QLatin1String(TrackIds::Interrupts))
        return QColor(0xff, 0x98, 0x00);
    if (trackId == QLatin1String(TrackIds::Exceptions))
        return QColor(0xf4, 0x43, 0x36);
    if (trackId == QLatin1String(TrackIds::Breakpoints))
        return QColor(0x9c, 0x27, 0xb0);
    if (trackId == QLatin1String(TrackIds::Watchpoints))
        return QColor(0x00, 0xbc, 0xd4);
    if (trackId == QLatin1String(TrackIds::Memory))
        return QColor(0x3f, 0x51, 0xb5);
    if (trackId == QLatin1String(TrackIds::Peripherals))
        return QColor(0x00, 0x96, 0x88);
    return QColor(0x9e, 0x9e, 0x9e);
}

const TraceEvent *findEventByStep(const std::vector<TraceEvent> &store, int step)
{
    for (const TraceEvent &event : store) {
        if (traceSnapshotStep(event) == step)
            return &event;
    }
    return nullptr;
}

} // namespace

// ============================================================================
// HistoryTimelineWidget
// ============================================================================

HistoryTimelineWidget::HistoryTimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumHeight(150);
    setToolTip(tr("Click an event to inspect the recorded stop; drag to pan, wheel to zoom."));
}

void HistoryTimelineWidget::setSession(HistorySession *session)
{
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    rebuildContentTracks();
    m_followLive = true;
    m_viewStart = InvalidTime;
    if (!m_session)
        return;
    connect(m_session, &HistorySession::eventsAppended, this, [this] {
        rebuildContentTracks();
        if (m_followLive)
            m_viewStart = InvalidTime;
        update();
    });
    connect(m_session, &HistorySession::currentTimeChanged, this,
            [this] { update(); });
    connect(m_session, &HistorySession::selectedEventChanged, this,
            [this] { update(); });
}

void HistoryTimelineWidget::setTracks(std::vector<std::unique_ptr<TimelineTrackProvider>> tracks)
{
    m_tracks = std::move(tracks);
    update();
}

void HistoryTimelineWidget::rebuildContentTracks()
{
    m_tracks.clear();
    if (!m_session)
        return;
    const std::vector<TraceEvent> *store = &m_session->storedEvents();
    for (const QString &trackId : activeTrackIds(*store)) {
        m_tracks.push_back(std::make_unique<FilterTrackProvider>(
            trackId, trackDisplayName(trackId), trackOrder(trackId), store));
    }
}

HistoryTimelineWidget::Layout HistoryTimelineWidget::computeLayout() const
{
    Layout layout;
    layout.trackHeight = kTrackHeight;
    layout.trackTop = 6;
    layout.axisTop = layout.trackTop
        + static_cast<int>(m_tracks.size()) * layout.trackHeight + 4;
    return layout;
}

QSize HistoryTimelineWidget::sizeHint() const
{
    return {640, computeLayout().axisTop + kAxisHeight + 6};
}

TimeRange HistoryTimelineWidget::visibleRange() const
{
    if (!m_session || m_session->storedEvents().empty())
        return {};
    const TimeRange full = m_session->fullRange();
    const Layout layout = computeLayout();
    const double plotWidth = std::max(1, width() - layout.leftGutter - layout.rightMargin);
    TimePoint start = m_viewStart;
    if (start == InvalidTime) {
        const double visibleNs = plotWidth / pxPerNs();
        start = std::max(full.begin, TimePoint(full.end - TimePoint(visibleNs)));
    }
    const double visibleNs = plotWidth / pxPerNs();
    return {start, TimePoint(start + TimePoint(visibleNs))};
}

TimePoint HistoryTimelineWidget::timeAtX(int x) const
{
    const Layout layout = computeLayout();
    const TimeRange visible = visibleRange();
    if (!visible.isValid())
        return InvalidTime;
    const double dx = x - layout.leftGutter;
    return TimePoint(visible.begin + TimePoint(dx / pxPerNs()));
}

int HistoryTimelineWidget::xAtTime(TimePoint t) const
{
    const Layout layout = computeLayout();
    const TimeRange visible = visibleRange();
    if (!visible.isValid())
        return layout.leftGutter;
    return layout.leftGutter + int((t - visible.begin) * pxPerNs());
}

void HistoryTimelineWidget::ensureVisible(TimePoint t)
{
    TimeRange visible = visibleRange();
    if (!visible.isValid())
        return;
    if (!visible.contains(t)) {
        const Layout layout = computeLayout();
        const double plotWidth = std::max(1, width() - layout.leftGutter - layout.rightMargin);
        const double visibleNs = plotWidth / pxPerNs();
        m_viewStart = TimePoint(std::max<qint64>(0, t - TimePoint(visibleNs / 2)));
        m_followLive = false;
        update();
    }
}

const TraceEvent *HistoryTimelineWidget::eventAt(const QPoint &pos) const
{
    if (!m_session)
        return nullptr;
    const Layout layout = computeLayout();
    if (pos.x() < layout.leftGutter)
        return nullptr;
    const int row = (pos.y() - layout.trackTop) / layout.trackHeight;
    if (row < 0 || row >= static_cast<int>(m_tracks.size()))
        return nullptr;
    const TimeRange visible = visibleRange();
    if (!visible.isValid())
        return nullptr;
    const std::vector<TraceEvent> trackEvents =
        m_tracks[size_t(row)]->events({0, std::numeric_limits<qint64>::max()});
    const TraceEvent *best = nullptr;
    int bestDx = kClickTolerancePx + 1;
    for (const TraceEvent &event : trackEvents) {
        if (!visible.contains(event.time) && event.time != visible.end)
            continue;
        const int dx = std::abs(xAtTime(event.time) - pos.x());
        if (dx <= kClickTolerancePx && dx < bestDx) {
            bestDx = dx;
            best = &event;
        }
    }
    if (!best)
        return nullptr;
    // Map back to the session-owned event so the pointer stays valid.
    return m_session->eventById(best->id);
}

void HistoryTimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    const Layout layout = computeLayout();

    if (!m_session || m_session->storedEvents().empty()) {
        painter.setPen(palette().placeholderText().color());
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No execution history yet.\n"
                            "Run the target: each stop is recorded on the timeline."));
        return;
    }

    const TimeRange full = m_session->fullRange();
    const TimeRange visible = visibleRange();
    const TraceEventId selectedId = m_session->selectedEventId();
    const TimePoint cursor = m_session->currentTime();

    QFont labelFont = font();
    labelFont.setPointSize(std::max(8, labelFont.pointSize() - 1));

    // Track rows (only lanes with recorded events exist).
    for (size_t row = 0; row < m_tracks.size(); ++row) {
        const int y = layout.trackTop + int(row) * layout.trackHeight;
        const QRect rowRect(0, y, width(), layout.trackHeight);
        if (row % 2 == 1)
            painter.fillRect(rowRect, palette().alternateBase());

        painter.setPen(palette().text().color());
        painter.setFont(labelFont);
        painter.drawText(QRect(6, y, layout.leftGutter - 12, layout.trackHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         m_tracks[row]->displayName());

        // Lane line.
        painter.setPen(QPen(palette().mid().color(), 1));
        const int laneY = y + layout.trackHeight / 2;
        painter.drawLine(layout.leftGutter, laneY, width() - layout.rightMargin, laneY);

        const std::vector<TraceEvent> trackEvents =
            m_tracks[row]->events({0, std::numeric_limits<qint64>::max()});
        const QColor color = trackColor(m_tracks[row]->id());
        for (const TraceEvent &event : trackEvents) {
            if (!visible.contains(event.time) && event.time != visible.end)
                continue;
            const int x = xAtTime(event.time);
            const bool selected = event.id == selectedId;
            painter.setBrush(selected ? palette().highlight() : color);
            painter.setPen(selected ? palette().highlightedText().color()
                                    : color.darker(130));
            painter.drawEllipse(QPoint(x, laneY), kMarkerRadius + (selected ? 2 : 0),
                                kMarkerRadius + (selected ? 2 : 0));
        }
    }

    // Current-time cursor.
    if (cursor != InvalidTime && visible.isValid()
        && (visible.contains(cursor) || cursor == visible.end)) {
        const int x = xAtTime(cursor);
        painter.setPen(QPen(palette().highlight().color(), 2));
        painter.drawLine(x, layout.trackTop, x, layout.axisTop + kAxisHeight - 4);
        painter.setBrush(palette().highlight());
        const QRect cursorHead(x - 1, layout.trackTop - 6, 3, 6);
        painter.drawRect(cursorHead);
    }

    // Time axis.
    painter.setPen(palette().text().color());
    painter.setFont(labelFont);
    painter.drawLine(layout.leftGutter, layout.axisTop, width() - layout.rightMargin,
                     layout.axisTop);
    if (visible.isValid()) {
        const double targetTickPx = 90.0;
        const double visibleMs = (visible.end - visible.begin) / 1e6;
        const double plotWidth = std::max(1, width() - layout.leftGutter - layout.rightMargin);
        double stepMs = visibleMs * targetTickPx / plotWidth;
        // Snap to 1/2/5 decades.
        const double magnitude = std::pow(10.0, std::floor(std::log10(std::max(stepMs, 1e-9))));
        for (double candidate : {1.0, 2.0, 5.0, 10.0}) {
            if (magnitude * candidate >= stepMs) {
                stepMs = magnitude * candidate;
                break;
            }
        }
        const qint64 firstTick = qint64(std::ceil(visible.begin / 1e6 / stepMs) * stepMs);
        const qint64 lastTick = qint64(visible.end / 1e6);
        QFontMetrics metrics(labelFont);
        for (qint64 tickMs = firstTick; tickMs <= lastTick;
             tickMs += qint64(std::max(1.0, stepMs))) {
            const TimePoint tickNs = TimePoint(tickMs * 1000000);
            const int x = xAtTime(tickNs);
            painter.drawLine(x, layout.axisTop, x, layout.axisTop + 4);
            const double relMs = (tickNs - full.end) / 1e6;
            const QString text = relMs >= -0.0005
                ? tr("NOW")
                : tr("%1 ms").arg(relMs, 0, 'f', 0);
            painter.drawText(x - metrics.horizontalAdvance(text) / 2,
                             layout.axisTop + 6 + metrics.ascent(), text);
        }
    }
}

void HistoryTimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_session || event->button() != Qt::LeftButton)
        return;
    setFocus(Qt::MouseFocusReason);
    m_pressPos = event->pos();
    m_pressViewStart = visibleRange().isValid() ? visibleRange().begin : InvalidTime;
    m_panning = false;
}

void HistoryTimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (const TraceEvent *hit = eventAt(event->pos())) {
        QString tip = QStringLiteral("%1\n%2").arg(hit->label(), formatTimeMs(hit->time));
        if (hit->source && hit->source->isValid())
            tip += QStringLiteral("\n%1:%2").arg(hit->source->file).arg(hit->source->line);
        QToolTip::showText(event->globalPos(), tip, this);
    } else {
        QToolTip::hideText();
    }
    // Left-drag on empty space pans the timeline.
    if ((event->buttons() & Qt::LeftButton) && m_session && !m_pressPos.isNull()
        && !m_session->storedEvents().empty() && m_pressViewStart != InvalidTime) {
        const int dx = event->pos().x() - m_pressPos.x();
        if (std::abs(dx) > 4)
            m_panning = true;
        if (m_panning) {
            m_viewStart = TimePoint(
                std::max<qint64>(0, m_pressViewStart - TimePoint(dx / pxPerNs())));
            m_followLive = false;
            update();
        }
    }
}

void HistoryTimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_session || event->button() != Qt::LeftButton || m_pressPos.isNull())
        return;
    const bool wasPan = m_panning;
    m_panning = false;
    m_pressPos = {-1, -1};
    if (wasPan)
        return; // Drag gesture already panned; do not seek.
    if (const TraceEvent *hit = eventAt(event->pos())) {
        m_session->selectEvent(hit->id);
        emit eventClicked(hit->id);
        return;
    }
    const TimePoint t = timeAtX(event->x());
    if (t != InvalidTime) {
        m_session->seek(t);
        emit timeClicked(t);
    }
}

void HistoryTimelineWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_session || m_session->storedEvents().empty())
        return;
    const double steps = event->angleDelta().y() / 120.0;
    m_pxPerMs = std::clamp(m_pxPerMs * std::pow(1.25, steps), kMinPxPerMs, kMaxPxPerMs);
    m_followLive = false;
    if (m_viewStart == InvalidTime) {
        const TimeRange full = m_session->fullRange();
        const double plotWidth = std::max(1, width() - 110 - 12);
        m_viewStart = TimePoint(std::max<qint64>(0, full.end - TimePoint(plotWidth / pxPerNs())));
    }
    update();
    event->accept();
}

void HistoryTimelineWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_session) {
        QWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left: selectOffset(-1); event->accept(); return;
    case Qt::Key_Right: selectOffset(1); event->accept(); return;
    case Qt::Key_Home:
        if (!m_session->storedEvents().empty())
            m_session->selectEvent(m_session->storedEvents().front().id);
        event->accept();
        return;
    case Qt::Key_End: m_session->goLive(); event->accept(); return;
    default: break;
    }
    QWidget::keyPressEvent(event);
}

void HistoryTimelineWidget::selectOffset(int delta)
{
    if (!m_session || m_session->storedEvents().empty())
        return;
    const std::vector<TraceEvent> &all = m_session->storedEvents();
    const TraceEventId current = m_session->selectedEventId();
    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].id == current) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found)
        index = delta > 0 ? 0 : all.size() - 1;
    else
        index = size_t(std::clamp<qint64>(qint64(index) + delta, 0, qint64(all.size()) - 1));
    m_session->selectEvent(all[index].id);
    ensureVisible(all[index].time);
}

void HistoryTimelineWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_session)
        return;
    QMenu menu(this);
    QAction *findPrev = menu.addAction(tr("Find Previous Write"));
    QAction *findNext = menu.addAction(tr("Find Next Write"));
    QAction *valueHistory = menu.addAction(tr("Show Value History"));
    const bool memoryHistory = m_session->capabilities().testFlag(
        HistoryCapability::MemoryWriteHistory);
    findPrev->setEnabled(memoryHistory);
    findNext->setEnabled(memoryHistory);
    valueHistory->setEnabled(memoryHistory);
    if (!memoryHistory) {
        findPrev->setToolTip(tr("Requires a backend with memory-write history."));
        findNext->setToolTip(tr("Requires a backend with memory-write history."));
        valueHistory->setToolTip(tr("Requires a backend with memory-write history."));
    }
    menu.addSeparator();
    QAction *addMarker = menu.addAction(tr("Add Marker Here"));
    addMarker->setEnabled(false);
    addMarker->setToolTip(tr("User markers require a backend with marker recording."));
    menu.exec(event->globalPos());
}

// ============================================================================
// HistoryView: unified History dock
// ============================================================================

HistoryView::HistoryView(HistorySession *session, DebuggerSession *debugSession,
                         DisplayedStateModel *displayModel, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_debug(debugSession)
    , m_display(displayModel)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    setupTransport(outer);

    m_timeline = new HistoryTimelineWidget(this);
    m_timeline->setSession(session);
    outer->addWidget(m_timeline, 0);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildStateTab(), tr("State"));
    tabs->addTab(buildValueTab(), tr("Value History"));
    tabs->addTab(buildEventTab(), tr("Event"));
    outer->addWidget(tabs, 1);

    if (m_display)
        connect(m_display, &DisplayedStateModel::displayedStateChanged,
                this, [this] { refreshAll(); });
    if (m_session) {
        connect(m_session, &HistorySession::eventsAppended, this, [this] {
            refreshTransport();
            refreshValueTab();
        });
    }
    if (m_debug)
        connect(m_debug, &DebuggerSession::snapshotCaptured, this, [this] {
            refreshTransport();
            refreshValueTab();
        });

    refreshAll();
}

QToolButton *HistoryView::makeButton(const QString &text, const QString &tooltip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

void HistoryView::setupTransport(QBoxLayout *layout)
{
    auto *row = new QHBoxLayout;
    row->setSpacing(2);
    // Stop-history navigation only: first / previous / next / last recorded
    // stop, plus an explicit return to live. These inspect recorded state;
    // they never command the target (tooltips say so).
    m_jumpStart = makeButton(tr("⏮"), tr("Show first recorded stop (target keeps running)"));
    m_stepBack = makeButton(tr("◀"), tr("Show previous recorded stop (target keeps running)"));
    m_stepFwd = makeButton(tr("▶"), tr("Show next recorded stop (target keeps running)"));
    m_jumpEnd = makeButton(tr("⏭"), tr("Show last recorded stop (target keeps running)"));
    m_liveButton = makeButton(tr("LIVE"), tr("Return all views to the live target state"));
    m_liveButton->setCheckable(true);
    row->addWidget(m_jumpStart);
    row->addWidget(m_stepBack);
    row->addWidget(m_stepFwd);
    row->addWidget(m_jumpEnd);
    row->addWidget(m_liveButton);
    m_modeBadge = new QLabel(this);
    m_modeBadge->setTextFormat(Qt::PlainText);
    m_modeBadge->setAlignment(Qt::AlignCenter);
    m_modeBadge->setMinimumWidth(72);
    row->addWidget(m_modeBadge);
    m_position = new QLabel(this);
    m_position->setTextFormat(Qt::PlainText);
    row->addWidget(m_position, 1);
    layout->addLayout(row);

    connect(m_jumpStart, &QToolButton::clicked, this, &HistoryView::goToStart);
    connect(m_stepBack, &QToolButton::clicked, this, &HistoryView::stepBackward);
    connect(m_stepFwd, &QToolButton::clicked, this, &HistoryView::stepForward);
    connect(m_jumpEnd, &QToolButton::clicked, this, [this] {
        if (!m_session || m_session->storedEvents().empty())
            return;
        m_session->selectEvent(m_session->storedEvents().back().id);
    });
    connect(m_liveButton, &QToolButton::clicked, this, &HistoryView::goLive);
}

QWidget *HistoryView::buildStateTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 2, 0, 0);
    m_stateSummary = new QLabel(tab);
    m_stateSummary->setTextFormat(Qt::PlainText);
    m_stateSummary->setWordWrap(true);
    layout->addWidget(m_stateSummary);
    m_stateVariables = new QTableWidget(tab);
    m_stateVariables->setColumnCount(3);
    m_stateVariables->setHorizontalHeaderLabels({tr("Variable"), tr("Value"), tr("Changed")});
    m_stateVariables->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stateVariables->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stateVariables->horizontalHeader()->setStretchLastSection(true);
    m_stateVariables->verticalHeader()->setVisible(false);
    layout->addWidget(m_stateVariables, 1);
    connect(m_stateVariables, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto *item = m_stateVariables->item(row, 0);
        if (item && m_valuePath)
            m_valuePath->setText(item->text());
    });
    return tab;
}

QWidget *HistoryView::buildValueTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 2, 0, 0);
    m_valuePath = new QLineEdit(tab);
    m_valuePath->setPlaceholderText(tr("Variable path — click a variable in State, or type here"));
    layout->addWidget(m_valuePath);
    m_plot = new ValueHistoryPlot(tab);
    layout->addWidget(m_plot);
    m_valueTable = new QTableWidget(tab);
    m_valueTable->setColumnCount(3);
    m_valueTable->setHorizontalHeaderLabels({tr("Stop"), tr("Value"), tr("Changed")});
    m_valueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_valueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_valueTable->horizontalHeader()->setStretchLastSection(true);
    m_valueTable->verticalHeader()->setVisible(false);
    layout->addWidget(m_valueTable, 1);
    connect(m_valuePath, &QLineEdit::textChanged, this, [this] { refreshValueTab(); });
    // Clicking a row inspects that recorded stop in every history view.
    connect(m_valueTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto *item = m_valueTable->item(row, 0);
        if (!item || !m_session)
            return;
        bool ok = false;
        const int step = item->data(Qt::UserRole).toInt(&ok);
        if (!ok)
            return;
        if (const TraceEvent *event = findEventByStep(m_session->storedEvents(), step))
            m_session->selectEvent(event->id);
    });
    return tab;
}

QWidget *HistoryView::buildEventTab()
{
    m_eventDetails = new QWidget(this);
    auto *outer = new QVBoxLayout(m_eventDetails);
    outer->setContentsMargins(0, 2, 0, 0);
    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Time:"), new QLabel(tr("—"), m_eventDetails));
    form->addRow(tr("Type:"), new QLabel(tr("—"), m_eventDetails));
    form->addRow(tr("PC / address:"), new QLabel(tr("—"), m_eventDetails));
    form->addRow(tr("Symbol:"), new QLabel(tr("—"), m_eventDetails));
    form->addRow(tr("Source:"), new QLabel(tr("—"), m_eventDetails));
    form->addRow(tr("Function:"), new QLabel(tr("—"), m_eventDetails));
    form->addRow(tr("Metadata:"), new QLabel(tr("—"), m_eventDetails));
    outer->addLayout(form);
    outer->addStretch(1);
    return m_eventDetails;
}

static QLabel *detailLabel(QWidget *details, int row)
{
    auto *form = details->findChild<QFormLayout *>();
    if (!form)
        return nullptr;
    auto *item = form->itemAt(row, QFormLayout::FieldRole);
    return qobject_cast<QLabel *>(item ? item->widget() : nullptr);
}

void HistoryView::refreshTransport()
{
    if (!m_session)
        return;
    const bool hasEvents = !m_session->storedEvents().empty();
    m_jumpStart->setEnabled(hasEvents);
    m_stepBack->setEnabled(hasEvents);
    m_stepFwd->setEnabled(hasEvents);
    m_jumpEnd->setEnabled(hasEvents);

    const bool live = !m_display || m_display->displayedState().isLive();
    m_liveButton->setChecked(live);
    m_liveButton->setEnabled(hasEvents || !live);
    if (live) {
        m_modeBadge->setText(tr("LIVE"));
        m_modeBadge->setToolTip(tr("Views show the live target state."));
        m_modeBadge->setStyleSheet(
            QStringLiteral("background: #1e4620; color: #d8f3dc; border-radius: 4px; padding: 2px 8px; font-weight: bold;"));
    } else {
        m_modeBadge->setText(tr("HISTORY"));
        m_modeBadge->setToolTip(
            tr("Views show a recorded stop. The target keeps running and is not rewound."));
        m_modeBadge->setStyleSheet(
            QStringLiteral("background: #5a3c00; color: #ffe0a3; border-radius: 4px; padding: 2px 8px; font-weight: bold;"));
    }

    const std::vector<TraceEvent> &all = m_session->storedEvents();
    if (all.empty()) {
        m_position->setText(tr("No recorded stops"));
        return;
    }
    const TraceEventId selected = m_session->selectedEventId();
    int position = int(all.size()); // live-following with no explicit selection
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].id == selected) {
            position = int(i) + 1;
            break;
        }
    }
    m_position->setText(tr("Stop %1 / %2").arg(position).arg(all.size()));
}

void HistoryView::refreshStateTab()
{
    if (!m_display)
        return;
    const DisplayedDebugState state = m_display->displayedState();
    if (!state.snapshot) {
        m_stateSummary->setText(tr("No recorded state."));
        m_stateVariables->setRowCount(0);
        return;
    }
    const ExecutionSnapshot &snapshot = *state.snapshot;
    QString summary;
    if (!snapshot.function.isEmpty())
        summary += snapshot.function;
    if (!snapshot.file.isEmpty() && snapshot.line > 0) {
        if (!summary.isEmpty())
            summary += QStringLiteral(" — ");
        summary += QStringLiteral("%1:%2").arg(snapshot.file).arg(snapshot.line);
    }
    if (summary.isEmpty())
        summary = tr("Stop %1").arg(snapshot.stepIndex);
    if (!state.isLive())
        summary += tr("  (recorded values — target not rewound)");
    m_stateSummary->setText(summary);

    QStringList keys = snapshot.variableValues.keys();
    keys.sort();
    m_stateVariables->setRowCount(keys.size());
    for (int row = 0; row < keys.size(); ++row) {
        auto *name = new QTableWidgetItem(keys[row]);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        auto *value = new QTableWidgetItem(snapshot.variableValues.value(keys[row]));
        value->setFlags(value->flags() & ~Qt::ItemIsEditable);
        const bool changed = snapshot.changedPaths.contains(keys[row]);
        auto *changedItem = new QTableWidgetItem(changed ? tr("Yes") : tr("No"));
        changedItem->setFlags(changedItem->flags() & ~Qt::ItemIsEditable);
        m_stateVariables->setItem(row, 0, name);
        m_stateVariables->setItem(row, 1, value);
        m_stateVariables->setItem(row, 2, changedItem);
    }
}

void HistoryView::refreshValueTab()
{
    if (!m_debug || !m_valuePath)
        return;
    const QVector<ExecutionSnapshot> &snapshots = m_debug->executionHistory();
    const QVector<ValueHistoryPoint> points = valueHistory(snapshots, m_valuePath->text());
    m_plot->points = points;
    m_plot->update();
    m_valueTable->setRowCount(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const auto &p = points[i];
        const int step = (i >= 0 && i < snapshots.size()) ? snapshots[i].stepIndex : -1;
        auto *stop = new QTableWidgetItem(tr("Stop %1").arg(step));
        stop->setData(Qt::UserRole, step);
        stop->setFlags(stop->flags() & ~Qt::ItemIsEditable);
        auto *value = new QTableWidgetItem(p.available ? p.value : tr("<unavailable>"));
        value->setFlags(value->flags() & ~Qt::ItemIsEditable);
        auto *changed = new QTableWidgetItem(p.changed ? tr("Yes") : tr("No"));
        changed->setFlags(changed->flags() & ~Qt::ItemIsEditable);
        m_valueTable->setItem(i, 0, stop);
        m_valueTable->setItem(i, 1, value);
        m_valueTable->setItem(i, 2, changed);
    }
}

void HistoryView::refreshEventTab()
{
    if (!m_session)
        return;
    const TraceEvent *event = m_session->selectedEvent();
    auto set = [this](int row, const QString &text) {
        if (QLabel *label = detailLabel(m_eventDetails, row))
            label->setText(text);
    };
    if (!event) {
        for (int row = 0; row < 7; ++row)
            set(row, tr("—"));
        return;
    }
    set(0, formatTimeMs(event->time));
    set(1, traceEventTypeName(event->type));
    set(2, formatAddress(event->address));
    QString symbol = tr("—");
    QString source = tr("—");
    QString function = tr("—");
    if (event->source) {
        if (!event->source->function.isEmpty()) {
            symbol = event->source->function;
            function = event->source->function;
        }
        if (event->source->isValid())
            source = QStringLiteral("%1:%2").arg(event->source->file).arg(event->source->line);
    }
    if (event->address && symbol == tr("—"))
        symbol = formatAddress(event->address);
    set(3, symbol);
    set(4, source);
    set(5, function);
    QStringList meta;
    for (auto it = event->metadata.constBegin(); it != event->metadata.constEnd(); ++it)
        meta << QStringLiteral("%1=%2").arg(it.key(), it.value().toString());
    set(6, meta.isEmpty() ? tr("—") : meta.join(QStringLiteral("; ")));
}

void HistoryView::refreshAll()
{
    refreshTransport();
    refreshStateTab();
    refreshValueTab();
    refreshEventTab();
}

void HistoryView::goToStart()
{
    if (!m_session || m_session->storedEvents().empty())
        return;
    m_session->selectEvent(m_session->storedEvents().front().id);
}

void HistoryView::stepBackward()
{
    if (!m_session)
        return;
    const std::vector<TraceEvent> &all = m_session->storedEvents();
    if (all.empty())
        return;
    const TraceEventId current = m_session->selectedEventId();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].id == current) {
            if (i > 0)
                m_session->selectEvent(all[i - 1].id);
            return;
        }
    }
    m_session->selectEvent(all.back().id);
}

void HistoryView::stepForward()
{
    if (!m_session)
        return;
    const std::vector<TraceEvent> &all = m_session->storedEvents();
    if (all.empty())
        return;
    const TraceEventId current = m_session->selectedEventId();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].id == current) {
            if (i + 1 < all.size())
                m_session->selectEvent(all[i + 1].id);
            return;
        }
    }
    m_session->selectEvent(all.front().id);
}

void HistoryView::goLive()
{
    if (m_display)
        m_display->goLive();
    else if (m_session)
        m_session->goLive();
}

} // namespace history
} // namespace qddd
