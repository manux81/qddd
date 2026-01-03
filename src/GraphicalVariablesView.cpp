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

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QStyleOptionGraphicsItem>
#include <QLinearGradient>
#include <QFontMetrics>
#include <QVBoxLayout>
#include <QToolButton>
#include <QtMath>

// =====================================================
// Helpers
// =====================================================

namespace {

constexpr qreal H_PADDING   = 12.0;
constexpr qreal V_PADDING   = 6.0;
constexpr qreal BASE_HEIGHT = 36.0;
constexpr qreal MAX_TEXT_W  = 420.0;

// =====================================================
// NodeItem
// =====================================================

class NodeItem : public QGraphicsItem {
public:
    static constexpr int INDENT_W = 14;
    static constexpr int TRI_SIZE = 8;

    explicit NodeItem(const GraphNode& n)
        : m_node(n)
    {
        setFlags(ItemIsMovable | ItemIsSelectable);
        setZValue(1);
        rebuildLayout();
    }

    QString id() const { return m_node.id; }

    QRectF boundingRect() const override
    {
        // un po' di “slack” per ombra e bordo
        return m_rect.adjusted(-10, -10, 10, 10);
    }

    int fieldAt(const QPointF& pos) const
    {
        // hit-test sui field rect (solo quelli visibili)
        for (int i = 0; i < m_lines.size(); ++i) {
            if (m_lines[i].hitRect.contains(pos))
                return m_lines[i].fieldIndex;
        }
        return -1;
    }

    auto& mutableField(int i) { return m_node.fields[i]; }

    void updateLayout()
    {
        prepareGeometryChange();
        rebuildLayout();
        update();
    }

protected:
    void paint(QPainter *p,
               const QStyleOptionGraphicsItem *opt,
               QWidget *) override
    {
        p->setRenderHint(QPainter::Antialiasing, true);

        const bool selected = opt->state & QStyle::State_Selected;

        QColor base = m_node.color.isValid() ? m_node.color : QColor("#ECEFF1");
        QColor border = selected ? QColor("#1976D2") : QColor(0, 0, 0, 90);

        // shadow
        QPainterPath shadow;
        shadow.addRoundedRect(m_rect.translated(3, 3), 10, 10);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 40));
        p->drawPath(shadow);

        // body
        QLinearGradient grad(m_rect.topLeft(), m_rect.bottomLeft());
        grad.setColorAt(0.0, base.lighter(108));
        grad.setColorAt(1.0, base.darker(104));
        p->setBrush(grad);
        p->drawRoundedRect(m_rect, 10, 10);

        // title bar
        p->setBrush(base.darker(115));
        p->drawRoundedRect(m_titleRect, 10, 10);
        p->drawRect(m_titleRect.adjusted(0, 10, 0, 0));

        // title text
        QFont titleFont = p->font();
        titleFont.setBold(true);
        p->setFont(titleFont);
        p->setPen(Qt::white);
        p->drawText(m_titleTextRect, Qt::AlignVCenter | Qt::AlignLeft, m_titleText);

        // fields
        QFont fieldFont = p->font();
        fieldFont.setBold(false);
        p->setFont(fieldFont);
        p->setPen(Qt::black);

        for (const auto& ln : m_lines) {
            const auto& f = m_node.fields[ln.fieldIndex];

            // triangle (if expandable)
            if (f.isExpandable) {
                const qreal x = ln.triX;
                const qreal y = ln.textRect.top(); // top aligned
                QPointF tri[3];
                if (f.expanded) {
                    tri[0] = {x, y + 4};
                    tri[1] = {x + TRI_SIZE, y + 4};
                    tri[2] = {x + TRI_SIZE / 2.0, y + 4 + TRI_SIZE};
                } else {
                    tri[0] = {x, y + 4};
                    tri[1] = {x, y + 4 + TRI_SIZE};
                    tri[2] = {x + TRI_SIZE, y + 4 + TRI_SIZE / 2.0};
                }
                p->setBrush(Qt::black);
                p->drawPolygon(tri, 3);
            }

            // text (real wrap)
            const QString text = f.name + " = " + f.value;
            p->setBrush(Qt::NoBrush);
            p->drawText(ln.textRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
        }

        // border last
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(border, selected ? 2.0 : 1.2));
        p->drawRoundedRect(m_rect, 10, 10);
    }

private:
    struct Line {
        int fieldIndex = -1;
        QRectF textRect;   // where text is drawn
        QRectF hitRect;    // row area for clicking
        qreal triX = 0;    // triangle x position
    };

    bool isVisibleField(int index) const
    {
        int d = m_node.fields[index].depth;
        for (int i = index - 1; i >= 0; --i) {
            if (m_node.fields[i].depth < d) {
                if (!m_node.fields[i].expanded)
                    return false;
                d = m_node.fields[i].depth;
            }
        }
        return true;
    }

    void rebuildLayout()
    {
        // IMPORTANT: font metrics must match paint() fonts
        QFont baseFont;                // default
        QFontMetrics fm(baseFont);

        QFont titleFont = baseFont;
        titleFont.setBold(true);
        QFontMetrics tfm(titleFont);

        // title metrics
        m_titleText = m_node.title + " [" + m_node.id + "]";
        const qreal titleH = 26.0;

        // width starts from title
        qreal w = qreal(tfm.horizontalAdvance(m_titleText));
        qreal h = titleH + 8 /*gap after title*/ + 8 /*bottom pad*/;

        // build field lines first with a provisional wrap width (cap)
        // If you want fewer wraps for huge LLDB strings, raise this (e.g. 720/900).
        const qreal wrapMax = 720.0;

        m_lines.clear();

        // First pass: determine required w/h
        for (int i = 0; i < m_node.fields.size(); ++i) {
            if (!isVisibleField(i))
                continue;

            const auto& f = m_node.fields[i];

            const qreal indent = f.depth * INDENT_W;
            const qreal triPad = f.isExpandable ? (TRI_SIZE + 6) : 0;

            const qreal availW = qMax<qreal>(80.0, wrapMax - indent - triPad);

            const QString text = f.name + " = " + f.value;

            QRect br = fm.boundingRect(
                0, 0,
                int(availW), 20000,
                Qt::TextWordWrap,
                text
            );

            const qreal rowH = qMax<qreal>(18.0, br.height());
            h += rowH + V_PADDING;

            w = qMax(w, qreal(br.width()) + indent + triPad);
        }

        w += 2 * H_PADDING;

        m_rect = QRectF(-w / 2.0, -h / 2.0, w, h);

        // title rects
        m_titleRect = QRectF(m_rect.left(), m_rect.top(), m_rect.width(), titleH);
        m_titleTextRect = m_titleRect.adjusted(8, 0, -8, 0);

        // Second pass: assign actual rects per field using final width
        qreal y = m_titleRect.bottom() + 8;

        for (int i = 0; i < m_node.fields.size(); ++i) {
            if (!isVisibleField(i))
                continue;

            const auto& f = m_node.fields[i];

            const qreal indent = f.depth * INDENT_W;
            const qreal triPad = f.isExpandable ? (TRI_SIZE + 6) : 0;

            const qreal triX = m_rect.left() + H_PADDING + indent;
            const qreal textX = triX + triPad;
            const qreal textW = m_rect.width() - (textX - m_rect.left()) - H_PADDING;

            const QString text = f.name + " = " + f.value;

            QRect br = fm.boundingRect(
                0, 0,
                int(textW), 20000,
                Qt::TextWordWrap,
                text
            );
            const qreal rowH = qMax<qreal>(18.0, br.height());

            Line ln;
            ln.fieldIndex = i;
            ln.triX = triX;
            ln.textRect = QRectF(textX, y, textW, rowH);
            ln.hitRect  = QRectF(m_rect.left(), y, m_rect.width(), rowH); // click on row
            m_lines.push_back(ln);

            y += rowH + V_PADDING;
        }
    }

    GraphNode m_node;
    QRectF m_rect;

    QString m_titleText;
    QRectF m_titleRect;
    QRectF m_titleTextRect;

    QVector<Line> m_lines;
};


} // namespace

// =====================================================
// GraphicalVariablesView
// =====================================================

struct GraphicalVariablesView::Impl {
    QVector<NodeItem*> nodes;
};

GraphicalVariablesView::GraphicalVariablesView(QWidget *parent)
    : QGraphicsView(parent),
      m_impl(new Impl),
      m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setRenderHint(QPainter::Antialiasing);
    setStyleSheet("QGraphicsView { border: none; }");

    // ---- overlay zoom (Google Maps style)
    auto *overlay = new QWidget(this);
    auto *vl = new QVBoxLayout(overlay);
    vl->setContentsMargins(4,4,4,4);

    auto mk = [&](const QString& t){
        auto *b = new QToolButton(overlay);
        b->setText(t);
        b->setAutoRaise(true);
        b->setFixedSize(28, 28);
        vl->addWidget(b);
        return b;
    };

    connect(mk("+"), &QToolButton::clicked, this, &GraphicalVariablesView::zoomIn);
    connect(mk("−"), &QToolButton::clicked, this, &GraphicalVariablesView::zoomOut);
    connect(mk("⤢"), &QToolButton::clicked, this, &GraphicalVariablesView::fitGraph);
    connect(mk("⟳"), &QToolButton::clicked, this, &GraphicalVariablesView::resetZoom);

    overlay->move(10, 10);
    overlay->show();
}

GraphicalVariablesView::~GraphicalVariablesView()
{
    delete m_impl;
}

void GraphicalVariablesView::wheelEvent(QWheelEvent *e)
{
    const qreal s = 1.15;
    scale(e->angleDelta().y() > 0 ? s : 1/s,
          e->angleDelta().y() > 0 ? s : 1/s);
}

void GraphicalVariablesView::mousePressEvent(QMouseEvent *e)
{
    if (auto* it = itemAt(e->pos())) {
        if (auto* n = dynamic_cast<NodeItem*>(it)) {
            QPointF lp = n->mapFromScene(mapToScene(e->pos()));
            int idx = n->fieldAt(lp);
            if (idx >= 0) {
                auto& f = n->mutableField(idx);
                if (f.isExpandable) {
                    f.expanded = !f.expanded;
                    n->updateLayout();
                    viewport()->update();
                    return;
                }
            }
        }
    }
    QGraphicsView::mousePressEvent(e);
}

void GraphicalVariablesView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (auto* it = itemAt(e->pos())) {
        if (auto* n = dynamic_cast<NodeItem*>(it)) {
            emit nodeDoubleClicked(n->id());
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

void GraphicalVariablesView::drawBackground(QPainter *p, const QRectF &rect)
{
    p->fillRect(rect, QColor("#f7f7f5"));

    const int small = 20;
    const int big   = 100;

    QPen thin(QColor(0,0,0,25));
    QPen thick(QColor(0,0,0,45));

    const int l = int(std::floor(rect.left()));
    const int r = int(std::ceil(rect.right()));
    const int t = int(std::floor(rect.top()));
    const int b = int(std::ceil(rect.bottom()));

    p->setPen(thin);
    for (int x = l - l % small; x < r; x += small)
        p->drawLine(x, t, x, b);
    for (int y = t - t % small; y < b; y += small)
        p->drawLine(l, y, r, y);

    p->setPen(thick);
    for (int x = l - l % big; x < r; x += big)
        p->drawLine(x, t, x, b);
    for (int y = t - t % big; y < b; y += big)
        p->drawLine(l, y, r, y);
}

void GraphicalVariablesView::zoomIn()    { scale(1.2, 1.2); }
void GraphicalVariablesView::zoomOut()   { scale(1/1.2, 1/1.2); }
void GraphicalVariablesView::resetZoom() { resetTransform(); }

void GraphicalVariablesView::fitGraph()
{
    fitInView(scene()->itemsBoundingRect().adjusted(-40,-40,40,40),
              Qt::KeepAspectRatio);
}

void GraphicalVariablesView::setGraph(const QVector<GraphNode>& nodes,
                                      const QVector<GraphEdge>&)
{
    m_scene->clear();
    m_impl->nodes.clear();

    qreal x = 0;
    for (const auto& n : nodes) {
        auto *ni = new NodeItem(n);
        m_scene->addItem(ni);
        ni->setPos(x, 0);
        m_impl->nodes.push_back(ni);
        x += ni->boundingRect().width() + 80;
    }

    fitGraph();
}

void GraphicalVariablesView::rebuildEdges()
{
    // stub
}
