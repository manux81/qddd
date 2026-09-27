#include "HistoryView.h"

#include "history/TimeMachineModel.h"

#include <QBoxLayout>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>

#include <algorithm>
#include <iterator>

namespace qddd {
namespace history {

// ============================================================================
// TimelineDelegate: hierarchical rows (marker, sequence, function, location).
// ============================================================================

TimelineDelegate::TimelineDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize TimelineDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &) const
{
    const QFontMetrics metrics(option.font);
    // Three compact text lines with breathing room: ~46px at 13px fonts.
    return {option.rect.width(), metrics.height() * 3 + 8};
}

void TimelineDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    painter->save();
    const QRect rect = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool isPresent = index.data(HistoryPointModel::IsPresentRole).toBool();
    const bool isCheckpoint =
        index.data(HistoryPointModel::IsCheckpointRole).toBool();

    if (selected)
        painter->fillRect(rect, option.palette.highlight());
    else
        painter->fillRect(rect, option.palette.base());

    constexpr int gutterWidth = 24;
    const int centerX = rect.left() + gutterWidth / 2;
    QFontMetrics metrics(option.font);
    const int lineH = metrics.height();
    const int top = rect.top() + 4;
    // Marker aligned with the middle (function) line.
    const int dotY = top + lineH + lineH / 2;

    // Vertical continuity between history points.
    painter->setPen(QPen(option.palette.mid().color(), 1));
    int lineTop = rect.top();
    int lineBottom = rect.bottom();
    if (isPresent)
        lineTop = dotY; // the line starts at the present
    if (index.row() == index.model()->rowCount() - 1)
        lineBottom = dotY; // and ends at the oldest point
    painter->drawLine(centerX, lineTop, centerX, lineBottom);

    const QColor accent = option.palette.highlight().color();
    const QColor pen = selected ? option.palette.highlightedText().color()
                               : option.palette.text().color();
    if (isPresent) {
        painter->setBrush(accent);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPoint(centerX, dotY), 5, 5);
    } else if (isCheckpoint) {
        painter->setBrush(accent);
        painter->setPen(QPen(selected ? option.palette.highlightedText().color()
                                      : accent.darker(130),
                             1));
        painter->drawEllipse(QPoint(centerX, dotY), 4, 4);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, 1));
        painter->drawEllipse(QPoint(centerX, dotY), 6, 6);
    } else {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(pen, 2));
        painter->drawEllipse(QPoint(centerX, dotY), 3, 3);
    }
    if (!isPresent && index.data(HistoryPointModel::IsSelectedRole).toBool()) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, 2));
        painter->drawEllipse(QPoint(centerX, dotY), 7, 7);
    }

    // Line 1: NOW (never numbered) or the canonical history-point id.
    const int textLeft = rect.left() + gutterWidth + 4;
    const int textRight = rect.right() - 4;
    QFont smallFont = option.font;
    smallFont.setPointSize(qMax(8, smallFont.pointSize() - 2));
    painter->setFont(smallFont);
    painter->setPen(selected ? option.palette.highlightedText().color()
                            : option.palette.placeholderText().color());
    QString seq;
    if (isPresent) {
        seq = tr("NOW");
    } else {
        seq = tr("#%1").arg(index.data(HistoryPointModel::PointIdRole).toULongLong());
        if (isCheckpoint)
            seq += tr(" · checkpoint");
    }
    painter->drawText(textLeft, top, textRight - textLeft, lineH,
                      Qt::AlignLeft | Qt::AlignVCenter, seq);

    // Line 2: function, prominent.
    QFont functionFont = option.font;
    functionFont.setBold(true);
    painter->setFont(functionFont);
    painter->setPen(pen);
    QString function = index.data(HistoryPointModel::FunctionRole).toString();
    if (function.isEmpty())
        function = index.data(Qt::DisplayRole).toString();
    QFontMetrics functionMetrics(functionFont);
    painter->drawText(textLeft, top + lineH, textRight - textLeft, lineH,
                      Qt::AlignLeft | Qt::AlignVCenter,
                      functionMetrics.elidedText(function, Qt::ElideRight,
                                               textRight - textLeft));

    // Line 3: source:line · thread, secondary.
    painter->setFont(option.font);
    painter->setPen(selected ? option.palette.highlightedText().color()
                            : option.palette.placeholderText().color());
    QString loc = index.data(HistoryPointModel::LocationRole).toString();
    const QString thread = index.data(HistoryPointModel::ThreadRole).toString();
    if (!thread.isEmpty() && thread != tr("—"))
        loc += tr(" · Thread %1").arg(thread);
    painter->drawText(textLeft, top + lineH * 2, textRight - textLeft, lineH,
                      Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(loc, Qt::ElideRight, textRight - textLeft));
    painter->restore();
}

// ============================================================================
// HistoryView: header / timeline / inspector.
// ============================================================================

HistoryView::HistoryView(HistorySession *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    setupHeader(outer);

    auto *model = new HistoryPointModel(this);
    model->setSession(session);
    m_list = new QListView(this);
    m_list->setModel(model);
    m_list->setItemDelegate(new TimelineDelegate(m_list));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setUniformItemSizes(false);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->installEventFilter(this);
    outer->addWidget(m_list, 1);

    buildInspector(outer);

    connect(model, &QAbstractItemModel::modelReset, this, [this] {
        refreshAll();
    });
    connect(session, &HistorySession::selectedEventChanged, this, [this] {
        refreshAll();
    });
    connect(session, &HistorySession::currentTimeChanged, this, [this] {
        refreshAll();
    });
    connect(session, &HistorySession::capabilitiesChanged, this, [this] {
        refreshAll();
    });
    connect(m_list, &QListView::activated, this, &HistoryView::activateRow);
    connect(m_list, &QListView::clicked, this, &HistoryView::activateRow);
    connect(m_list, &QWidget::customContextMenuRequested,
            this, &HistoryView::showContextMenu);

    refreshAll();
}

bool HistoryView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_badge && event->type() == QEvent::MouseButtonPress) {
        // The badge is the compact Return-to-Live control when historic;
        // a no-op with explanation when already live.
        navigatePresent();
        return true;
    }
    if (watched == m_list && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Left) {
            goToPrevious();
            return true;
        }
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Right) {
            goToNext();
            return true;
        }
        if (key->key() == Qt::Key_Home) {
            navigatePresent();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QToolButton *HistoryView::makeButton(const QString &text, const QString &tooltip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

void HistoryView::setController(TimeTravelController *controller)
{
    m_controller = controller;
}

void HistoryView::setupHeader(QBoxLayout *layout)
{
    // Bar styling follows the app-wide dark theme (cf. the command pill in
    // MainWindow and the Data Display overlay): flat buttons, subtle hover,
    // dim secondary text.
    auto *bar = new QWidget(this);
    bar->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 4px; "
        "padding: 3px 6px; margin: 0px; color: #d8d8d8; font-size: 13px; "
        "min-height: 22px; }"
        "QToolButton:hover { background: #333333; }"
        "QToolButton:pressed { background: #1b1b1b; }"
        "QToolButton:disabled { color: #5f5f5f; background: transparent; }"
        "QLabel#tmBadgeLive { color: #d8f3dc; background: #2f3f33; "
        "border: 1px solid #3b5141; border-radius: 4px; padding: 1px 8px; }"
        "QLabel#tmBadgeHistoric { color: #f0d9a8; background: #4a3a20; "
        "border: 1px solid #6b5426; border-radius: 4px; padding: 1px 8px; }"
        "QLabel#tmBadgeReplay { color: #dbeafe; background: #1e3a5f; "
        "border: 1px solid #3b5f8a; border-radius: 4px; padding: 1px 8px; }"
        "QLabel#tmTitle { color: #9a9a9a; font-size: 12px; }"
        "QLabel#tmCount { color: #9a9a9a; font-size: 11px; }"));
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    m_badge = new QLabel(tr("LIVE"), bar);
    m_badge->setTextFormat(Qt::PlainText);
    m_badge->setAlignment(Qt::AlignCenter);
    // The badge doubles as the Return-to-Live control: compact, always
    // visible, and unambiguous in both states.
    m_badge->setCursor(Qt::PointingHandCursor);
    m_badge->installEventFilter(this);
    row->addWidget(m_badge);
    auto *title = new QLabel(tr("Time Travel"), bar);
    title->setObjectName(QStringLiteral("tmTitle"));
    row->addWidget(title);
    row->addStretch(1);
    m_countLabel = new QLabel(bar);
    m_countLabel->setObjectName(QStringLiteral("tmCount"));
    m_countLabel->setTextFormat(Qt::PlainText);
    row->addWidget(m_countLabel);
    m_prevButton = makeButton(tr("◀"), tr("Show previous recorded stop"));
    m_nextButton = makeButton(tr("▶"), tr("Show next recorded stop"));
    row->addWidget(m_prevButton);
    row->addWidget(m_nextButton);
    layout->addWidget(bar);

    connect(m_prevButton, &QToolButton::clicked, this, &HistoryView::goToPrevious);
    connect(m_nextButton, &QToolButton::clicked, this, &HistoryView::goToNext);
}

void HistoryView::buildInspector(QBoxLayout *layout)
{
    // Bounded inspector: hugs small content, scrolls internally past the
    // cap. Timeline geometry above never depends on inspector expansion.
    m_inspectorScroll = new QScrollArea(this);
    m_inspectorScroll->setObjectName(QStringLiteral("inspectorScroll"));
    m_inspectorScroll->setFrameShape(QFrame::NoFrame);
    m_inspectorScroll->setWidgetResizable(true);
    m_inspectorScroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inspectorScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_inspectorScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_inspectorScroll->setMaximumHeight(300);
    auto *box = new QWidget(m_inspectorScroll);
    m_inspectorScroll->setWidget(box);
    layout->addWidget(m_inspectorScroll);
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    boxLayout->setSpacing(3);

    m_titleLabel = new QLabel(box);
    m_titleLabel->setTextFormat(Qt::PlainText);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    boxLayout->addWidget(m_titleLabel);

    m_functionLabel = new QLabel(box);
    m_functionLabel->setTextFormat(Qt::PlainText);
    m_functionLabel->setWordWrap(true);
    QFont functionFont = m_functionLabel->font();
    functionFont.setPointSize(functionFont.pointSize() + 2);
    functionFont.setBold(true);
    m_functionLabel->setFont(functionFont);
    boxLayout->addWidget(m_functionLabel);

    m_locationLabel = new QLabel(box);
    m_locationLabel->setTextFormat(Qt::PlainText);
    m_locationLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    boxLayout->addWidget(m_locationLabel);

    m_hintLabel = new QLabel(box);
    m_hintLabel->setTextFormat(Qt::PlainText);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    boxLayout->addWidget(m_hintLabel);

    m_changesTitle = new QLabel(tr("Changes"), box);
    QFont changesFont = m_changesTitle->font();
    changesFont.setBold(true);
    m_changesTitle->setFont(changesFont);
    boxLayout->addWidget(m_changesTitle);
    m_changesSeparator = new QFrame(box);
    m_changesSeparator->setFrameShape(QFrame::HLine);
    m_changesSeparator->setFrameShadow(QFrame::Plain);
    m_changesSeparator->setStyleSheet(QStringLiteral("color: palette(mid);"));
    boxLayout->addWidget(m_changesSeparator);
    m_changesScroll = new QScrollArea(box);
    m_changesScroll->setObjectName(QStringLiteral("changesScroll"));
    m_changesScroll->setFrameShape(QFrame::NoFrame);
    m_changesScroll->setWidgetResizable(true);
    m_changesScroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_changesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_changesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_changesScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *changesInner = new QWidget(m_changesScroll);
    m_changesBox = new QVBoxLayout(changesInner);
    m_changesBox->setContentsMargins(0, 0, 0, 0);
    m_changesBox->setSpacing(2);
    m_changesScroll->setWidget(changesInner);
    boxLayout->addWidget(m_changesScroll);
    m_showAllButton = new QToolButton(box);
    m_showAllButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_showAllButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: palette(highlight); "
        "font-size: 11px; text-align: left; }"));
    connect(m_showAllButton, &QToolButton::clicked, this, &HistoryView::toggleShowAllChanges);
    boxLayout->addWidget(m_showAllButton);

    m_capturedButton = new QToolButton(box);
    m_capturedButton->setCheckable(true);
    m_capturedButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_capturedButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; font-size: 12px; text-align: left; }"));
    connect(m_capturedButton, &QToolButton::clicked, this, &HistoryView::toggleCaptured);
    boxLayout->addWidget(m_capturedButton);
    m_filterEdit = new QLineEdit(box);
    m_filterEdit->setPlaceholderText(tr("Filter values…"));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &HistoryView::filterCaptured);
    boxLayout->addWidget(m_filterEdit);
    m_capturedTable = new QTableWidget(box);
    m_capturedTable->setColumnCount(2);
    m_capturedTable->setHorizontalHeaderLabels({tr("Path"), tr("Value")});
    m_capturedTable->horizontalHeader()->setStretchLastSection(true);
    m_capturedTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_capturedTable->verticalHeader()->setVisible(false);
    m_capturedTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_capturedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_capturedTable->setShowGrid(false);
    m_capturedTable->setAlternatingRowColors(true);
    m_capturedTable->setMaximumHeight(160);
    boxLayout->addWidget(m_capturedTable);

    m_detailsButton = new QToolButton(box);
    m_detailsButton->setCheckable(true);
    m_detailsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_detailsButton->setStyleSheet(m_capturedButton->styleSheet());
    connect(m_detailsButton, &QToolButton::clicked, this, &HistoryView::toggleDetails);
    boxLayout->addWidget(m_detailsButton);
    m_detailsBox = new QWidget(box);
    auto *detailsLayout = new QVBoxLayout(m_detailsBox);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(2);
    boxLayout->addWidget(m_detailsBox);
}

void HistoryView::clearLayout(QLayout *layout)
{
    QLayoutItem *item = nullptr;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->layout())
            clearLayout(item->layout());
        delete item->widget();
        delete item;
    }
}

HistoryView::InspectorData HistoryView::collectInspectorData() const
{
    InspectorData data;
    if (!m_session)
        return data;
    data.live = m_session->isLive();
    if (data.live) {
        // Live inspector describes the present target state.
        const auto &events = m_session->storedEvents();
        const TraceEvent *latest = events.empty() ? nullptr : &events.back();
        std::optional<SourceLocation> loc;
        if (latest && latest->source)
            loc = latest->source;
        else if (const auto snapshot = latest
                     ? m_session->snapshotForEvent(latest->id)
                     : std::optional<ExecutionSnapshot>{}) {
            if (!snapshot->function.isEmpty() || !snapshot->file.isEmpty()) {
                SourceLocation fallback;
                fallback.file = snapshot->file;
                fallback.line = snapshot->line;
                fallback.function = snapshot->function;
                loc = fallback;
            }
        }
        if (loc && (!loc->function.isEmpty() || !loc->file.isEmpty())) {
            data.function = loc->function;
            if (!loc->file.isEmpty() && loc->line > 0)
                data.location = QFileInfo(loc->file).fileName()
                    + QStringLiteral(":%1").arg(loc->line);
        }
        data.liveHint = tr("Current target state.\n"
                           "Select a recorded stop to inspect its captured state.");
        return data;
    }

    const TraceEvent *event = m_session->selectedEvent();
    if (!event)
        return data;
    data.live = false;
    if (const ExecutionHistoryPoint *point = m_session->historyPoint(event->id))
        data.title = tr("#%1 · %2").arg(point->id).arg(
            m_session->capabilities().testFlag(HistoryCapability::Seek) ? tr("REPLAY")
                                                                        : tr("SNAPSHOT"));
    const auto snapshot = m_session->snapshotForEvent(event->id);
    data.hasSnapshot = snapshot.has_value();
    if (snapshot) {
        data.function = snapshot->function;
        if (!snapshot->file.isEmpty() && snapshot->line > 0) {
            data.location = QFileInfo(snapshot->file).fileName()
                + QStringLiteral(":%1").arg(snapshot->line);
            if (!snapshot->threadId.isEmpty())
                data.location += tr(" · Thread %1").arg(snapshot->threadId);
        } else if (!snapshot->threadId.isEmpty()) {
            data.location = tr("Thread %1").arg(snapshot->threadId);
        }
        data.values = snapshot->variableValues;

        // "What changed here": prefer the session-computed diff so every
        // view agrees on the definition of changed. Snapshots whose backend
        // did not precompute one fall back to a direct union comparison.
        if (!snapshot->changedPaths.isEmpty()) {
            // Previous recorded snapshot for old values.
            QHash<QString, QString> before;
            const auto &events = m_session->storedEvents();
            for (auto it = events.begin(); it != events.end(); ++it) {
                if (it->id != event->id)
                    continue;
                for (auto back = std::make_reverse_iterator(it);
                     back != events.rend(); ++back) {
                    if (back->id == event->id)
                        continue;
                    if (const auto previous = m_session->snapshotForEvent(back->id)) {
                        before = previous->variableValues;
                        break;
                    }
                }
                break;
            }
            QStringList keys = snapshot->changedPaths.values();
            keys.sort();
            for (const QString &key : keys) {
                HistoryChange change;
                change.path = key;
                change.hadBefore = before.contains(key);
                change.hasNow = snapshot->variableValues.contains(key);
                change.oldValue = change.hadBefore ? before.value(key) : tr("—");
                change.newValue = change.hasNow ? snapshot->variableValues.value(key) : tr("—");
                data.changes.append(change);
            }
        } else {
            data.changes = computeHistoryChanges(m_session, event->id).changes;
        }
        data.totalChanges = data.changes.size();
    }
    return data;
}

void HistoryView::rebuildInspector(const InspectorData &data)
{
    // Title.
    if (data.live) {
        m_titleLabel->setText(tr("LIVE"));
        m_titleLabel->show();
    } else if (!data.title.isEmpty()) {
        m_titleLabel->setText(data.title);
        m_titleLabel->show();
    } else {
        m_titleLabel->hide();
    }

    // Function + location share one visual block; hide what is missing.
    if (data.function.isEmpty() && data.location.isEmpty()) {
        m_functionLabel->hide();
        m_locationLabel->hide();
    } else {
        m_functionLabel->setText(data.function.isEmpty() ? tr("—") : data.function);
        m_functionLabel->show();
        m_locationLabel->setText(data.location);
        m_locationLabel->setVisible(!data.location.isEmpty());
    }
    m_hintLabel->setText(data.liveHint);
    m_hintLabel->setVisible(!data.liveHint.isEmpty());

    // Changes: inline up to 5, toggle for the rest, honest empty states.
    // The scroll region caps show-all growth so expanding it scrolls
    // internally instead of crushing the timeline above.
    clearLayout(m_changesBox);
    const bool showChangesSection = !data.live;
    m_changesTitle->setVisible(showChangesSection);
    m_changesSeparator->setVisible(showChangesSection);
    m_changesScroll->setVisible(showChangesSection);
    const int changeRowHeight = fontMetrics().height() + 2;
    m_changesScroll->setMaximumHeight(6 * changeRowHeight + 4);
    if (showChangesSection) {
        if (!data.hasSnapshot) {
            auto *none = new QLabel(tr("No values were captured for this stop."), this);
            none->setStyleSheet(QStringLiteral("color: palette(mid);"));
            none->setWordWrap(true);
            m_changesBox->addWidget(none);
            m_showAllButton->hide();
        } else if (data.changes.isEmpty()) {
            auto *none = new QLabel(tr("No captured values changed at this stop."), this);
            none->setStyleSheet(QStringLiteral("color: palette(mid);"));
            none->setWordWrap(true);
            m_changesBox->addWidget(none);
            m_showAllButton->hide();
        } else {
            const int shown =
                (m_showAllChanges ? data.changes.size() : qMin(5, data.changes.size()));
            for (int i = 0; i < shown; ++i) {
                const HistoryChange &change = data.changes[i];
                auto *row = new QHBoxLayout();
                row->setContentsMargins(0, 0, 0, 0);
                row->setSpacing(6);
                auto *path = new QLabel(change.path, this);
                path->setTextInteractionFlags(Qt::TextSelectableByMouse);
                row->addWidget(path, 1);
                auto *old = new QLabel(change.oldValue, this);
                old->setStyleSheet(QStringLiteral("color: palette(mid);"));
                old->setTextInteractionFlags(Qt::TextSelectableByMouse);
                row->addWidget(old);
                auto *arrow = new QLabel(tr("→"), this);
                arrow->setStyleSheet(QStringLiteral("color: palette(mid);"));
                row->addWidget(arrow);
                auto *current = new QLabel(change.newValue, this);
                current->setTextInteractionFlags(Qt::TextSelectableByMouse);
                row->addWidget(current);
                // Per-variable causal step: walk to the previous snapshot
                // where this value differs. Snapshot fallback only — an
                // exact write needs a recording backend (see tooltip).
                auto *prevChange = new QToolButton(this);
                prevChange->setText(tr("‹ Change"));
                prevChange->setToolButtonStyle(Qt::ToolButtonTextOnly);
                prevChange->setStyleSheet(QStringLiteral(
                    "QToolButton { background: transparent; border: none; "
                    "color: palette(highlight); font-size: 11px; }"));
                const bool exactWrites =
                    m_session && m_session->capabilities().testFlag(
                                    HistoryCapability::MemoryWriteHistory);
                prevChange->setToolTip(
                    exactWrites
                        ? tr("Find the exact previous write of %1").arg(change.path)
                        : tr("Find the previous snapshot where %1 differs "
                             "(snapshot fallback — exact writes need a recording backend)")
                              .arg(change.path));
                const QString expression = change.path;
                connect(prevChange, &QToolButton::clicked, this,
                        [this, expression] {
                            const TraceEventId id =
                                m_controller
                                    ? m_controller->findPreviousChange(expression)
                                    : qddd::history::findPreviousChange(m_session,
                                                                        expression);
                            // Controller already selected; direct fallback
                            // inspects without moving the target.
                            if (!m_controller && id != InvalidTraceEventId && m_session)
                                m_session->selectEvent(id);
                        });
                row->addWidget(prevChange);
                m_changesBox->addLayout(row);
            }
            if (data.totalChanges > 5) {
                m_showAllButton->setVisible(true);
                m_showAllButton->setChecked(m_showAllChanges);
                m_showAllButton->setText(
                    m_showAllChanges
                        ? tr("Show fewer changes")
                        : tr("Show all %1 changes").arg(data.totalChanges));
            } else {
                m_showAllButton->hide();
            }
        }
    } else {
        m_showAllButton->hide();
    }

    // Captured values: one collapsed row, expandable with filter + table.
    if (!data.live && data.hasSnapshot && !data.values.isEmpty()) {
        m_capturedButton->setVisible(true);
        m_capturedButton->setChecked(m_capturedExpanded);
        m_capturedButton->setText(
            tr("Captured values  %1  %2")
                .arg(data.values.size())
                .arg(m_capturedExpanded ? QStringLiteral("˅") : QStringLiteral("›")));
        m_capturedButton->setToolTip(
            tr("Values recorded at this stop. The target was not rewound."));
        m_filterEdit->setVisible(m_capturedExpanded);
        m_capturedTable->setVisible(m_capturedExpanded);
        if (m_capturedExpanded)
            filterCaptured(m_filterEdit->text());
    } else if (!data.live && data.hasSnapshot) {
        m_capturedButton->hide();
        m_filterEdit->hide();
        m_capturedTable->hide();
        auto *none = new QLabel(tr("No values were captured for this stop."), this);
        none->setStyleSheet(QStringLiteral("color: palette(mid);"));
        // Reuse the changes area anchor: append under the last section.
        m_changesBox->addWidget(none);
        m_changesSeparator->setVisible(showChangesSection);
    } else {
        m_capturedButton->hide();
        m_filterEdit->hide();
        m_capturedTable->hide();
    }

    // Technical details stay collapsed and secondary.
    if (!data.live && data.hasSnapshot) {
        m_detailsButton->setVisible(true);
        m_detailsButton->setChecked(m_detailsExpanded);
        m_detailsButton->setText(tr("More details  %1").arg(
            m_detailsExpanded ? QStringLiteral("˅") : QStringLiteral("›")));
        m_detailsBox->setVisible(m_detailsExpanded);
        if (m_detailsExpanded)
            rebuildDetails(data);
    } else {
        m_detailsButton->hide();
        m_detailsBox->hide();
    }
}

void HistoryView::rebuildDetails(const InspectorData &data)
{
    Q_UNUSED(data);
    clearLayout(static_cast<QBoxLayout *>(m_detailsBox->layout()));
    if (!m_session)
        return;
    const TraceEvent *event = m_session->selectedEvent();
    if (!event)
        return;
    auto *detailsLayout = static_cast<QBoxLayout *>(m_detailsBox->layout());
    auto addRow = [&](const QString &key, const QString &value) {
        auto *label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setText(QStringLiteral("%1  %2").arg(key, value));
        label->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
        detailsLayout->addWidget(label);
    };
    if (event->time != InvalidTime) {
        const double ms = event->time / 1e6;
        const TimeRange full = m_session->fullRange();
        QString stamp = tr("Timestamp  %1 ms").arg(ms, 0, 'f', 3);
        if (full.isValid())
            stamp += tr("  (%1 ms ago)").arg((full.end - event->time) / 1e6, 0, 'f', 0);
        addRow(QString(), stamp);
    }
    if (event->address)
        addRow(tr("PC"), QStringLiteral("0x%1").arg(*event->address, 0, 16).toUpper());
    if (const auto snapshot = m_session->snapshotForEvent(event->id)) {
        if (!snapshot->function.isEmpty())
            addRow(tr("Symbol"), snapshot->function);
        addRow(tr("Frame"), QString::number(snapshot->frame));
        if (!snapshot->threadId.isEmpty())
            addRow(tr("Thread"), snapshot->threadId);
    } else if (event->source && !event->source->function.isEmpty()) {
        addRow(tr("Symbol"), event->source->function);
    }
    const QStringList caps = historyCapabilityNames(m_session->capabilities());
    addRow(tr("Backend"), caps.isEmpty() ? tr("stop snapshots only") : caps.join(", "));
    addRow(tr("History ID"), QString::number(event->id));
}

void HistoryView::filterCaptured(const QString &text)
{
    if (!m_session || !m_capturedTable->isVisible())
        return;
    const TraceEvent *event = m_session->selectedEvent();
    if (!event)
        return;
    const auto snapshot = m_session->snapshotForEvent(event->id);
    if (!snapshot)
        return;
    QStringList keys(snapshot->variableValues.keys());
    keys.sort();
    const QString needle = text.trimmed();
    m_capturedTable->setRowCount(0);
    int row = 0;
    for (const QString &key : keys) {
        if (!needle.isEmpty() && !key.contains(needle, Qt::CaseInsensitive))
            continue;
        m_capturedTable->insertRow(row);
        auto *path = new QTableWidgetItem(key);
        path->setFlags(path->flags() & ~Qt::ItemIsEditable);
        auto *value = new QTableWidgetItem(snapshot->variableValues.value(key));
        value->setFlags(value->flags() & ~Qt::ItemIsEditable);
        m_capturedTable->setItem(row, 0, path);
        m_capturedTable->setItem(row, 1, value);
        ++row;
    }
}

void HistoryView::refreshAll()
{
    if (!m_session)
        return;

    // Explicit temporal badge: text first, never color-only.
    const bool isLive = m_session->isLive();
    TemporalState temporal = TemporalState::Historic;
    if (isLive) {
        temporal = TemporalState::Live;
    } else if (m_session->capabilities().testFlag(HistoryCapability::Seek)) {
        temporal = TemporalState::Replayed;
    }
    HistoryPointId pointId = InvalidHistoryPointId;
    if (!isLive) {
        if (const ExecutionHistoryPoint *point =
                m_session->historyPoint(m_session->selectedEventId())) {
            pointId = point->id;
        }
    }
    const QString badgeText =
        temporal == TemporalState::Live
            ? tr("LIVE")
            : (temporal == TemporalState::Replayed ? tr("REPLAY") : tr("SNAPSHOT"));
    m_badge->setText(badgeText);
    m_badge->setObjectName(temporal == TemporalState::Live
                               ? QStringLiteral("tmBadgeLive")
                               : temporal == TemporalState::Replayed
                                     ? QStringLiteral("tmBadgeReplay")
                                     : QStringLiteral("tmBadgeHistoric"));
    // Re-apply the object-name-scoped style.
    m_badge->style()->unpolish(m_badge);
    m_badge->style()->polish(m_badge);
    QString tip = temporalBadgeText(temporal, pointId);
    if (temporal != TemporalState::Live)
        tip += tr(" — stored debugger state. Execution has not been rewound. "
                  "Click to return to live.");
    else
        tip += tr(" — already at the present.");
    m_badge->setToolTip(tip);

    const int count = int(m_session->storedEvents().size());
    m_countLabel->setText(count == 1 ? tr("1 stop") : tr("%1 stops").arg(count));

    // Shared capability gating: one decision point for transport buttons.
    auto apply = [this](TimeMachineAction action, QToolButton *button) {
        const TimeMachineActionState actionState =
            timeMachineActionState(action, m_session, nullptr);
        button->setEnabled(actionState.enabled);
        if (!actionState.enabled && !actionState.reason.isEmpty())
            button->setToolTip(actionState.reason);
    };
    apply(TimeMachineAction::Previous, m_prevButton);
    apply(TimeMachineAction::Next, m_nextButton);

    // Keep list selection tracking the session selection.
    if (auto *pointModel = qobject_cast<HistoryPointModel *>(m_list->model())) {
        const int row = isLive ? 0 : pointModel->rowForPoint(m_session->selectedEventId());
        if (row >= 0) {
            const QModelIndex current = pointModel->index(qMax(0, row), 0);
            if (m_list->currentIndex() != current) {
                m_list->setCurrentIndex(current);
                m_list->scrollTo(current, QAbstractItemView::EnsureVisible);
            }
        }
    }

    rebuildInspector(collectInspectorData());
}

void HistoryView::activateRow(const QModelIndex &index)
{
    if (!m_session || !index.isValid())
        return;
    auto *pointModel = qobject_cast<HistoryPointModel *>(m_list->model());
    if (!pointModel)
        return;
    if (pointModel->isPresentRow(index.row())) {
        if (m_controller)
            m_controller->showPresent();
        else
            m_session->returnToPresent();
        return;
    }
    const TraceEventId id = pointModel->pointIdAtRow(index.row());
    if (id == InvalidTraceEventId)
        return;
    if (m_controller)
        m_controller->showRecordedPosition(id);
    else
        m_session->seekToHistoryPoint(id);
}

void HistoryView::showContextMenu(const QPoint &pos)
{
    if (!m_session)
        return;
    const QModelIndex index = m_list->indexAt(pos);
    const bool onPoint = index.isValid() && !qobject_cast<HistoryPointModel *>(
                                                   m_list->model())->isPresentRow(index.row());
    const TraceEventId id = onPoint ? qobject_cast<HistoryPointModel *>(
                                          m_list->model())->pointIdAtRow(index.row())
                                    : InvalidTraceEventId;
    QMenu menu(this);
    auto gated = [&](TimeMachineAction action, const QString &text) {
        const TimeMachineActionState actionState =
            timeMachineActionState(action, m_session, nullptr);
        QAction *act = menu.addAction(text);
        act->setEnabled(actionState.enabled);
        if (!actionState.enabled && !actionState.reason.isEmpty())
            act->setToolTip(actionState.reason);
        return act;
    };
    QAction *prev = gated(TimeMachineAction::Previous, tr("Previous stop"));
    QAction *next = gated(TimeMachineAction::Next, tr("Next stop"));
    QAction *present = gated(TimeMachineAction::ReturnToPresent, tr("Return to present"));
    menu.addSeparator();
    QAction *checkpoint = gated(TimeMachineAction::MakeCheckpoint, tr("Create checkpoint here"));

    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == prev)
        navigatePrevious();
    else if (chosen == next)
        navigateNext();
    else if (chosen == present)
        navigatePresent();
    else if (chosen == checkpoint && id != InvalidTraceEventId)
        m_session->createCheckpoint(id);
}

// Single navigation funnel: every user-initiated move in this view goes
// through the controller when one is attached.
void HistoryView::navigatePrevious()
{
    if (m_controller)
        m_controller->showPrevious();
    else if (m_session)
        m_session->selectPreviousEvent();
}

void HistoryView::navigateNext()
{
    if (m_controller)
        m_controller->showNext();
    else if (m_session)
        m_session->selectNextEvent();
}

void HistoryView::navigatePresent()
{
    if (m_controller)
        m_controller->showPresent();
    else if (m_session)
        m_session->returnToPresent();
}

void HistoryView::goToPrevious()
{
    navigatePrevious();
}

void HistoryView::goToNext()
{
    navigateNext();
}

void HistoryView::toggleCaptured()
{
    m_capturedExpanded = !m_capturedExpanded;
    refreshAll();
}

void HistoryView::toggleDetails()
{
    m_detailsExpanded = !m_detailsExpanded;
    refreshAll();
}

void HistoryView::toggleShowAllChanges()
{
    m_showAllChanges = !m_showAllChanges;
    refreshAll();
}

} // namespace history
} // namespace qddd
