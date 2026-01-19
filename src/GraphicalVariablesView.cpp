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

#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>


// =======================================================
// Blender-style socket geometry
// =======================================================

static constexpr int SocketRadius = 4;
static constexpr int SocketOffset = -6;


// =======================================================
// GraphicalNodeItem
// =======================================================

GraphicalNodeItem::GraphicalNodeItem(VarNode* node)
    : m_node(node)
{
    setFlag(ItemIsSelectable);
    setFlag(ItemIsMovable);
    recalculateWidth();
}

VarNode* GraphicalNodeItem::node() const
{
    return m_node;
}

QRectF GraphicalNodeItem::boundingRect() const
{
    int rows = expanded ? m_node->children.size() : 0;
    int height = HeaderHeight + rows * RowHeight;

    return QRectF(
        -SocketOffset - SocketRadius,
        0,
        m_width + 2 * (SocketOffset + SocketRadius),
        height
    );
}

void GraphicalNodeItem::paint(QPainter* p,
                              const QStyleOptionGraphicsItem*,
                              QWidget*)
{
    p->setRenderHint(QPainter::Antialiasing);

    drawHeader(p);

    if (expanded || m_node->parent == nullptr)
        drawSource(p);
}

void GraphicalNodeItem::drawHeader(QPainter* p)
{
    QRectF header(0, 0, m_width, HeaderHeight);

    QColor bg(70, 70, 70);
    if (m_node->parent == nullptr)
        bg = QColor(90, 70, 40);

    p->setPen(Qt::NoPen);
    p->setBrush(bg);
    p->drawRoundedRect(header, 6, 6);

    p->setPen(Qt::white);

    QString title = m_node->type.isEmpty()
        ? m_node->name
        : QString("%1 : %2").arg(m_node->name, m_node->type);

    p->drawText(15, 15, title);


    QPointF in(-SocketOffset, (HeaderHeight / 2) - 2);

    p->setPen(Qt::NoPen);
    p->setBrush(QColor(200, 200, 200));
    p->drawEllipse(in, SocketRadius, SocketRadius);
}

void GraphicalNodeItem::drawSource(QPainter* p)
{
    int y = HeaderHeight - 5;

    for (VarNode* child : m_node->children) {

        QRectF row(0, y, m_width, RowHeight);

        p->setPen(Qt::NoPen);
        p->setBrush(QColor(45, 45, 45));
        p->drawRect(row);

        p->setPen(Qt::white);

        QString text =
            QString("%1 = %2")
                .arg(child->name, child->value);

        p->drawText(12, y + 16, text);

        // outlet socket
        if (child->value.startsWith("0x")) {

            QPointF out(
                m_width + SocketOffset,
                y + RowHeight / 2
            );

            p->setPen(Qt::NoPen);
            p->setBrush(QColor(200, 200, 200));
            p->drawEllipse(out, SocketRadius, SocketRadius);
        }

        y += RowHeight;
    }
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
        return mapToScene(QPointF(m_width + SocketOffset,
                                  HeaderHeight / 2));

    qreal y =
        HeaderHeight +
        index * RowHeight +
        RowHeight / 2;

    return mapToScene(QPointF(
        m_width + SocketOffset,
        y
    ));
}

void GraphicalNodeItem::recalculateWidth()
{
    QFont font;
    QFontMetrics fm(font);

    int maxWidth = 0;

    // header
    QString headerText =
        m_node->type.isEmpty()
            ? m_node->name
            : QString("%1 : %2").arg(m_node->name, m_node->type);

    maxWidth = fm.horizontalAdvance(headerText);

    // children
    for (VarNode* child : m_node->children) {
        QString row =
            QString("%1 = %2")
                .arg(child->name, child->value);

        maxWidth = qMax(maxWidth, fm.horizontalAdvance(row));
    }

    constexpr int leftPadding  = 12;
    constexpr int rightPadding = 20;

    m_width = maxWidth + leftPadding + rightPadding;
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
}

GraphicalVariablesView::~GraphicalVariablesView() = default;

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

    const QList<VarNode*> roots =
        m_session->complexVariables();

    int y = 0;
    const int rootSpacing = 120;

    for (VarNode* root : roots) {

        auto* rootItem =
            new GraphicalNodeItem(root);

        m_scene->addItem(rootItem);

        layoutTree(rootItem, 0, y);

        y += rootSpacing;
    }
}

void GraphicalVariablesView::layoutTree(GraphicalNodeItem* item,
                                        int depth,
                                        int& y)
{
    const int xSpacing = 320;
    const int ySpacing = 160;

    item->setPos(depth * xSpacing, y);

    if (!item->expanded)
        return;

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

void GraphicalNodeItem::addEdge(GraphicalEdgeItem* e)
{
    m_edges << e;
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
        item->expanded = !item->expanded;
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
