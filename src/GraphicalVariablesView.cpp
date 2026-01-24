/*
 * Copyright (c) [2026], Manuele Conti
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Manuele Conti nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "GraphicalVariablesView.h"
#include "DebugSession.h"

#include <cmath>
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>


// =======================================================
// Helpers
// =======================================================

static constexpr int SocketRadius = 4;
static constexpr int SocketOffset = -6;
static constexpr int IndentStep = 14;


// =======================================================
// Internal visible-row structure
// =======================================================

struct VisibleRow
{
    VarNode* node = nullptr;
    int indent = 0;
};


static void buildRowsRecursive(
    VarNode* node,
    int indent,
    QHash<VarNode*, bool>& expanded,
    QVector<VisibleRow>& out)
{
    out.push_back({ node, indent });

    if (!node->hasChildren)
        return;

    if (!expanded.value(node, false))
        return;

    for (VarNode* c : node->children)
        buildRowsRecursive(c, indent + 1, expanded, out);
}

static void collectPointerChildren(
    VarNode* parent,
    QVector<VarNode*>& out)
{
    for (VarNode* c : parent->children) {

        if (c->isPointer)
            out.push_back(c);

        if (c->hasChildren)
            collectPointerChildren(c, out);
    }
}


static void buildPointerEdges(
    const QHash<VarNode*, GraphicalNodeItem*>& nodeMap,
    const QHash<quintptr, GraphicalNodeItem*>& addrMap,
    QGraphicsScene* scene)
{
    for (auto it = nodeMap.begin(); it != nodeMap.end(); ++it) {

        VarNode* rootNode = it.key();
        GraphicalNodeItem* rootItem = it.value();

        QVector<VarNode*> pointerFields;
        collectPointerChildren(rootNode, pointerFields);

        for (VarNode* field : pointerFields) {

            bool ok = false;
            quintptr ptr =
                field->value.toULongLong(&ok, 16);

            if (!ok || ptr == 0)
                continue;

            if (!addrMap.contains(ptr))
                continue;

            GraphicalNodeItem* dstItem = addrMap[ptr];

            auto* edge =
                new GraphicalEdgeItem(rootItem, dstItem);

            scene->addItem(edge);

            rootItem->addEdge(edge);
            dstItem->addEdge(edge);

            edge->updatePosition();
        }
    }
}



// =======================================================
// GraphicalNodeItem
// =======================================================

GraphicalNodeItem::GraphicalNodeItem(VarNode* node)
    : m_node(node)
{
    setFlag(ItemIsSelectable);
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);

    recalculateWidth();
}

VarNode* GraphicalNodeItem::node() const
{
    return m_node;
}


QRectF GraphicalNodeItem::boundingRect() const
{
    QVector<VisibleRow> rows;

    for (VarNode* c : m_node->children)
        buildRowsRecursive(c, 0,
                           const_cast<QHash<VarNode*, bool>&>(m_expanded),
                           rows);

    int height =
        HeaderHeight +
        rows.size() * RowHeight;

    return QRectF(
        -8,
        0,
        m_width + 16,
        height
    );
}


// -------------------------------------------------------

void GraphicalNodeItem::paint(QPainter* p,
                              const QStyleOptionGraphicsItem*,
                              QWidget*)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = boundingRect().adjusted(8, 0, -8, 0);
    drawHeader(p, r);
    drawSource(p);
}


// -------------------------------------------------------

void GraphicalNodeItem::drawHeader(QPainter* p, const QRectF& r)
{
    const qreal radius = 10.0;

    QRectF header(
        r.left(),
        r.top(),
        r.width(),
        HeaderHeight
    );

    QColor base = m_node->parent == nullptr
        ? QColor(150, 110, 50)
        : QColor(80, 80, 80);

    QLinearGradient grad(
        header.topLeft(),
        header.bottomLeft());

    grad.setColorAt(0.0, base.lighter(120));
    grad.setColorAt(1.0, base.darker(120));

    QPainterPath path;

    path.moveTo(header.left(), header.bottom());
    path.lineTo(header.left(), header.top() + radius);

    path.quadTo(
        header.topLeft(),
        header.topLeft() + QPointF(radius, 0)
    );

    path.lineTo(header.right() - radius, header.top());

    path.quadTo(
        header.topRight(),
        header.topRight() + QPointF(0, radius)
    );

    path.lineTo(header.right(), header.bottom());
    path.closeSubpath();

    p->setPen(Qt::NoPen);
    p->setBrush(grad);
    p->drawPath(path);

    p->setPen(Qt::white);
    p->drawText(
        header.adjusted(12, 0, -12, 0),
        Qt::AlignVCenter | Qt::AlignLeft,
        m_node->addr.isEmpty()
            ? m_node->name
            : QString("%1 : %2").arg(m_node->name, m_node->addr)
    );
}

// -------------------------------------------------------

static void drawTriangle(QPainter* p,
                         QPointF c,
                         bool expanded)
{
    QPolygonF poly;

    if (expanded) {
        // ▼
        poly << QPointF(c.x() - 4, c.y() - 2)
             << QPointF(c.x() + 4, c.y() - 2)
             << QPointF(c.x(),     c.y() + 4);
    } else {
        // ▶
        poly << QPointF(c.x() - 2, c.y() - 4)
             << QPointF(c.x() - 2, c.y() + 4)
             << QPointF(c.x() + 4, c.y());
    }

    p->setBrush(Qt::white);
    p->setPen(Qt::NoPen);
    p->drawPolygon(poly);
}


// -------------------------------------------------------

void GraphicalNodeItem::drawSource(QPainter* p)
{
    QVector<VisibleRow> rows;

    for (VarNode* c : m_node->children)
        buildRowsRecursive(c, 0, m_expanded, rows);

    int y = HeaderHeight;

    for (int i = 0; i < rows.size(); ++i) {

        const VisibleRow& r = rows[i];
        VarNode* n = r.node;

        QRectF rowRect(0, y, m_width, RowHeight);

        p->setPen(Qt::NoPen);
        p->setBrush(QColor(45, 45, 45));
        p->drawRect(rowRect);

        int x = LeftPadding + r.indent * IndentStep + 5;

        // triangle
        if (n->hasChildren) {
            drawTriangle(
                p,
                QPointF(x - 10, y + RowHeight / 2),
                m_expanded.value(n, false));
        }

        p->setPen(Qt::white);

        QString text;

        if (!n->hasChildren) {
            text = QString("%1 = %2")
                       .arg(n->name, n->value);
        } else {
            if (m_expanded.value(n, false))
                text = n->name;
            else
                text = QString("%1 = []").arg(n->name);
        }

        p->drawText(x, y + 16, text);

        y += RowHeight;
    }
}


// -------------------------------------------------------

void GraphicalNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    QPointF p = e->pos();

    if (p.y() < HeaderHeight)
        return QGraphicsItem::mousePressEvent(e);

    QVector<VisibleRow> rows;

    for (VarNode* c : m_node->children)
        buildRowsRecursive(c, 0, m_expanded, rows);

    int row =
        int((p.y() - HeaderHeight) / RowHeight);

    if (row < 0 || row >= rows.size())
        return;

    const VisibleRow& r = rows[row];

    int x = LeftPadding + r.indent * IndentStep;

    QRect triangleRect(
        x - 14,
        HeaderHeight + row * RowHeight,
        12,
        RowHeight
    );

    if (triangleRect.contains(p.toPoint()) &&
        r.node->hasChildren)
    {
        m_expanded[r.node] =
            !m_expanded.value(r.node, false);

        prepareGeometryChange();
        update();
        return;
    }

    QGraphicsItem::mousePressEvent(e);
}


// -------------------------------------------------------

void GraphicalNodeItem::recalculateWidth()
{
    QFontMetrics fm{QFont()};


    QVector<VisibleRow> rows;
    for (VarNode* c : m_node->children)
        buildRowsRecursive(c, 0, m_expanded, rows);


    QString title =
        m_node->varId.isEmpty()
            ? m_node->name
            : QString("%1 : %2").arg(m_node->name, m_node->varId);

    int maxWidth = fm.horizontalAdvance(title);

    for (const VisibleRow& r : rows) {
        VarNode* n = r.node;

        QString text;
        if (!n->hasChildren) {
            text = QString("%1 = %2").arg(n->name, n->value);
        } else {
            text = m_expanded.value(n, false) ? n->name
                                              : QString("%1 = []").arg(n->name);
        }

        const int indentPx = r.indent * IndentStep;


        const int w =
            LeftPadding + indentPx + fm.horizontalAdvance(text);

        maxWidth = qMax(maxWidth, w);
    }


    maxWidth += 40;

    if (maxWidth != m_width) {
        prepareGeometryChange();
        m_width = maxWidth;
    }
}


void GraphicalNodeItem::addEdge(GraphicalEdgeItem* e)
{
    m_edges << e;
}

QPointF GraphicalNodeItem::inputPort() const
{
    return mapToScene(QPointF(
        -SocketOffset,
        HeaderHeight / 2
    ));
}

QPointF GraphicalNodeItem::outputPortFor(VarNode* child) const
{
    int index = m_node->children.indexOf(child);

    if (index < 0)
        return mapToScene(QPointF(
            m_width + SocketOffset,
            HeaderHeight / 2
        ));

    qreal y =
        HeaderHeight +
        index * RowHeight +
        RowHeight / 2;

    return mapToScene(QPointF(
        m_width + SocketOffset,
        y
    ));
}

// =======================================================
// GraphicalEdgeItem
// =======================================================

GraphicalEdgeItem::GraphicalEdgeItem(GraphicalNodeItem* from,
                                     GraphicalNodeItem* to)
    : m_from(from)
    , m_to(to)
{
    setZValue(-1);

    QPen pen(QColor(180, 180, 180));
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    setPen(pen);
}

void GraphicalEdgeItem::updatePosition()
{
    QPointF p1 = m_from->outputPortFor(nullptr);
    QPointF p2 = m_to->inputPort();

    const qreal dx = qAbs(p2.x() - p1.x());
    const qreal handle = qMax(dx * 0.5, 60.0);

    QPointF c1 = p1 + QPointF(handle, 0);
    QPointF c2 = p2 - QPointF(handle, 0);

    QPainterPath path;
    path.moveTo(p1);
    path.cubicTo(c1, c2, p2);

    setPath(path);
}





// =======================================================
// GraphicalVariablesView
// =======================================================

GraphicalVariablesView::GraphicalVariablesView(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    m_scene->setSceneRect(-5000, -5000, 10000, 10000);

    setRenderHint(QPainter::Antialiasing);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorViewCenter);
    setResizeAnchor(AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);


    setStyleSheet(R"(
    QScrollBar:vertical {
        background: transparent;
        width: 8px;
        margin: 4px 2px 4px 2px;
    }

    QScrollBar::handle:vertical {
        background: rgba(200, 200, 200, 120);
        border-radius: 4px;
        min-height: 30px;
    }

    QScrollBar::handle:vertical:hover {
        background: rgba(220, 220, 220, 180);
    }

    QScrollBar::add-line:vertical,
    QScrollBar::sub-line:vertical {
        height: 0px;
    }

    QScrollBar::add-page:vertical,
    QScrollBar::sub-page:vertical {
        background: transparent;
    }

    /* ---- horizontal ---- */

    QScrollBar:horizontal {
        background: transparent;
        height: 8px;
        margin: 2px 4px 2px 4px;
    }

    QScrollBar::handle:horizontal {
        background: rgba(200, 200, 200, 120);
        border-radius: 4px;
        min-width: 30px;
    }

    QScrollBar::handle:horizontal:hover {
        background: rgba(220, 220, 220, 180);
    }

    QScrollBar::add-line:horizontal,
    QScrollBar::sub-line:horizontal {
        width: 0px;
    }

    QScrollBar::add-page:horizontal,
    QScrollBar::sub-page:horizontal {
        background: transparent;
    }
    )");

    //overlay zoom
    auto *overlay = new QWidget(this);
    overlay->setStyleSheet(R"(
        QWidget {
            background: rgba(30, 30, 30, 220);
            border: 1px solid #555;
            border-radius: 6px;
        }

        QToolButton {
            color: white;
            background: transparent;
            border: none;
            font-size: 16px;
        }

        QToolButton:hover {
            background: #3a3a3a;
            border-radius: 4px;
        }

        QToolButton:pressed {
            background: #5a5a5a;
        }
    )");

    auto *vl = new QVBoxLayout(overlay);
    vl->setContentsMargins(6, 6, 6, 6);
    vl->setSpacing(4);

    auto mk = [&](const QString &t) {
        auto *b = new QToolButton(overlay);
        b->setText(t);
        b->setAutoRaise(true);
        b->setFixedSize(28, 28);
        vl->addWidget(b);
        return b;
    };

    connect(mk("+"), &QToolButton::clicked, this,
            &GraphicalVariablesView::zoomIn);

    connect(mk("-"), &QToolButton::clicked, this,
            &GraphicalVariablesView::zoomOut);

    connect(mk("⤢"), &QToolButton::clicked, this,
            &GraphicalVariablesView::fitGraph);

    connect(mk("⟳"), &QToolButton::clicked, this,
            &GraphicalVariablesView::resetZoom);

    overlay->move(10, 10);
    overlay->show();
}

GraphicalVariablesView::~GraphicalVariablesView() = default;

void GraphicalVariablesView::zoomIn() { scale(1.2, 1.2); }
void GraphicalVariablesView::zoomOut() { scale(1 / 1.2, 1 / 1.2); }
void GraphicalVariablesView::fitGraph() {
    	fitInView(scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40),
	          Qt::KeepAspectRatio);
}
void GraphicalVariablesView::resetZoom() { resetTransform(); }


void GraphicalVariablesView::setSession(DebugSession* session)
{
    m_session = session;
    refresh();
}

void GraphicalVariablesView::refresh()
{
    if (!m_session)
        return;

    m_scene->clear();

    QHash<VarNode*, GraphicalNodeItem*> nodeMap;
    QHash<quintptr, GraphicalNodeItem*> addrMap;

    const QList<VarNode*> roots = m_session->complexVariables();

    int y = 0;

    for (VarNode* root : roots) {

        auto* item = new GraphicalNodeItem(root);
        m_scene->addItem(item);
        item->setPos(0, y);

        nodeMap[root] = item;

        if (root->addr != 0) {
            bool ok = false;
            addrMap[root->addr.toULongLong(&ok, 16)] = item;
        }

        y += 140;
    }

    buildPointerEdges(nodeMap, addrMap, m_scene);
}




/*deprecated */
void GraphicalVariablesView::layoutTree(GraphicalNodeItem* item,
                                        int depth,
                                        int& y) 
{
    const int xSpacing = 320;
    const int ySpacing = 160;

    item->setPos(depth * xSpacing, y);

    int childY = y;

    for (VarNode* child : item->node()->children) {

        if (!child->hasChildren)
            continue;

        auto* childItem =
            new GraphicalNodeItem(child);

        m_scene->addItem(childItem);

        childY += ySpacing;

        layoutTree(childItem, depth + 1, childY);

        auto* edge = new GraphicalEdgeItem(item, childItem);

        m_scene->addItem(edge);

        item->addEdge(edge);
        childItem->addEdge(edge);

        edge->updatePosition();

    }
}

void GraphicalVariablesView::wheelEvent(QWheelEvent* event)
{
    const double factor = 1.15;

    if (event->angleDelta().y() > 0)
        scale(factor, factor);
    else
        scale(1.0 / factor, 1.0 / factor);
}

void GraphicalVariablesView::mouseDoubleClickEvent(QMouseEvent* event)
{
    auto* item =
        dynamic_cast<GraphicalNodeItem*>(itemAt(event->pos()));

    if (item) {
        item->recalculateWidth();
        refresh();
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void GraphicalVariablesView::drawBackground(QPainter* p,
                                            const QRectF& rect)
{
    const int small = 20;
    const int big   = 100;

    QPen thin(QColor(0, 0, 0, 25));
    QPen thick(QColor(0, 0, 0, 45));

    const int l = int(std::floor(rect.left()));
    const int r = int(std::ceil(rect.right()));
    const int t = int(std::floor(rect.top()));
    const int b = int(std::ceil(rect.bottom()));

    p->setPen(thin);
    for (int x = l - l % small; x <= r; x += small)
        p->drawLine(x, t, x, b);
    for (int y = t - t % small; y <= b; y += small)
        p->drawLine(l, y, r, y);

    p->setPen(thick);
    for (int x = l - l % big; x <= r; x += big)
        p->drawLine(x, t, x, b);
    for (int y = t - t % big; y <= b; y += big)
        p->drawLine(l, y, r, y);
}

QVariant GraphicalNodeItem::itemChange(
    GraphicsItemChange change,
    const QVariant& value)
{
    if (change == ItemPositionHasChanged) {
        for (GraphicalEdgeItem* e : m_edges)
            e->updatePosition();
    }

    return QGraphicsItem::itemChange(change, value);
}

