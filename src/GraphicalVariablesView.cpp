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

#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QVBoxLayout>
#include <QToolButton>
#include <QStyle>
#include <cmath>


// =======================================================
// Helpers / layout constants (come versione vecchia)
// =======================================================
static constexpr int SocketRadius = 4;
static constexpr int SocketOffset = -6;
static constexpr int IndentStep   = 14;
static constexpr int FooterHeight = 24;
static constexpr int RowsPerPage = 8;



namespace Style {
	static const QColor NodeBg        = QColor(30, 30, 30, 220);
	static const QColor NodeBorder    = QColor(85, 85, 85);
	static const QColor HeaderBg      = QColor(60, 60, 60, 230);
	static const QColor RowBg         = QColor(40, 40, 40, 220);
	static const QColor RowAltBg      = QColor(46, 46, 46, 220);
	static const QColor Text          = Qt::white;
	static constexpr qreal Radius     = 6.0;
}


// ------------------------------------------------------------
// helpers
// ------------------------------------------------------------

struct VisibleRow {
	DebugVariable* node;
	int indent;
};

static void buildRows(DebugVariable* n,
					  int indent,
					  QHash<DebugVariable*, bool>& expanded,
					  QVector<VisibleRow>& out)
{
	out.push_back({n, indent});

	if (!n->hasChildren)
		return;
	if (!expanded.value(n, false))
		return;

	for (auto& c : n->children)
		buildRows(c.get(), indent + 1, expanded, out);
}

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

	p->setBrush(QColor(220, 220, 220));
	p->setPen(Qt::NoPen);
	p->drawPolygon(poly);
}

// ------------------------------------------------------------
// GraphicalNodeItem
// ------------------------------------------------------------

GraphicalNodeItem::GraphicalNodeItem(DebugVariable* node)
	: m_node(node)
{
	setFlag(ItemIsMovable);
	setFlag(ItemSendsGeometryChanges);
	recalculateWidth();
}

QRectF GraphicalNodeItem::boundingRect() const
{
	QVector<VisibleRow> rows;

	for (auto& c : m_node->children)
		buildRows(c.get(), 0,
				  const_cast<QHash<DebugVariable*, bool>&>(m_expanded),
				  rows);
	int totalRows = qMax(1, rows.size());
	int visibleRows = qMin(totalRows, RowsPerPage);
	int pageCount =
	    qMax(1, (rows.size() + RowsPerPage - 1) / RowsPerPage);
	int h = HeaderHeight
		  + visibleRows * RowHeight
	      + (pageCount > 1 ? FooterHeight : 0);

	return QRectF(-8, 0, m_width + 16, h);
}

void GraphicalNodeItem::paint(QPainter* p,
							  const QStyleOptionGraphicsItem*,
							  QWidget*)
{
	p->setRenderHint(QPainter::Antialiasing);

	QRectF outer = boundingRect().adjusted(8, 0, -8, 0);

	// shadow soft (facoltativa ma molto bella)
	p->setPen(Qt::NoPen);
	p->setBrush(QColor(0, 0, 0, 60));
	p->drawRoundedRect(outer.translated(2, 2),
					   Style::Radius, Style::Radius);

	// card background
	p->setBrush(Style::NodeBg);
	p->setPen(QPen(Style::NodeBorder, 1));
	p->drawRoundedRect(outer,
					   Style::Radius, Style::Radius);
	QPainterPath clip;
	clip.addRoundedRect(outer, Style::Radius, Style::Radius);
	p->setClipPath(clip);

	drawHeader(p, outer);
	drawSource(p);
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

void GraphicalNodeItem::drawHeader(QPainter* p, const QRectF& r)
{
	QRectF h(r.left(), r.top(), r.width(), HeaderHeight);

	p->setPen(Qt::NoPen);
	p->setBrush(Style::HeaderBg);

	QPainterPath path;
	path.moveTo(h.bottomLeft());
	path.lineTo(h.topLeft() + QPointF(0, Style::Radius));
	path.quadTo(h.topLeft(), h.topLeft() + QPointF(Style::Radius, 0));
	path.lineTo(h.topRight() - QPointF(Style::Radius, 0));
	path.quadTo(h.topRight(), h.topRight() + QPointF(0, Style::Radius));
	path.lineTo(h.bottomRight());
	path.closeSubpath();

	p->drawPath(path);

	p->setPen(Style::Text);

	QString t = m_node->address.isEmpty()
		? m_node->name
		: QString("%1 : %2").arg(m_node->name, m_node->address);

	p->drawText(
		h.adjusted(12, 0, -12, 0),
		Qt::AlignVCenter | Qt::AlignLeft,
		t
	);
}


void GraphicalNodeItem::drawSource(QPainter* p)
{
	QVector<VisibleRow> rows;

	for (auto& c : m_node->children)
		buildRows(c.get(), 0, m_expanded, rows);

	const int totalRows = qMax(1, rows.size());
	const int visibleRows = qMin(totalRows, RowsPerPage);
	int pageCount =
		qMax(1, (rows.size() + RowsPerPage - 1) / RowsPerPage);

	m_page = qBound(0, m_page, pageCount - 1);

	int start = m_page * RowsPerPage;
	int end   = qMin(start + RowsPerPage, rows.size());
	int y = HeaderHeight;

	if (rows.isEmpty()) {
		QRectF rowRect(0, y, m_width, RowHeight);

		p->setPen(Qt::NoPen);
		p->setBrush(QColor(45, 45, 45));
		p->drawRect(rowRect);

		p->setPen(Qt::white);

		p->drawText(
			rowRect.adjusted(12, 0, -12, 0),
			Qt::AlignVCenter | Qt::AlignLeft,
			m_node->value
		);
		return;
	}

	for (int i = start; i < end; ++i) {
		const VisibleRow& r = rows[i];
		DebugVariable* n = r.node;

		QRectF rowRect(0, y, m_width, RowHeight);

		QColor bg = (i % 2 == 0)
			? Style::RowBg
			: Style::RowAltBg;

		p->setBrush(bg);
		p->setPen(Qt::NoPen);
		p->drawRect(rowRect);

		p->setPen(QColor(255, 255, 255, 20));
		p->drawLine(rowRect.bottomLeft(), rowRect.bottomRight());

		int x = LeftPadding + r.indent * IndentStep + 5;

		if (n->hasChildren) {
			drawTriangle(
				p,
				QPointF(x - 10, y + RowHeight / 2),
				m_expanded.value(n, false)
			);
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

	// If this page has fewer rows than a full page, paint the remaining row
	// background so the footer stays at the same vertical position as the
	// hit-test area in mousePressEvent().
	for (int j = (end - start); j < visibleRows; ++j) {
		QRectF rowRect(0, y, m_width, RowHeight);

		const QColor bg = ((start + j) % 2 == 0)
			? Style::RowBg
			: Style::RowAltBg;

		p->setBrush(bg);
		p->setPen(Qt::NoPen);
		p->drawRect(rowRect);

		p->setPen(QColor(255, 255, 255, 20));
		p->drawLine(rowRect.bottomLeft(), rowRect.bottomRight());

		y += RowHeight;
	}

	// ---- footer ----
	if (pageCount <= 1)
		return;

	QRectF footerRect(
		0,
		HeaderHeight + visibleRows * RowHeight,
		m_width,
		FooterHeight
	);

	// background
	p->setPen(Qt::NoPen);
	p->setBrush(Style::NodeBg);
	p->drawRect(footerRect);

	// separator
	p->setPen(QColor(255, 255, 255, 25));
	p->drawLine(
		footerRect.topLeft() + QPointF(8, 0),
		footerRect.topRight() - QPointF(8, 0)
	);

	// text
	p->setPen(Qt::white);

	QString pageText =
		QString("◀  %1 / %2  ▶")
			.arg(m_page + 1)
			.arg(pageCount);

	p->drawText(
		footerRect,
		Qt::AlignCenter,
		pageText
	);


}

void GraphicalNodeItem::mousePressEvent(QGraphicsSceneMouseEvent *e) {
	if (e->pos().y() < HeaderHeight)
		return QGraphicsItem::mousePressEvent(e);

	QVector<VisibleRow> rows;
	for (auto &c : m_node->children)
		buildRows(c.get(), 0, m_expanded, rows);

	int pageCount = qMax(1, (rows.size() + RowsPerPage - 1) / RowsPerPage);

	qreal y = e->pos().y();
	if (pageCount > 1) {
		qreal footerTop = HeaderHeight + qMin(rows.size(), RowsPerPage) * RowHeight;

		if (y >= footerTop) {
			if (e->pos().x() < m_width / 2)
				m_page = qMax(0, m_page - 1);
			else
				m_page = qMin(pageCount - 1, m_page + 1);

			update();
			return;
		}
	}
	int localIdx = int((e->pos().y() - HeaderHeight) / RowHeight);
	int idx = m_page * RowsPerPage + localIdx;

	if (idx >= 0 && idx < rows.size()) {
		DebugVariable *n = rows[idx].node;
		if (n->hasChildren) {
			m_expanded[n] = !m_expanded.value(n, false);
			m_page = 0; // reset pagina
			prepareGeometryChange();
			update();
		}
	}
}

void GraphicalNodeItem::recalculateWidth()
{
	QFontMetrics fm{QFont()};
	int w = fm.horizontalAdvance(m_node->name) + 40;
	m_width = qMax(m_width, w);
}

void GraphicalNodeItem::addEdge(GraphicalEdgeItem* e)
{
	m_edges << e;
}

QPointF GraphicalNodeItem::inputPort() const
{
	return mapToScene(QPointF(0, HeaderHeight / 2));
}

QPointF GraphicalNodeItem::outputPortFor(DebugVariable* child) const
{
	if (!child)
		return mapToScene(QPointF(m_width, HeaderHeight / 2));

	QVector<VisibleRow> rows;
	for (auto& c : m_node->children)
		buildRows(c.get(), 0,
				  const_cast<QHash<DebugVariable*, bool>&>(m_expanded),
				  rows);

	for (int i = 0; i < rows.size(); ++i) {
		if (rows[i].node == child) {
			qreal y = HeaderHeight + i * RowHeight + RowHeight / 2;
			return mapToScene(QPointF(m_width, y));
		}
	}

	return mapToScene(QPointF(m_width, HeaderHeight / 2));
}

QVariant GraphicalNodeItem::itemChange(GraphicsItemChange c,
									   const QVariant& v)
{
	if (c == ItemPositionHasChanged)
		for (auto* e : m_edges)
			e->updatePosition();
	return QGraphicsItem::itemChange(c, v);
}

// ------------------------------------------------------------
// GraphicalEdgeItem
// ------------------------------------------------------------

GraphicalEdgeItem::GraphicalEdgeItem(GraphicalNodeItem* f,
									 GraphicalNodeItem* t,
									 DebugVariable* child)
	: m_from(f)
	, m_to(t)
	, m_fromChild(child)
{
	setZValue(-1);
	QPen pen(QColor(180,180,180));
	pen.setWidthF(2);
	setPen(pen);

	// ✔ CORRETTO: GraphicalEdgeItem NON è QObject
	QObject::connect(&m_timer, &QTimer::timeout,
			[this]() { tick(); });
}

void GraphicalEdgeItem::updatePosition()
{
	m_pos[0] = m_from->outputPortFor(m_fromChild);
	m_targetEnd = m_to->inputPort();
	if (!m_timer.isActive())
		m_timer.start(16);
}

void GraphicalEdgeItem::tick()
{
	m_pos[SEGMENTS - 1] = m_targetEnd;
	QPainterPath p;
	p.moveTo(m_pos[0]);
	p.lineTo(m_pos[SEGMENTS - 1]);
	setPath(p);
	m_timer.stop();
}

// ------------------------------------------------------------
// GraphicalVariablesView
// ------------------------------------------------------------

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

	connect(mk("+"), &QToolButton::clicked, this, &GraphicalVariablesView::zoomIn);
	connect(mk("-"), &QToolButton::clicked, this, &GraphicalVariablesView::zoomOut);
	connect(mk("⤢"), &QToolButton::clicked, this, &GraphicalVariablesView::fitGraph);
	connect(mk("⟳"), &QToolButton::clicked, this, &GraphicalVariablesView::resetZoom);

	overlay->move(10, 10);
	overlay->show();

}

GraphicalVariablesView::~GraphicalVariablesView() = default;

void GraphicalVariablesView::setSession(DebuggerSession* s)
{
	m_session = s;
	refresh();
}

void GraphicalVariablesView::refresh()
{
	if (!m_session) return;

	m_scene->clear();

	QHash<DebugVariable*, GraphicalNodeItem*> nodeMap;
	QHash<quintptr, GraphicalNodeItem*> addrMap;

	int y = 0;
	for (auto& v : m_session->variables()) {
		auto* item = new GraphicalNodeItem(v.get());
		m_scene->addItem(item);
		item->setPos(0, y);
		nodeMap[v.get()] = item;

		bool ok = false;
		        if (!v->address.isEmpty()) {
		            quintptr addr = v->address.toULongLong(&ok, 16);
		            if (ok)
		                addrMap[addr] = item;
		        }

		y += 150;
	}

	for (auto it = nodeMap.begin(); it != nodeMap.end(); ++it) {
		DebugVariable* root = it.key();
		for (auto& c : root->children) {
			if (!c->isPointer) continue;

			bool ok = false;
			quintptr target = c->value.toULongLong(&ok, 16);
			if (ok && target != 0 && addrMap.contains(target)) {
				auto* e = new GraphicalEdgeItem(
					it.value(), addrMap[target], c.get());
				m_scene->addItem(e);
				it.value()->addEdge(e);
				addrMap[target]->addEdge(e);
				e->updatePosition();
			}
		}
	}
}

// ------------------------------------------------------------
// Zoom / View controls
// ------------------------------------------------------------

void GraphicalVariablesView::zoomIn()
{
	scale(1.2, 1.2);
}

void GraphicalVariablesView::zoomOut()
{
	scale(1.0 / 1.2, 1.0 / 1.2);
}

void GraphicalVariablesView::resetZoom()
{
	resetTransform();
}

void GraphicalVariablesView::fitGraph()
{
	if (!scene())
		return;

	fitInView(
		scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40),
		Qt::KeepAspectRatio
	);
}

void GraphicalVariablesView::wheelEvent(QWheelEvent* event)
{
	const double factor = 1.15;

	if (event->angleDelta().y() > 0)
		scale(factor, factor);
	else
		scale(1.0 / factor, 1.0 / factor);

	event->accept();
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

