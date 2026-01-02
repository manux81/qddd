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
#include <QtMath>
#include <QQueue>

#include <QStyleOptionGraphicsItem>
#include <QLinearGradient>

// =====================================================
//  Oggetti grafici interni
// =====================================================

namespace {

class NodeItem : public QGraphicsItem {
  public:
	explicit NodeItem(const GraphNode &n) : m_node(n) {
		setFlags(ItemIsMovable | ItemIsSelectable);
		setZValue(1);
		updateRect();
	}

	QString id() const { return m_node.id; }

	void setValue(const QString &v) {
		if (!m_node.fields.isEmpty())
			m_node.fields[0].value = v;
		update();
	}

	void setFields(const QVector<GraphNodeField> &fields) {
		m_node.fields = fields;
		updateRect();
		update();
	}

	QRectF rect() const { return m_rect; }

	QRectF boundingRect() const override {
		return m_rect.adjusted(-2, -2, 2, 2);
	}

	void paint(QPainter *p, const QStyleOptionGraphicsItem *opt,
	           QWidget *widget = nullptr) override {
		Q_UNUSED(widget);
		p->setRenderHint(QPainter::Antialiasing, true);

		const bool selected = opt->state & QStyle::State_Selected;
		QColor base = m_node.color.isValid() ? m_node.color : QColor("#E0E0E0");
		QColor border = selected ? QColor("#1565C0") : QColor(0, 0, 0, 150);

		// card
		QLinearGradient grad(m_rect.topLeft(), m_rect.bottomLeft());
		grad.setColorAt(0.0, base.lighter(115));
		grad.setColorAt(1.0, base.darker(105));

		p->setBrush(grad);
		p->setPen(QPen(border, selected ? 2.0 : 1.3));
		p->drawRoundedRect(m_rect, 10, 10);

		// title bar
		QRectF titleBar = m_rect.adjusted(0, 0, 0, -m_rect.height() + 26);
		p->setBrush(base.darker(120));
		p->setPen(Qt::NoPen);
		p->drawRoundedRect(titleBar, 10, 10);
		p->drawRect(titleBar.adjusted(0, 10, 0, 0));

		// title text
		p->setPen(Qt::white);
		QFont f = p->font();
		f.setBold(true);
		p->setFont(f);
		p->drawText(titleBar.adjusted(8, 3, -6, -4),
		            Qt::AlignVCenter | Qt::AlignLeft, m_node.title);

		// divider
		p->setPen(QPen(Qt::white, 0.5));
		p->drawLine(QPointF(m_rect.left() + 4, titleBar.bottom()),
		            QPointF(m_rect.right() - 4, titleBar.bottom()));

		// fields
		f.setBold(false);
		f.setPointSizeF(f.pointSizeF() - 1);
		p->setFont(f);
		p->setPen(Qt::black);

		qreal y = titleBar.bottom() + 3;
		for (const auto &fld : m_node.fields) {
			QString line = fld.name + ": " + fld.value;
			p->drawText(QRectF(m_rect.left() + 6, y, m_rect.width() - 12, 16),
			            Qt::AlignLeft | Qt::AlignVCenter, line);
			y += 16;
		}
	}

  private:
	void updateRect() {
		const qreal baseH = 40.0;
		const qreal perRow = 18.0;
		qreal h = baseH + perRow * m_node.fields.size();
		m_rect = QRectF(-95, -h / 2.0, 190, h);
	}

	GraphNode m_node;
	QRectF m_rect;
};

class EdgeItem : public QGraphicsItem {
  public:
	EdgeItem(NodeItem *from, NodeItem *to, const GraphEdge &edge)
	    : m_from(from), m_to(to), m_edge(edge) {
		setZValue(0);
	}

	QString fromId() const { return m_edge.fromId; }
	QString toId() const { return m_edge.toId; }

	QRectF boundingRect() const override {
		if (!m_from || !m_to)
			return {};

		QPointF a = mapFromItem(m_from, m_from->rect().center());
		QPointF b = mapFromItem(m_to, m_to->rect().center());
		QRectF r(a, b);
		return r.normalized().adjusted(-30, -30, 30, 30);
	}

	void paint(QPainter *p, const QStyleOptionGraphicsItem *opt,
	           QWidget *widget = nullptr) override {
		Q_UNUSED(opt);
		Q_UNUSED(widget);
		if (!m_from || !m_to)
			return;

		p->setRenderHint(QPainter::Antialiasing, true);

		QPointF a = mapFromItem(m_from, m_from->rect().center());
		QPointF b = mapFromItem(m_to, m_to->rect().center());

		QPointF c1 = a + QPointF(0, -50);
		QPointF c2 = b + QPointF(0, 50);

		QPainterPath path(a);
		path.cubicTo(c1, c2, b);

		p->setPen(QPen(QColor("#444444"), 2.0));
		p->setBrush(Qt::NoBrush);
		p->drawPath(path);

		// freccia
		QLineF line(path.pointAtPercent(0.94), b);
		double angle = std::atan2(-line.dy(), line.dx());
		const qreal s = 9.0;
		QPointF p1 =
		    b + QPointF(std::sin(angle + M_PI / 3) * s,
		                std::cos(angle + M_PI / 3) * s);
		QPointF p2 =
		    b + QPointF(std::sin(angle - M_PI / 3) * s,
		                std::cos(angle - M_PI / 3) * s);
		QPolygonF poly;
		poly << b << p1 << p2;
		p->setBrush(QColor("#444444"));
		p->setPen(Qt::NoPen);
		p->drawPolygon(poly);

		if (!m_edge.label.isEmpty()) {
			QPointF mid = path.pointAtPercent(0.5);
			p->setPen(Qt::darkBlue);
			p->drawText(mid + QPointF(5, -4), m_edge.label);
		}
	}

  private:
	NodeItem *m_from;
	NodeItem *m_to;
	GraphEdge m_edge;
};

} // namespace

// =====================================================
//  GraphicalVariablesView
// =====================================================

struct GraphicalVariablesView::Impl {
	QMap<QString, NodeItem *> nodes;
	QVector<EdgeItem *> edges;
	QVector<GraphEdge> edgeDefs;
};

GraphicalVariablesView::GraphicalVariablesView(QWidget *parent)
    : QGraphicsView(parent), m_impl(new Impl),
      m_scene(new QGraphicsScene(this)) {
	setScene(m_scene);
	setRenderHint(QPainter::Antialiasing, true);
	setDragMode(RubberBandDrag);
	setTransformationAnchor(AnchorUnderMouse);

	setBackgroundBrush(QColor("#1a2d3e")); // viola-grigio scuro
	setStyleSheet("QGraphicsView { border: none; }");
}

GraphicalVariablesView::~GraphicalVariablesView() { delete m_impl; }

void GraphicalVariablesView::clearGraph() {
	m_scene->clear();
	m_impl->nodes.clear();
	m_impl->edges.clear();
	m_impl->edgeDefs.clear();
}

void GraphicalVariablesView::setGraph(const QVector<GraphNode> &nodes,
                                      const QVector<GraphEdge> &edges) {
	clearGraph();

	for (const auto &n : nodes) {
		auto *item = new NodeItem(n);
		m_scene->addItem(item);
		m_impl->nodes.insert(n.id, item);
	}

	m_impl->edgeDefs = edges;

	// layout prima di creare fisicamente gli edge
	layoutBreadthFirst();

	for (const auto &e : edges) {
		NodeItem *from = m_impl->nodes.value(e.fromId, nullptr);
		NodeItem *to = m_impl->nodes.value(e.toId, nullptr);
		if (!from || !to)
			continue;
		auto *edgeItem = new EdgeItem(from, to, e);
		m_scene->addItem(edgeItem);
		m_impl->edges.push_back(edgeItem);
	}

	QRectF r = m_scene->itemsBoundingRect().adjusted(-40, -40, 40, 40);
	if (!r.isEmpty())
		fitInView(r, Qt::KeepAspectRatio);
}

void GraphicalVariablesView::layoutBreadthFirst() {
	if (m_impl->nodes.isEmpty())
		return;

	// trova radici (nodi che non compaiono mai come destinazione)
	QSet<QString> all;
	QSet<QString> asChild;
	for (auto it = m_impl->nodes.begin(); it != m_impl->nodes.end(); ++it)
		all.insert(it.key());
	for (const auto &e : m_impl->edgeDefs)
		asChild.insert(e.toId);
	QVector<QString> roots = (all - asChild).values().toVector();
	if (roots.isEmpty())
		roots.append(m_impl->nodes.firstKey());

	// BFS
	QMap<QString, int> depth;
	QMap<int, QVector<QString>> perLevel;
	QQueue<QString> q;
	for (const QString &r : roots) {
		depth[r] = 0;
		q.enqueue(r);
		perLevel[0].append(r);
	}

	while (!q.isEmpty()) {
		const QString id = q.dequeue();
		int d = depth[id];
		for (const auto &e : m_impl->edgeDefs) {
			if (e.fromId == id) {
				if (!depth.contains(e.toId)) {
					depth[e.toId] = d + 1;
					q.enqueue(e.toId);
					perLevel[d + 1].append(e.toId);
				}
			}
		}
	}

	// disposizione
	const qreal dx = 260.0;
	const qreal dy = 170.0;
	for (auto it = perLevel.begin(); it != perLevel.end(); ++it) {
		int level = it.key();
		const QVector<QString> &list = it.value();
		for (int i = 0; i < list.size(); ++i) {
			NodeItem *item = m_impl->nodes.value(list[i], nullptr);
			if (!item)
				continue;
			item->setPos(level * dx, i * dy);
		}
	}
}

void GraphicalVariablesView::updateNodeValue(const QString &id,
                                             const QString &value) {
	if (auto *n = m_impl->nodes.value(id, nullptr))
		n->setValue(value);
}

void GraphicalVariablesView::updateNodeFields(
    const QString &id, const QVector<GraphNodeField> &fields) {
	if (auto *n = m_impl->nodes.value(id, nullptr))
		n->setFields(fields);
}

void GraphicalVariablesView::wheelEvent(QWheelEvent *event) {
	const double factor = 1.15;
	if (event->angleDelta().y() > 0)
		scale(factor, factor);
	else
		scale(1.0 / factor, 1.0 / factor);
}

void GraphicalVariablesView::mouseDoubleClickEvent(QMouseEvent *event) {
	if (auto *item = itemAt(event->pos())) {
		if (auto *ni = dynamic_cast<NodeItem *>(item)) {
			emit nodeDoubleClicked(ni->id());
		}
	}
	QGraphicsView::mouseDoubleClickEvent(event);
}
