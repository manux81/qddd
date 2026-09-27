#include "HistoryView.h"

#include "history/FlowModel.h"

#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitter>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

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

} // namespace

// ============================================================================
// HistoryTimelineWidget
// ============================================================================

HistoryTimelineWidget::HistoryTimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumHeight(220);
    setToolTip(tr("Click an event to select it; click empty space to move the time cursor."));
}

void HistoryTimelineWidget::setSession(HistorySession *session)
{
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    rebuildDefaultTracks();
    m_followLive = true;
    m_viewStart = InvalidTime;
    if (!m_session)
        return;
    connect(m_session, &HistorySession::eventsAppended, this, [this] {
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

void HistoryTimelineWidget::rebuildDefaultTracks()
{
    m_tracks.clear();
    if (!m_session)
        return;
    const std::vector<TraceEvent> *store = &m_session->storedEvents();
    struct Builtin {
        const char *id;
        const char *name;
        int order;
    };
    const Builtin builtins[] = {
        {TrackIds::Cpu, "CPU", 0},
        {TrackIds::Interrupts, "Interrupts", 1},
        {TrackIds::Exceptions, "Exceptions", 2},
        {TrackIds::Breakpoints, "Breakpoints", 3},
        {TrackIds::Watchpoints, "Watchpoints", 4},
        {TrackIds::Memory, "Memory", 5},
        {TrackIds::Peripherals, "Peripherals", 6},
        {TrackIds::UserEvents, "User Events", 7},
    };
    for (const Builtin &builtin : builtins) {
        m_tracks.push_back(std::make_unique<FilterTrackProvider>(
            QString::fromLatin1(builtin.id), tr(builtin.name), builtin.order, store));
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

    // Track rows.
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
// HistoryView
// ============================================================================

HistoryView::HistoryView(HistorySession *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    auto *preview = new QLabel(
        tr("Preview — selecting history never disturbs the live target."), this);
    preview->setStyleSheet(QStringLiteral("color: palette(mid); font-style: italic;"));
    outer->addWidget(preview);

    setupTransport(outer);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    m_timeline = new HistoryTimelineWidget(splitter);
    m_timeline->setSession(session);
    splitter->addWidget(m_timeline);

    auto *bottom = new QWidget(splitter);
    auto *bottomLayout = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    setupDetails(bottomLayout);
    splitter->addWidget(bottom);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    outer->addWidget(splitter, 1);

    connect(session, &HistorySession::capabilitiesChanged, this,
            &HistoryView::refreshControls);
    connect(session, &HistorySession::eventsAppended, this,
            &HistoryView::refreshControls);
    connect(session, &HistorySession::eventsAppended, this,
            &HistoryView::refreshFlowPlaceholder);
    connect(session, &HistorySession::selectedEventChanged, this,
            &HistoryView::refreshDetails);
    connect(session, &HistorySession::currentTimeChanged, this,
            &HistoryView::refreshDetails);
    connect(m_timeline, &HistoryTimelineWidget::eventClicked, this,
            &HistoryView::refreshDetails);
    connect(m_timeline, &HistoryTimelineWidget::timeClicked, this,
            &HistoryView::refreshDetails);

    refreshControls();
    refreshDetails();
    refreshFlowPlaceholder();
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
    // U+23EE, U+25C0 etc. render without icon assets.
    m_jumpStart = makeButton(tr("⏮"), tr("Jump to start (oldest recorded stop)"));
    m_stepBack = makeButton(tr("◀"), tr("Select previous event"));
    m_stepFwd = makeButton(tr("▶"), tr("Select next event"));
    m_contBack = makeButton(tr("⏪"), tr("Continue backward (needs reverse-continue support)"));
    m_contFwd = makeButton(tr("⏩"), tr("Continue forward (needs reverse-continue support)"));
    m_jumpEnd = makeButton(tr("⏭"), tr("Jump to end / live"));
    row->addWidget(m_jumpStart);
    row->addWidget(m_stepBack);
    row->addWidget(m_stepFwd);
    row->addWidget(m_contBack);
    row->addWidget(m_contFwd);
    row->addWidget(m_jumpEnd);
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    row->addWidget(m_status, 1);
    layout->addLayout(row);

    connect(m_jumpStart, &QToolButton::clicked, this, &HistoryView::goToStart);
    connect(m_stepBack, &QToolButton::clicked, this, &HistoryView::stepBackward);
    connect(m_stepFwd, &QToolButton::clicked, this, &HistoryView::stepForward);
    connect(m_contBack, &QToolButton::clicked, this, &HistoryView::continueBackward);
    connect(m_contFwd, &QToolButton::clicked, this, &HistoryView::continueForward);
    connect(m_jumpEnd, &QToolButton::clicked, this, &HistoryView::goToEnd);
}

void HistoryView::setupDetails(QBoxLayout *layout)
{
    m_details = new QWidget(this);
    auto *form = new QFormLayout(m_details);
    form->setContentsMargins(4, 4, 4, 4);
    form->addRow(tr("Time:"), new QLabel(tr("—"), m_details));
    form->addRow(tr("Type:"), new QLabel(tr("—"), m_details));
    form->addRow(tr("PC / address:"), new QLabel(tr("—"), m_details));
    form->addRow(tr("Symbol:"), new QLabel(tr("—"), m_details));
    form->addRow(tr("Source:"), new QLabel(tr("—"), m_details));
    form->addRow(tr("Function:"), new QLabel(tr("—"), m_details));
    form->addRow(tr("Metadata:"), new QLabel(tr("—"), m_details));
    m_details->setLayout(form);
    layout->addWidget(m_details, 3);

    m_flowTree = new QTreeWidget(this);
    m_flowTree->setHeaderLabels({tr("Execution path (prototype)")});
    layout->addWidget(m_flowTree, 2);
}

static QLabel *detailLabel(QWidget *details, int row)
{
    auto *form = qobject_cast<QFormLayout *>(details->layout());
    if (!form)
        return nullptr;
    auto *item = form->itemAt(row, QFormLayout::FieldRole);
    return qobject_cast<QLabel *>(item ? item->widget() : nullptr);
}

void HistoryView::refreshControls()
{
    if (!m_session)
        return;
    const bool hasEvents = !m_session->storedEvents().empty();
    const HistoryCapabilities caps = m_session->capabilities();
    const bool reverseContinue = caps.testFlag(HistoryCapability::ReverseContinue);
    const bool canStep = caps.testFlag(HistoryCapability::ReverseStep);

    m_jumpStart->setEnabled(hasEvents);
    m_jumpEnd->setEnabled(hasEvents);
    // Steps move the target when a reverse backend exists, otherwise they
    // navigate the recorded stops. Either way they need history.
    m_stepBack->setEnabled(hasEvents);
    m_stepFwd->setEnabled(hasEvents);
    if (canStep) {
        m_stepBack->setToolTip(tr("Step backward (reverse execution)"));
        m_stepFwd->setToolTip(tr("Step forward (reverse execution)"));
    } else {
        m_stepBack->setToolTip(
            tr("Select previous event (no reverse-step backend; target is not moved)"));
        m_stepFwd->setToolTip(
            tr("Select next event (no reverse-step backend; target is not moved)"));
    }
    m_contBack->setEnabled(hasEvents && reverseContinue);
    m_contFwd->setEnabled(hasEvents && reverseContinue);
    if (!reverseContinue) {
        const QString tip = tr("Disabled: the active backend has no reverse-continue support.");
        m_contBack->setToolTip(tip);
        m_contFwd->setToolTip(tip);
    }

    const int count = int(m_session->storedEvents().size());
    QString text = tr("%1 recorded stops").arg(count);
    if (!m_session->isLive())
        text += tr(" — viewing history");
    else if (count > 0)
        text += tr(" — live");
    const QStringList capNames = historyCapabilityNames(m_session->capabilities());
    text += tr(" — backend: %1")
                .arg(capNames.isEmpty() ? tr("stop recording only") : capNames.join(QStringLiteral(", ")));
    m_status->setText(text);
}

void HistoryView::refreshDetails()
{
    if (!m_session)
        return;
    const TraceEvent *event = m_session->selectedEvent();
    auto set = [this](int row, const QString &text) {
        if (QLabel *label = detailLabel(m_details, row))
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

void HistoryView::refreshFlowPlaceholder()
{
    if (!m_flowTree || !m_session)
        return;
    m_flowTree->clear();
    const std::unique_ptr<CallNode> root = buildCallTree(m_session->storedEvents());
    if (root->children.empty()) {
        auto *item = new QTreeWidgetItem(
            m_flowTree, {tr("No function-entry data — requires an instruction-history backend.")});
        item->setDisabled(true);
        return;
    }
    std::function<void(QTreeWidgetItem *, const CallNode &)> add =
        [&add](QTreeWidgetItem *parent, const CallNode &node) {
            for (const auto &child : node.children) {
                auto *item = new QTreeWidgetItem(parent, {child->function});
                add(item, *child);
            }
        };
    for (const auto &child : root->children) {
        auto *item = new QTreeWidgetItem(m_flowTree, {child->function});
        add(item, *child);
    }
    m_flowTree->expandAll();
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
    if (m_session->capabilities().testFlag(HistoryCapability::ReverseStep)) {
        if (HistoryBackend *backend = m_session->backend())
            backend->stepBackward();
        return;
    }
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
    if (m_session->capabilities().testFlag(HistoryCapability::ReverseStep)) {
        if (HistoryBackend *backend = m_session->backend())
            backend->stepForward();
        return;
    }
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

void HistoryView::continueBackward()
{
    if (!m_session)
        return;
    if (HistoryBackend *backend = m_session->backend())
        backend->continueBackward();
}

void HistoryView::continueForward()
{
    if (!m_session)
        return;
    if (HistoryBackend *backend = m_session->backend())
        backend->continueForward();
}

void HistoryView::goToEnd()
{
    if (m_session)
        m_session->goLive();
}

} // namespace history
} // namespace qddd
