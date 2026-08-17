/*
 * Copyright (c) 2026, Manuele Conti
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
#include "OrthogonalEdgeRouter.h"

#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QVBoxLayout>
#include <QToolButton>
#include <QStyle>
#include <QMenu>
#include <QInputDialog>
#include <QLineEdit>
#include <QLineF>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>


// =======================================================
// Helpers / layout constants (come versione vecchia)
// =======================================================
static constexpr qreal SocketRadius = 5.0;
static constexpr qreal ArrowLength = 11.0;
static constexpr qreal Pi = 3.14159265358979323846;
static constexpr int IndentStep   = 14;
static constexpr int FooterHeight = 24;
static constexpr int RowsPerPage = 8;
static constexpr int MinimumNodeWidth = 260;
static constexpr int MaximumNodeWidth = 440;
static constexpr qreal MinimumZoom = 0.25;
static constexpr qreal MaximumZoom = 4.0;



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

static void applyBoundedZoom(QGraphicsView* view, qreal factor)
{
	const qreal currentZoom = view->transform().m11();
	if (currentZoom <= 0.0)
		return;
	const qreal targetZoom = qBound(MinimumZoom, currentZoom * factor, MaximumZoom);
	view->scale(targetZoom / currentZoom, targetZoom / currentZoom);
}

static bool isInlineStruct(const QString& s)
{
	const QString v = s.trimmed();
	return v.startsWith('{') && v.endsWith('}');
}

static QString pointerAddressFromValue(const QString& value)
{
	static const QRegularExpression addressPattern(
		QStringLiteral(R"(0x[0-9a-fA-F]+)"));
	const QRegularExpressionMatch match = addressPattern.match(value);
	return match.hasMatch() ? match.captured(0).toLower() : QString();
}

static QStringList splitTopLevelCommasLocal(const QString& s)
{
	QStringList out;
	int depth = 0;
	int start = 0;
	for (int i = 0; i < s.size(); ++i) {
		const QChar c = s[i];
		if (c == '{') depth++;
		else if (c == '}') depth--;
		else if (c == ',' && depth == 0) {
			out << s.mid(start, i - start).trimmed();
			start = i + 1;
		}
	}
	out << s.mid(start).trimmed();
	return out;
}

static void expandInlineStructIntoChildrenLocal(
	DebugVariable* node,
	const QString& value,
	int depth,
	int maxDepth)
{
	if (!node) return;
	if (depth >= maxDepth) return;

	QString v = value.trimmed();
	if (!isInlineStruct(v))
		return;

	v = v.mid(1, v.size() - 2).trimmed();
	if (v.isEmpty())
		return;

	const QStringList entries = splitTopLevelCommasLocal(v);
	for (const QString& e : entries) {
		const int eq = e.indexOf('=');
		if (eq <= 0)
			continue;

		auto c = std::make_unique<DebugVariable>();
		c->name = e.left(eq).trimmed();
		c->value = e.mid(eq + 1).trimmed();
		c->parent = node;
		const QString parentExpression = node->fullPath();
		if (!parentExpression.isEmpty()) {
			const QString base = node->isPointer
				? QStringLiteral("(*(%1))").arg(parentExpression)
				: QStringLiteral("(%1)").arg(parentExpression);
			c->expression = c->name.startsWith('[')
				? base + c->name
				: base + QStringLiteral(".") + c->name;
		}

		c->pointeeAddress = pointerAddressFromValue(c->value);
		c->isPointer = !c->pointeeAddress.isEmpty();
		c->hasChildren = isInlineStruct(c->value);

		if (c->hasChildren)
			expandInlineStructIntoChildrenLocal(c.get(), c->value, depth + 1, maxDepth);

		node->children.push_back(std::move(c));
	}

	if (!node->children.empty())
		node->hasChildren = true;
}

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

static DebugVariable* rootOf(DebugVariable* v)
{
	if (!v) return nullptr;
	while (v->parent)
		v = v->parent;
	return v;
}

static DebugVariable* findVariableByPath(DebugVariable* variable, const QString& path)
{
	if (!variable)
		return nullptr;
	if (variable->fullPath() == path)
		return variable;
	for (auto& child : variable->children)
		if (DebugVariable* found = findVariableByPath(child.get(), path))
			return found;
	return nullptr;
}

static void indexVariablePaths(DebugVariable* variable,
	                           QHash<QString, DebugVariable*>& index)
{
	if (!variable)
		return;
	index.insert(variable->fullPath(), variable);
	for (auto& child : variable->children)
		indexVariablePaths(child.get(), index);
}

static QString layoutKeyForVariable(DebugVariable* v)
{
	if (!v)
		return {};

	if (!v->address.isEmpty())
		return RuntimeObjectGraph::identityFor(v->address, v->type, v->fullPath());

	const QString path = v->fullPath();
	if (!path.isEmpty())
		return QStringLiteral("var:%1").arg(path);

	return QStringLiteral("name:%1").arg(v->name);
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

static qreal normalizedAngle(qreal angle)
{
	constexpr qreal TwoPi = 2.0 * Pi;

	while (angle < 0.0)
		angle += TwoPi;
	while (angle >= TwoPi)
		angle -= TwoPi;

	return angle;
}

static qreal positiveAngleDistance(qreal from, qreal to)
{
	return normalizedAngle(to - from);
}

static bool circleThroughPoints(const QPointF& p1,
								const QPointF& p2,
								const QPointF& p3,
								QPointF& center,
								qreal& radius)
{
	const qreal x1 = p1.x();
	const qreal y1 = p1.y();
	const qreal x2 = p2.x();
	const qreal y2 = p2.y();
	const qreal x3 = p3.x();
	const qreal y3 = p3.y();

	const qreal d =
		2.0 *
		(x1 * (y2 - y3) +
		 x2 * (y3 - y1) +
		 x3 * (y1 - y2));

	// Nearly collinear points would produce an excessively large circle.
	// A straight line is cleaner and matches DDD's fallback behavior.
	if (std::abs(d) < 0.001)
		return false;

	const qreal p1sq = x1 * x1 + y1 * y1;
	const qreal p2sq = x2 * x2 + y2 * y2;
	const qreal p3sq = x3 * x3 + y3 * y3;

	center.setX(
		(p1sq * (y2 - y3) +
		 p2sq * (y3 - y1) +
		 p3sq * (y1 - y2)) / d);

	center.setY(
		(p1sq * (x3 - x2) +
		 p2sq * (x1 - x3) +
		 p3sq * (x2 - x1)) / d);

	radius = QLineF(center, p1).length();
	return std::isfinite(radius) && radius > 1.0;
}

static void appendCircularArc(QPainterPath& path,
							  const QPointF& start,
							  const QPointF& through,
							  const QPointF& end,
							  qreal sourceGap)
{
	QPointF center;
	qreal radius = 0.0;

	if (!circleThroughPoints(start, through, end, center, radius)) {
		QLineF line(start, end);

		if (line.length() <= sourceGap) {
			path.moveTo(start);
			path.lineTo(end);
			return;
		}

		line.setLength(sourceGap);
		path.moveTo(line.p2());
		path.lineTo(end);
		return;
	}

	const qreal startAngle =
		std::atan2(start.y() - center.y(), start.x() - center.x());
	const qreal hintAngle =
		std::atan2(through.y() - center.y(), through.x() - center.x());
	const qreal endAngle =
		std::atan2(end.y() - center.y(), end.x() - center.x());

	const qreal ccwTotal = positiveAngleDistance(startAngle, endAngle);
	const qreal ccwHint = positiveAngleDistance(startAngle, hintAngle);

	const qreal sweep =
		ccwHint <= ccwTotal
			? ccwTotal
			: -positiveAngleDistance(endAngle, startAngle);

	// Skip the portion of the arc covered by the hollow source socket.
	const qreal gapAngle =
		qMin(std::abs(sweep) * 0.40,
			 sourceGap / qMax<qreal>(radius, 1.0));

	const qreal firstAngle =
		startAngle + std::copysign(gapAngle, sweep);
	const qreal remainingSweep =
		sweep - std::copysign(gapAngle, sweep);

	path.moveTo(
		center +
		QPointF(radius * std::cos(firstAngle),
				radius * std::sin(firstAngle)));

	// Sample the circular arc densely enough to remain smooth at every zoom
	// level without depending on QPainterPath::arcTo angle conventions.
	const int segments =
		qBound(
			8,
			int(std::ceil(std::abs(remainingSweep) * radius / 6.0)),
			96);

	for (int i = 1; i <= segments; ++i) {
		const qreal t = qreal(i) / qreal(segments);
		const qreal angle = firstAngle + remainingSweep * t;

		path.lineTo(
			center +
			QPointF(radius * std::cos(angle),
					radius * std::sin(angle)));
	}
}

// ------------------------------------------------------------
// GraphicalNodeItem
// ------------------------------------------------------------

GraphicalNodeItem::GraphicalNodeItem(DebugVariable* node,
	                                 DebuggerSession* session,
	                                 QString layoutKey)
	: m_node(node)
	, m_session(session)
	, m_layoutKey(std::move(layoutKey))
{
	setFlag(ItemIsMovable);
	setFlag(ItemSendsGeometryChanges);
	recalculateWidth();
}

const QVector<VisibleRow>& GraphicalNodeItem::cachedRows() const
{
	if (m_rowsDirty) {
		m_cachedRows.clear();
		for (auto& c : m_node->children)
			buildRows(c.get(), 0,
					  const_cast<QHash<DebugVariable*, bool>&>(m_expanded),
					  m_cachedRows);
		m_rowsDirty = false;
	}
	return m_cachedRows;
}

DebugVariable* GraphicalNodeItem::variableAt(const QPointF& localPos) const
{
	if (!m_node)
		return nullptr;

	if (localPos.y() < HeaderHeight)
		return nullptr;

	const QVector<VisibleRow>& rows = cachedRows();

	if (rows.isEmpty()) {
		// Single value row (used for leaf values / pointers)
		if (localPos.y() >= HeaderHeight && localPos.y() < HeaderHeight + RowHeight)
			return m_node;
		return nullptr;
	}

	const int pageCount = qMax(1, (rows.size() + RowsPerPage - 1) / RowsPerPage);
	const int page = qBound(0, m_page, pageCount - 1);
	const int start = page * RowsPerPage;
	const int end = qMin(start + RowsPerPage, rows.size());

	const int localIdx = int((localPos.y() - HeaderHeight) / RowHeight);
	const int idx = start + localIdx;
	if (idx < start || idx >= end)
		return nullptr;

	return rows[idx].node;
}

QRectF GraphicalNodeItem::boundingRect() const
{
	const QVector<VisibleRow>& rows = cachedRows();

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
	p->setBrush(m_node->enabled ? Style::HeaderBg : QColor(75, 75, 75, 180));

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
	if (!m_node->enabled)
		t += QObject::tr("  [disabled]");

	const QString visibleTitle = p->fontMetrics().elidedText(
		t, Qt::ElideMiddle, qMax(1, int(h.width()) - 24));
	p->drawText(
		h.adjusted(12, 0, -12, 0),
		Qt::AlignVCenter | Qt::AlignLeft,
		visibleTitle
	);
}


void GraphicalNodeItem::drawSource(QPainter* p)
{
	const QVector<VisibleRow>& rows = cachedRows();

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

		const QString displayedValue = m_session
			? m_session->formattedValue(m_node) : m_node->value;
		const QString visibleValue = p->fontMetrics().elidedText(
			displayedValue, Qt::ElideMiddle, qMax(1, m_width - 24));
		p->drawText(
			rowRect.adjusted(12, 0, -12, 0),
			Qt::AlignVCenter | Qt::AlignLeft,
			visibleValue
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
					   .arg(n->name, m_session ? m_session->formattedValue(n) : n->value);
		} else {
			if (m_expanded.value(n, false))
				text = n->name;
			else
				text = QString("%1 = []").arg(n->name);
		}

		const QString visibleText = p->fontMetrics().elidedText(
			text, Qt::ElideMiddle, qMax(1, m_width - x - 14));
		p->drawText(
			QRectF(x, y, qMax(1, m_width - x - 12), RowHeight),
			Qt::AlignVCenter | Qt::AlignLeft,
			visibleText);

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

	// A scrollbar makes it immediately visible that more rows are available.
	const QRectF scrollTrack(
		m_width - 5,
		HeaderHeight + 4,
		3,
		visibleRows * RowHeight - 8);
	p->setPen(Qt::NoPen);
	p->setBrush(QColor(255, 255, 255, 30));
	p->drawRoundedRect(scrollTrack, 1.5, 1.5);
	const qreal thumbHeight = qMax<qreal>(18.0, scrollTrack.height() / pageCount);
	const qreal thumbTravel = scrollTrack.height() - thumbHeight;
	const qreal thumbY = scrollTrack.top()
		+ thumbTravel * m_page / qMax(1, pageCount - 1);
	p->setBrush(QColor(147, 197, 253, 210));
	p->drawRoundedRect(
		QRectF(scrollTrack.left(), thumbY, scrollTrack.width(), thumbHeight),
		1.5, 1.5);

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
		QObject::tr("Page %1/%2  •  scroll")
			.arg(m_page + 1)
			.arg(pageCount);

	p->drawText(
		QRectF(footerRect.left(), footerRect.top(), 34, footerRect.height()),
		Qt::AlignCenter,
		QStringLiteral("◀"));
	p->drawText(
		footerRect.adjusted(34, 0, -34, 0),
		Qt::AlignCenter,
		pageText
	);
	p->drawText(
		QRectF(footerRect.right() - 34, footerRect.top(), 34, footerRect.height()),
		Qt::AlignCenter,
		QStringLiteral("▶"));
}

void GraphicalNodeItem::setPage(int page, int pageCount)
{
	const int nextPage = qBound(0, page, qMax(1, pageCount) - 1);
	if (nextPage == m_page)
		return;
	m_page = nextPage;
	update();
	for (auto* edge : m_edges)
		edge->updatePosition();
}

void GraphicalNodeItem::mousePressEvent(QGraphicsSceneMouseEvent *e) {
	if (e->pos().y() < HeaderHeight) {
		m_dragStartPosition = pos();
		m_draggingHeader = true;
		return QGraphicsItem::mousePressEvent(e);
	}
	if (e->button() != Qt::LeftButton)
		return QGraphicsItem::mousePressEvent(e);

	const QVector<VisibleRow>& rows = cachedRows();

	int pageCount = qMax(1, (rows.size() + RowsPerPage - 1) / RowsPerPage);

	qreal y = e->pos().y();
	if (pageCount > 1) {
		qreal footerTop = HeaderHeight + qMin(rows.size(), RowsPerPage) * RowHeight;

		if (y >= footerTop) {
			if (e->pos().x() < m_width / 2)
				setPage(m_page - 1, pageCount);
			else
				setPage(m_page + 1, pageCount);
			e->accept();
			return;
		}
	}
	int localIdx = int((e->pos().y() - HeaderHeight) / RowHeight);
	int idx = m_page * RowsPerPage + localIdx;

	if (idx >= 0 && idx < rows.size()) {
		DebugVariable *n = rows[idx].node;
		if (n->hasChildren) {
			prepareGeometryChange();
			m_rowsDirty = true;
			m_expanded[n] = !m_expanded.value(n, false);
			if (m_expanded.value(n))
				m_expandedExpressionState.insert(n->fullPath());
			else
				m_expandedExpressionState.remove(n->fullPath());
			m_page = 0; // reset pagina
			recalculateWidth();
			update();
			for (auto* edge : m_edges)
				edge->updatePosition();
			if (m_onGeometryChanged)
				m_onGeometryChanged();
		}
	}
}

void GraphicalNodeItem::wheelEvent(QGraphicsSceneWheelEvent* event)
{
	if (event->orientation() != Qt::Vertical || event->delta() == 0) {
		event->ignore();
		return;
	}

	const QVector<VisibleRow>& rows = cachedRows();
	const int pageCount = qMax(1, (rows.size() + RowsPerPage - 1) / RowsPerPage);
	if (pageCount <= 1) {
		event->ignore();
		return;
	}

	setPage(m_page + (event->delta() < 0 ? 1 : -1), pageCount);
	event->accept();
}

void GraphicalNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
	QGraphicsItem::mouseReleaseEvent(event);
	if (m_draggingHeader && QLineF(m_dragStartPosition, pos()).length() > 1.0 &&
	    !m_layoutKey.isEmpty() && m_onUserMoved) {
		m_onUserMoved(m_layoutKey, pos());
	}
	m_draggingHeader = false;
}

void GraphicalNodeItem::recalculateWidth()
{
	QFontMetrics fm{QFont()};
	int w = fm.horizontalAdvance(m_node->address.isEmpty()
		? m_node->name
		: QString("%1 : %2").arg(m_node->name, m_node->address)) + 32;

	if (m_node->children.empty()) {
		w = qMax(w, fm.horizontalAdvance(
			m_session ? m_session->formattedValue(m_node) : m_node->value) + 24);
	} else {
		const QVector<VisibleRow>& rows = cachedRows();
		for (const VisibleRow& row : rows) {
			const DebugVariable* value = row.node;
			const QString displayedValue = m_session
				? m_session->formattedValue(value) : value->value;
			const QString text = value->hasChildren
				? QStringLiteral("%1 = []").arg(value->name)
				: QStringLiteral("%1 = %2").arg(value->name, displayedValue);
			w = qMax(w, fm.horizontalAdvance(text) + 32 + row.indent * IndentStep);
		}
	}

	m_width = qBound(MinimumNodeWidth, w, MaximumNodeWidth);
}

void GraphicalNodeItem::addEdge(GraphicalEdgeItem* e)
{
	m_edges << e;
}

void GraphicalNodeItem::setPositionChangedCallback(
	std::function<void(const QString&, const QPointF&)> cb)
{
	m_onPositionChanged = std::move(cb);
}

void GraphicalNodeItem::setUserMovedCallback(
	std::function<void(const QString&, const QPointF&)> cb)
{
	m_onUserMoved = std::move(cb);
}

void GraphicalNodeItem::setGeometryChangedCallback(std::function<void()> cb)
{
	m_onGeometryChanged = std::move(cb);
}

QPointF GraphicalNodeItem::inputPort() const
{
	return mapToScene(QPointF(0, HeaderHeight / 2));
}

QPointF GraphicalNodeItem::outputPortFor(DebugVariable* child) const
{
	Q_UNUSED(child);

	// Keep one stable hollow socket in the right side of the header.
	return mapToScene(
		QPointF(m_width - (SocketRadius * 2.0),
				HeaderHeight / 2.0));
}

QPointF GraphicalNodeItem::outputPortForExpression(const QString& expression) const
{
	Q_UNUSED(expression);

	// The expression remains the semantic edge identity, while every
	// connection uses the same visual socket in the card header.
	return mapToScene(
		QPointF(m_width - (SocketRadius * 2.0),
				HeaderHeight / 2.0));
}

void GraphicalNodeItem::setExpandedRecursively(bool expanded, int maxDepth)
{
	prepareGeometryChange();
	m_rowsDirty = true;
	std::function<void(DebugVariable*, int)> visit = [&](DebugVariable* value, int depth) {
		if (!value || depth > maxDepth) return;
		if (value->hasChildren) {
			m_expanded[value] = expanded;
			if (expanded)
				m_expandedExpressionState.insert(value->fullPath());
			else
				m_expandedExpressionState.remove(value->fullPath());
		}
		for (auto& child : value->children) visit(child.get(), depth + 1);
	};
	visit(m_node, 0);
	m_page = 0;
	recalculateWidth();
	update();
	for (auto* edge : m_edges)
		edge->updatePosition();
	if (m_onGeometryChanged)
		m_onGeometryChanged();
}

QSet<QString> GraphicalNodeItem::expandedExpressions() const
{
	return m_expandedExpressionState;
}

void GraphicalNodeItem::restoreExpandedExpressions(const QSet<QString>& expressions)
{
	m_rowsDirty = true;
	m_expandedExpressionState = expressions;
	std::function<void(DebugVariable*)> restore = [&](DebugVariable* value) {
		if (!value)
			return;
		if (value->hasChildren)
			m_expanded[value] = expressions.contains(value->fullPath());
		for (auto& child : value->children)
			restore(child.get());
	};
	restore(m_node);
	recalculateWidth();
}

QVariant GraphicalNodeItem::itemChange(GraphicsItemChange c,
									   const QVariant& v)
{
	if (c == ItemPositionHasChanged) {
		for (auto* e : m_edges)
			e->updatePosition();
		if (!m_layoutKey.isEmpty() && m_onPositionChanged)
			m_onPositionChanged(m_layoutKey, pos());
	}
	return QGraphicsItem::itemChange(c, v);
}

// ------------------------------------------------------------
// GraphicalEdgeItem
// ------------------------------------------------------------

GraphicalEdgeItem::GraphicalEdgeItem(GraphicalNodeItem* f,
									 GraphicalNodeItem* t,
									 QString sourceObjectId,
									 QString sourceExpression,
									 QString destinationObjectId,
									 RuntimeChangeState change)
	: m_from(f)
	, m_to(t)
	, m_sourceObjectId(std::move(sourceObjectId))
	, m_sourceExpression(std::move(sourceExpression))
	, m_destinationObjectId(std::move(destinationObjectId))
	, m_change(change)
{
	setZValue(1);
	QPen pen(QColor(180, 180, 180, 210));
	pen.setWidthF(2);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	setPen(pen);

	const QString routeKey = RuntimeObjectGraph::referenceIdentity(m_sourceObjectId, m_sourceExpression);
	const int lane = static_cast<int>(qHash(routeKey) % 9) - 4;

	// Spread parallel references by moving the circular-arc hint point away
	// from the source-to-destination chord.
	m_routeOffset = lane * 18.0;

	// GraphicalEdgeItem is not a QObject, so connect through the timer object.
	QObject::connect(&m_timer, &QTimer::timeout,
			[this]() { tick(); });
}

static QRectF visibleCardRect(const GraphicalNodeItem* node)
{
	// GraphicalNodeItem::boundingRect() includes an 8 px horizontal margin.
	// Remove it so edges attach to the visible card border.
	return node->sceneBoundingRect().adjusted(8.0, 0.0, -8.0, 0.0);
}

static QPointF pointOnRectToward(const QRectF& rect, const QPointF& toward)
{
	const QPointF center = rect.center();
	const QPointF delta = toward - center;

	if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))
		return center;

	const qreal halfWidth = rect.width() * 0.5;
	const qreal halfHeight = rect.height() * 0.5;

	const qreal nx =
		halfWidth > 0.0 ? std::abs(delta.x()) / halfWidth : 0.0;
	const qreal ny =
		halfHeight > 0.0 ? std::abs(delta.y()) / halfHeight : 0.0;

	const qreal divisor = qMax(nx, ny);
	if (divisor <= 0.0)
		return center;

	return center + delta / divisor;
}

void GraphicalEdgeItem::updatePosition()
{
	const QRectF targetRect = visibleCardRect(m_to);
	m_pos[0] = m_from->outputPortForExpression(m_sourceExpression);

	if (m_from == m_to) {
		m_targetEnd = m_pos[0];
	} else {
		// Keep the destination attachment on the natural perimeter point.
		// The router enforces the final segment perpendicular to that side.
		m_targetEnd = pointOnRectToward(targetRect, m_pos[0]);
	}

	if (!m_timer.isActive())
		m_timer.start(16);
}

static bool segmentIntersectsRect(const QPointF& start,
								  const QPointF& end,
								  const QRectF& rect)
{
	if (rect.contains(start) || rect.contains(end))
		return true;

	const QLineF segment(start, end);
	const QLineF top(rect.topLeft(), rect.topRight());
	const QLineF right(rect.topRight(), rect.bottomRight());
	const QLineF bottom(rect.bottomRight(), rect.bottomLeft());
	const QLineF left(rect.bottomLeft(), rect.topLeft());

	QPointF intersection;
	return segment.intersects(top, &intersection) == QLineF::BoundedIntersection
		|| segment.intersects(right, &intersection) == QLineF::BoundedIntersection
		|| segment.intersects(bottom, &intersection) == QLineF::BoundedIntersection
		|| segment.intersects(left, &intersection) == QLineF::BoundedIntersection;
}

static void appendStraightEdge(QPainterPath& path,
							   const QPointF& start,
							   const QPointF& end,
							   qreal sourceGap)
{
	QLineF line(start, end);

	if (line.length() <= sourceGap) {
		path.moveTo(start);
		path.lineTo(end);
		return;
	}

	line.setLength(sourceGap);
	path.moveTo(line.p2());
	path.lineTo(end);
}

static qreal routeLength(const QPointF& start,
						 const QVector<QPointF>& bends,
						 const QPointF& end)
{
	qreal total = 0.0;
	QPointF previous = start;

	for (const QPointF& bend : bends) {
		total += QLineF(previous, bend).length();
		previous = bend;
	}

	return total + QLineF(previous, end).length();
}

static bool routeCrossesRect(const QPointF& start,
							 const QVector<QPointF>& bends,
							 const QPointF& end,
							 const QRectF& rect)
{
	QPointF previous = start;

	for (const QPointF& bend : bends) {
		if (segmentIntersectsRect(previous, bend, rect))
			return true;
		previous = bend;
	}

	return segmentIntersectsRect(previous, end, rect);
}

static QVector<QPointF> shortestDetour(
	const QPointF& start,
	const QPointF& end,
	const QRectF& blockingRect,
	qreal laneOffset)
{
	// Keep the detour close to the obstacle. The lane offset only separates
	// otherwise identical references instead of producing oversized arcs.
	const qreal clearance =
		18.0 + qMin<qreal>(18.0, std::abs(laneOffset) * 0.20);
	const QRectF routeRect =
		blockingRect.adjusted(-clearance, -clearance, clearance, clearance);

	const bool leftToRight = start.x() <= end.x();
	const bool topToBottom = start.y() <= end.y();

	const QPointF topA =
		leftToRight ? routeRect.topLeft() : routeRect.topRight();
	const QPointF topB =
		leftToRight ? routeRect.topRight() : routeRect.topLeft();
	const QPointF bottomA =
		leftToRight ? routeRect.bottomLeft() : routeRect.bottomRight();
	const QPointF bottomB =
		leftToRight ? routeRect.bottomRight() : routeRect.bottomLeft();
	const QPointF leftA =
		topToBottom ? routeRect.topLeft() : routeRect.bottomLeft();
	const QPointF leftB =
		topToBottom ? routeRect.bottomLeft() : routeRect.topLeft();
	const QPointF rightA =
		topToBottom ? routeRect.topRight() : routeRect.bottomRight();
	const QPointF rightB =
		topToBottom ? routeRect.bottomRight() : routeRect.topRight();

	const QVector<QVector<QPointF>> candidates = {
		{topA, topB},
		{bottomA, bottomB},
		{leftA, leftB},
		{rightA, rightB}
	};

	QVector<QPointF> best;
	qreal bestLength = -1.0;
	const QRectF forbidden =
		blockingRect.adjusted(-2.0, -2.0, 2.0, 2.0);

	for (const QVector<QPointF>& candidate : candidates) {
		if (routeCrossesRect(start, candidate, end, forbidden))
			continue;

		const qreal candidateLength =
			routeLength(start, candidate, end);
		if (bestLength < 0.0 || candidateLength < bestLength) {
			bestLength = candidateLength;
			best = candidate;
		}
	}

	return best;
}

static QPointF pointToward(
	const QPointF& from,
	const QPointF& toward,
	qreal distance)
{
	QLineF line(from, toward);
	if (line.length() < 0.001)
		return from;

	line.setLength(qMin(distance, line.length()));
	return line.p2();
}

static void appendRoundedRoute(
	QPainterPath& path,
	const QPointF& start,
	const QVector<QPointF>& bends,
	const QPointF& end,
	qreal sourceGap)
{
	QLineF sourceLine(start, bends.isEmpty() ? end : bends.first());
	QPointF routedStart = start;

	if (sourceLine.length() > sourceGap) {
		sourceLine.setLength(sourceGap);
		routedStart = sourceLine.p2();
	}

	path.moveTo(routedStart);

	if (bends.isEmpty()) {
		path.lineTo(end);
		return;
	}

	QVector<QPointF> points;
	points << routedStart;
	for (const QPointF& bend : bends)
		points << bend;
	points << end;

	constexpr qreal CornerRadius = 18.0;

	for (int i = 1; i + 1 < points.size(); ++i) {
		const QPointF& previous = points[i - 1];
		const QPointF& corner = points[i];
		const QPointF& next = points[i + 1];

		const qreal incomingLength =
			QLineF(corner, previous).length();
		const qreal outgoingLength =
			QLineF(corner, next).length();
		const qreal radius =
			qMin(CornerRadius,
				 qMin(incomingLength * 0.28,
					  outgoingLength * 0.28));

		const QPointF enter =
			pointToward(corner, previous, radius);
		const QPointF leave =
			pointToward(corner, next, radius);

		path.lineTo(enter);
		path.quadTo(corner, leave);
	}

	path.lineTo(end);
}

void GraphicalEdgeItem::tick()
{
	m_pos[SEGMENTS - 1] = m_targetEnd;

	const QPointF start = m_pos[0];
	const QPointF end = m_pos[SEGMENTS - 1];
	const qreal sourceGap = SocketRadius + 1.5;

	OrthogonalEdgeRouter::Request request;
	request.source = start;
	request.target = end;
	request.sourceNormal = QPointF(1.0, 0.0);
	request.sourceGap = sourceGap;
	request.laneOffset = m_routeOffset;
	request.stabilityKey = RuntimeObjectGraph::referenceIdentity(
		m_sourceObjectId, m_sourceExpression);

	if (m_from != m_to) {
		const QRectF targetRect = visibleCardRect(m_to);
		const qreal distances[] = {
			std::abs(end.x() - targetRect.left()),
			std::abs(end.x() - targetRect.right()),
			std::abs(end.y() - targetRect.top()),
			std::abs(end.y() - targetRect.bottom())
		};

		int side = 0;
		for (int i = 1; i < 4; ++i)
			if (distances[i] < distances[side])
				side = i;

		switch (side) {
		case 0: request.targetNormal = QPointF(-1.0, 0.0); break;
		case 1: request.targetNormal = QPointF( 1.0, 0.0); break;
		case 2: request.targetNormal = QPointF( 0.0,-1.0); break;
		default: request.targetNormal = QPointF(0.0, 1.0); break;
		}
	}

	if (scene()) {
		for (QGraphicsItem* item : scene()->items()) {
			if (auto* node = qgraphicsitem_cast<GraphicalNodeItem*>(item)) {
				const QRectF cardRect = visibleCardRect(node);
				if (!request.routingBounds.isValid()
				    || request.routingBounds.isEmpty())
					request.routingBounds = cardRect;
				else
					request.routingBounds =
						request.routingBounds.united(cardRect);

				if (node != m_from && node != m_to)
					request.obstacles.push_back(cardRect);
				continue;
			}

			auto* edge = qgraphicsitem_cast<GraphicalEdgeItem*>(item);
			if (edge && edge != this && !edge->path().isEmpty())
				request.existingEdges.push_back(edge->path());
		}
	}

	OrthogonalEdgeRouter::Result result;
	if (m_from == m_to) {
		result = OrthogonalEdgeRouter::routeSelfLoop(
			request, visibleCardRect(m_from));
		m_pos[SEGMENTS - 1] = result.endPoint;
	} else {
		result = OrthogonalEdgeRouter::route(request);
	}

	m_labelPosition = result.labelPosition;
	setPath(result.path);
	m_timer.stop();
}

QRectF GraphicalEdgeItem::boundingRect() const
{
	// Keep room for the connection label around the path.
	QRectF rect =
		QGraphicsPathItem::boundingRect().adjusted(
			-42.0, -42.0, 42.0, 42.0);

	const QRectF socketRect(
		m_pos[0].x() - SocketRadius - 2.0,
		m_pos[0].y() - SocketRadius - 2.0,
		(SocketRadius + 2.0) * 2.0,
		(SocketRadius + 2.0) * 2.0);

	return rect.united(socketRect);
}

void GraphicalEdgeItem::paint(
	QPainter* painter,
	const QStyleOptionGraphicsItem* option,
	QWidget* widget)
{
	painter->setRenderHint(QPainter::Antialiasing);

	// Draw the edge path first.
	QGraphicsPathItem::paint(painter, option, widget);

	const QColor edgeColor = pen().color();

	// Draw a hollow source socket. The edge itself starts outside the socket,
	// so its center remains genuinely transparent.
	painter->save();

	QPen socketPen(edgeColor);
	socketPen.setWidthF(2.0);
	socketPen.setCapStyle(Qt::RoundCap);
	socketPen.setJoinStyle(Qt::RoundJoin);

	painter->setPen(socketPen);
	painter->setBrush(Qt::NoBrush);
	painter->drawEllipse(
		m_pos[0],
		SocketRadius,
		SocketRadius);

	painter->restore();

	const QPainterPath edgePath = path();
	if (edgePath.isEmpty())
		return;

	QString edgeLabel;
	const QString expression = m_sourceExpression.trimmed();
	if (!expression.isEmpty()) {
		static const QRegularExpression fieldPattern(
			QStringLiteral(R"(([A-Za-z_][A-Za-z0-9_]*)\s*(?:\)|\])*\s*$)"));
		const QRegularExpressionMatch match = fieldPattern.match(expression);
		edgeLabel = match.hasMatch() ? match.captured(1) : expression;
	}

	if (!edgeLabel.isEmpty()) {
		QFont labelFont = painter->font();
		if (labelFont.pointSizeF() > 0.0)
			labelFont.setPointSizeF(
				qMax<qreal>(8.0, labelFont.pointSizeF() - 1.0));

		const QFontMetricsF fm(labelFont);
		const QSizeF size(fm.horizontalAdvance(edgeLabel), fm.height());
		const QRectF labelRect(
			m_labelPosition.x() - size.width() * 0.5 - 5.0,
			m_labelPosition.y() - size.height() * 0.5 - 2.0,
			size.width() + 10.0,
			size.height() + 4.0);

		painter->save();
		painter->setFont(labelFont);
		painter->setPen(Qt::NoPen);
		painter->setBrush(QColor(30, 30, 30, 225));
		painter->drawRoundedRect(labelRect, 4.0, 4.0);
		painter->setPen(QColor(225, 225, 225));
		painter->drawText(labelRect, Qt::AlignCenter, edgeLabel);
		painter->restore();
	}

	// Build a filled DDD-style arrowhead from the final path tangent.
	const QPointF tip = edgePath.pointAtPercent(1.0);
	const QPointF before = edgePath.pointAtPercent(0.985);

	QPointF direction = before - tip;
	const qreal directionLength =
		std::hypot(direction.x(), direction.y());

	if (directionLength < 0.001)
		return;

	direction /= directionLength;

	constexpr qreal ArrowHalfAngleDegrees = 17.0;
	const qreal angle =
		std::atan2(direction.y(), direction.x());
	const qreal delta =
		ArrowHalfAngleDegrees * Pi / 180.0;

	QPolygonF arrow;
	arrow
		<< tip
		<< QPointF(
			tip.x() + ArrowLength * std::cos(angle + delta),
			tip.y() + ArrowLength * std::sin(angle + delta))
		<< QPointF(
			tip.x() + ArrowLength * std::cos(angle - delta),
			tip.y() + ArrowLength * std::sin(angle - delta));

	painter->save();
	painter->setPen(Qt::NoPen);
	painter->setBrush(edgeColor);
	painter->drawPolygon(arrow);
	painter->restore();
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
		width: 10px;
		margin: 10px 4px 10px 4px;
	}

	QScrollBar::handle:vertical {
		background: #CBD5E1;
		border-radius: 5px;
		min-height: 32px;
	}

	QScrollBar::handle:vertical:hover {
		background: #94A3B8;
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
		height: 10px;
		margin: 4px 10px 4px 10px;
	}

	QScrollBar::handle:horizontal {
		background: #CBD5E1;
		border-radius: 5px;
		min-width: 32px;
	}

	QScrollBar::handle:horizontal:hover {
		background: #94A3B8;
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

		QToolButton:checked {
			color: white;
			background: #2563EB;
			border: 1px solid #60A5FA;
			border-radius: 4px;
		}
	)");

	auto *vl = new QVBoxLayout(overlay);
	vl->setContentsMargins(6, 6, 6, 6);
	vl->setSpacing(4);

	auto mk = [&](const QString &t, const QString& tooltip) {
		auto *b = new QToolButton(overlay);
		b->setText(t);
		b->setToolTip(tooltip);
		b->setAutoRaise(true);
		b->setFixedSize(28, 28);
		vl->addWidget(b);
		return b;
	};

	connect(mk("+", tr("Zoom in (Ctrl/Command + scroll)")), &QToolButton::clicked, this, &GraphicalVariablesView::zoomIn);
	connect(mk("-", tr("Zoom out")), &QToolButton::clicked, this, &GraphicalVariablesView::zoomOut);
	connect(mk("ƒ+", tr("Add expression display")), &QToolButton::clicked,
	        this, [this] { createDisplayExpression(); });
	m_autoLayoutButton = mk("⇆", tr("Automatic layout: on"));
	m_autoLayoutButton->setCheckable(true);
	m_autoLayoutButton->setChecked(true);
	m_autoLayoutButton->setAccessibleName(tr("Automatic layout"));
	connect(m_autoLayoutButton, &QToolButton::toggled,
	        this, &GraphicalVariablesView::setAutoLayoutEnabled);
	connect(mk("⤢", tr("Fit graph")), &QToolButton::clicked, this, &GraphicalVariablesView::fitGraph);
	connect(mk("⟳", tr("Reset zoom")), &QToolButton::clicked, this, &GraphicalVariablesView::resetZoom);

	overlay->move(10, 10);
	overlay->show();

}

GraphicalVariablesView::~GraphicalVariablesView() = default;

void GraphicalVariablesView::setSession(DebuggerSession* s)
{
	if (m_session == s)
		return;
	m_session = s;
	m_openPointerExprs.clear();
	m_displayDependencies.clear();
	m_dynamicRootByKey.clear();
	m_dynamicRoots.clear();
	refresh();
}

void GraphicalVariablesView::refresh()
{
	if (!m_session) return;
	if (m_refreshInProgress) {
		m_refreshPending = true;
		return;
	}
	++m_refreshGeneration;
	m_refreshInProgress = true;
	m_refreshPending = false;
	setUpdatesEnabled(false);
	const QPointF previousCenter = mapToScene(viewport()->rect().center());

	// Do not inspect the old scene here: DebuggerSession replaces its owned
	// DebugVariable objects before emitting variablesUpdated(), so those items
	// may already contain obsolete non-owning pointers.  Expansion state is
	// captured at the moment the user changes it in configureNodeItem().
	m_scene->clear();
	m_dynamicItems.clear();
	m_dynamicRootByKey.clear();
	m_dynamicRoots.clear();

	QHash<DebugVariable*, GraphicalNodeItem*> nodeMap;
	QHash<quintptr, GraphicalNodeItem*> addrMap;
	QHash<QString, DebugVariable*> varByPath;
	QHash<QString, GraphicalNodeItem*> rootItemByExpression;

	int y = 0;
	for (auto& v : m_session->variables()) {
		const QString layoutKey = layoutKeyForVariable(v.get());
		auto* item = new GraphicalNodeItem(v.get(), m_session, layoutKey);
		configureNodeItem(item);
		m_scene->addItem(item);
		item->setPos(positionForNode(layoutKey, QPointF(0, y)));
		nodeMap[v.get()] = item;
		rootItemByExpression.insert(v->fullPath(), item);
		indexVariablePaths(v.get(), varByPath);

		bool ok = false;
		        if (!v->address.isEmpty()) {
		            quintptr addr = v->address.toULongLong(&ok, 16);
		            if (ok)
		                addrMap[addr] = item;
		        }

		y += 150;
	}

	// Re-open pointer cards after a refresh (e.g. when stepping).
	for (const QString& expr : std::as_const(m_openPointerExprs)) {
		if (!varByPath.contains(expr))
			continue;
		DebugVariable* ptrVar = varByPath.value(expr);
		if (!ptrVar || !ptrVar->isPointer)
			continue;

		GraphicalNodeItem* fromItem = nodeMap.value(rootOf(ptrVar), nullptr);
		if (!fromItem)
			fromItem = nodeMap.value(ptrVar, nullptr);
		if (!fromItem)
			continue;

		ensurePointerNodeOpen(expr, ptrVar, fromItem, /*recenterView=*/false);
	}

	for (auto it = nodeMap.begin(); it != nodeMap.end(); ++it) {
		DebugVariable* root = it.key();
		if (!root->enabled)
			continue;
		for (auto& c : root->children) {
			if (!c->isPointer) continue;

			bool ok = false;
			quintptr target = c->pointeeAddress.toULongLong(&ok, 16);
			if (ok && target != 0 && addrMap.contains(target)) {
				auto* e = new GraphicalEdgeItem(it.value(), addrMap[target],
					layoutKeyForVariable(root), c->fullPath(), layoutKeyForVariable(rootOf(c.get())));
				m_scene->addItem(e);
				it.value()->addEdge(e);
				addrMap[target]->addEdge(e);
				e->updatePosition();
			}
		}
	}

	// Explicitly dependent displays retain their logical relationship even
	// when the value is not a pointer.  This mirrors DDD's "dependent on"
	// displays while leaving evaluation ownership in DebuggerSession.
	for (auto dependency = m_displayDependencies.cbegin();
	     dependency != m_displayDependencies.cend(); ++dependency) {
		const QString& destinationExpression = dependency.key();
		const QString& sourceExpression = dependency.value();
		DebugVariable* sourceVariable = varByPath.value(sourceExpression, nullptr);
		GraphicalNodeItem* sourceItem = sourceVariable
			? nodeMap.value(rootOf(sourceVariable), nullptr) : nullptr;
		GraphicalNodeItem* destinationItem =
			rootItemByExpression.value(destinationExpression, nullptr);
		if (!sourceItem || !destinationItem || sourceItem == destinationItem)
			continue;

		auto* edge = new GraphicalEdgeItem(
			sourceItem, destinationItem,
			layoutKeyForVariable(rootOf(sourceVariable)), sourceExpression,
			layoutKeyForVariable(destinationItem->node()));
		m_scene->addItem(edge);
		sourceItem->addEdge(edge);
		destinationItem->addEdge(edge);
		edge->updatePosition();
	}

	if (m_autoLayoutEnabled)
		applyAutomaticLayout(false);
	else
		m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-200, -200, 200, 200));

	m_refreshInProgress = false;
	centerOn(previousCenter);
	setUpdatesEnabled(true);
	viewport()->update();
	if (m_refreshPending)
		refresh();
}

void GraphicalVariablesView::rememberNodePosition(const QString& key, const QPointF& pos)
{
	if (!key.isEmpty())
		m_nodePositions.insert(key, pos);
}

QPointF GraphicalVariablesView::positionForNode(
	const QString& key,
	const QPointF& defaultPos) const
{
	if (key.isEmpty())
		return defaultPos;
	return m_nodePositions.value(key, defaultPos);
}

void GraphicalVariablesView::scheduleAutoLayout()
{
	// Several pointer cards can resolve in quick succession after a single
	// step (each is its own GDB round trip). Without coalescing, every one
	// of them would trigger its own immediate RuntimeGraphLayout::compute()
	// pass, and the graph visibly resettles once per card instead of once
	// per refresh -- the "flickers as if it keeps re-arranging" symptom.
	if (m_layoutScheduled)
		return;
	m_layoutScheduled = true;
	QTimer::singleShot(0, this, [this] {
		m_layoutScheduled = false;
		applyAutomaticLayout(false);
	});
}

void GraphicalVariablesView::configureNodeItem(GraphicalNodeItem* item)
{
	if (!item)
		return;
	item->restoreExpandedExpressions(
		m_nodeExpandedExpressions.value(item->layoutKey()));
	item->setPositionChangedCallback(
		[this](const QString& key, const QPointF& pos) {
			rememberNodePosition(key, pos);
		});
	item->setUserMovedCallback(
		[this](const QString& key, const QPointF& pos) {
			m_pinnedNodeKeys.insert(key);
			rememberNodePosition(key, pos);
		});
	item->setGeometryChangedCallback([this, item] {
		if (!item->layoutKey().isEmpty())
			m_nodeExpandedExpressions.insert(
				item->layoutKey(), item->expandedExpressions());
		if (m_autoLayoutEnabled && !m_refreshInProgress)
			scheduleAutoLayout();
	});
}

void GraphicalVariablesView::setAutoLayoutEnabled(bool enabled)
{
	m_autoLayoutEnabled = enabled;
	if (m_autoLayoutButton) {
		m_autoLayoutButton->setToolTip(enabled
			? tr("Automatic layout: on")
			: tr("Automatic layout: off"));
	}
	if (enabled)
		autoLayout();
}

void GraphicalVariablesView::applyAutomaticLayout(bool fitAfterLayout)
{
	QHash<QString, GraphicalNodeItem*> itemsByKey;
	QVector<RuntimeLayoutNode> nodes;
	QVector<RuntimeLayoutEdge> edges;

	for (QGraphicsItem* sceneItem : m_scene->items()) {
		if (auto* nodeItem = qgraphicsitem_cast<GraphicalNodeItem*>(sceneItem)) {
			const QString key = nodeItem->layoutKey();
			if (key.isEmpty() || itemsByKey.contains(key))
				continue;
			itemsByKey.insert(key, nodeItem);
			RuntimeLayoutNode node;
			node.id = key;
			node.size = nodeItem->boundingRect().size();
			node.hasPreviousPosition = m_nodePositions.contains(key);
			node.previousPosition = m_nodePositions.value(key);
			node.pinned = m_pinnedNodeKeys.contains(key);
			nodes.push_back(node);
		}
	}

	for (QGraphicsItem* sceneItem : m_scene->items()) {
		auto* edgeItem = qgraphicsitem_cast<GraphicalEdgeItem*>(sceneItem);
		if (!edgeItem || !edgeItem->sourceNode() || !edgeItem->destinationNode())
			continue;
		const QString source = edgeItem->sourceNode()->layoutKey();
		const QString destination = edgeItem->destinationNode()->layoutKey();
		if (source.isEmpty() || destination.isEmpty())
			continue;
		edges.push_back({QStringLiteral("%1->%2:%3")
		                    .arg(source, destination)
		                    .arg(edges.size()),
		                 source, destination});
	}

	const RuntimeLayoutResult layout = RuntimeGraphLayout::compute(nodes, edges);
	QStringList keys = itemsByKey.keys();
	std::sort(keys.begin(), keys.end());
	for (const QString& key : keys) {
		if (layout.positions.contains(key))
			itemsByKey.value(key)->setPos(layout.positions.value(key));
	}
	m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-200, -200, 200, 200));
	if (fitAfterLayout)
		fitGraph();
}

void GraphicalVariablesView::autoLayout()
{
	applyAutomaticLayout(false);
}

// ------------------------------------------------------------
// Zoom / View controls
// ------------------------------------------------------------

void GraphicalVariablesView::zoomIn()
{
	applyBoundedZoom(this, 1.2);
}

void GraphicalVariablesView::zoomOut()
{
	applyBoundedZoom(this, 1.0 / 1.2);
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
	// A two-finger secondary click on a touchpad can also produce a small
	// wheel event.  Zoom only for an explicit keyboard-modified gesture;
	// ordinary touchpad/wheel events keep their normal scrolling behaviour.
	const Qt::KeyboardModifiers modifiers = event->modifiers();
	const bool zoomGesture = modifiers.testFlag(Qt::ControlModifier)
		|| modifiers.testFlag(Qt::MetaModifier);
	if (!zoomGesture) {
		QGraphicsView::wheelEvent(event);
		return;
	}

	int delta = event->angleDelta().y();
	if (delta == 0)
		delta = event->pixelDelta().y();
	if (delta == 0) {
		event->ignore();
		return;
	}

	const ViewportAnchor oldAnchor = transformationAnchor();
	setTransformationAnchor(AnchorUnderMouse);
	applyBoundedZoom(this, delta > 0 ? 1.15 : 1.0 / 1.15);
	setTransformationAnchor(oldAnchor);
	event->accept();
}

void GraphicalVariablesView::mouseDoubleClickEvent(QMouseEvent* event)
{
	auto* item =
		qgraphicsitem_cast<GraphicalNodeItem*>(itemAt(event->pos()));

	if (item) {
		const QPointF scenePos = mapToScene(event->pos());
		const QPointF localPos = item->mapFromScene(scenePos);
		if (DebugVariable* v = item->variableAt(localPos)) {
			if (v->isPointer) {
				openPointerNode(v, item);
				event->accept();
				return;
			}
		}

		item->recalculateWidth();
		refresh();
	}

	QGraphicsView::mouseDoubleClickEvent(event);
}

void GraphicalVariablesView::contextMenuEvent(QContextMenuEvent* event)
{
	auto* item = qgraphicsitem_cast<GraphicalNodeItem*>(itemAt(event->pos()));
	if (!item) {
		QMenu menu(this);
		menu.setToolTipsVisible(true);
		QAction* create = menu.addAction(tr("Add expression display…"));
		create->setToolTip(tr("Create a new card for an expression or variable."));
		menu.addSeparator();
		QAction* arrange = menu.addAction(tr("Arrange all cards automatically"));
		arrange->setToolTip(tr("Recalculate positions and reduce crossed connections."));
		QAction* fit = menu.addAction(tr("Fit all cards in view"));
		QAction* selected = menu.exec(event->globalPos());
		if (selected == create)
			createDisplayExpression();
		else if (selected == arrange)
			autoLayout();
		else if (selected == fit)
			fitGraph();
		return;
	}

	const QPointF localPos = item->mapFromScene(mapToScene(event->pos()));
	DebugVariable* selectedVariable = item->variableAt(localPos);
	if (!selectedVariable)
		selectedVariable = item->node();
	DebugVariable* displayRoot = rootOf(selectedVariable);

	QMenu menu(this);
	menu.setToolTipsVisible(true);
	QString selectedPath = selectedVariable
		? selectedVariable->fullPath() : QString();
	if (selectedPath.isEmpty() && selectedVariable)
		selectedPath = selectedVariable->name;
	menu.addSection(tr("Selected: %1").arg(
		menu.fontMetrics().elidedText(selectedPath, Qt::ElideMiddle, 320)));
	auto* dependent = menu.addAction(tr("Add connected expression…"));
	dependent->setToolTip(tr("Create another display connected to this value."));
	auto* editValue = menu.addAction(tr("Change value…"));
	editValue->setToolTip(tr("Assign a new value through the debugger."));
	editValue->setEnabled(selectedVariable && selectedVariable->enabled &&
	                      !selectedVariable->hasChildren);
	auto* dereference = menu.addAction(tr("Show pointed value instead"));
	dereference->setToolTip(
		tr("Replace this display with the value referenced by the selected pointer."));
	dereference->setEnabled(selectedVariable && selectedVariable->isPointer &&
	                       displayRoot && displayRoot->isWatch && displayRoot->enabled);
	auto* formatMenu = menu.addMenu(tr("Display format"));
	formatMenu->setToolTipsVisible(true);
	const DebugValueFormat formats[] = {
		DebugValueFormat::Natural,
		DebugValueFormat::Hexadecimal,
		DebugValueFormat::Decimal,
		DebugValueFormat::Octal,
		DebugValueFormat::Binary,
		DebugValueFormat::Character
	};
	const DebugValueFormat currentFormat = selectedVariable && m_session
		? m_session->valueFormat(selectedVariable->fullPath())
		: DebugValueFormat::Natural;
	for (DebugValueFormat format : formats) {
		QAction* action = formatMenu->addAction(debugValueFormatLabel(format));
		action->setCheckable(true);
		action->setChecked(format == currentFormat);
		action->setData(static_cast<int>(format));
		action->setProperty("debugValueFormat", true);
	}
	formatMenu->setEnabled(selectedVariable != nullptr);

	QAction* toggleEnabled = nullptr;
	QAction* deleteDisplay = nullptr;
	if (displayRoot && displayRoot->isWatch) {
		menu.addSection(tr("Display"));
		toggleEnabled = menu.addAction(displayRoot->enabled
			? tr("Pause updates") : tr("Resume updates"));
		toggleEnabled->setToolTip(displayRoot->enabled
			? tr("Keep this card visible but stop evaluating its expression.")
			: tr("Evaluate this display again when execution stops."));
		deleteDisplay = menu.addAction(tr("Remove this display"));
	}

	menu.addSection(tr("Card contents"));
	auto* expandOne = menu.addAction(tr("Expand direct children"));
	auto* expandAll = menu.addAction(tr("Expand all descendants"));
	auto* collapseOne = menu.addAction(tr("Collapse direct children"));
	auto* collapseAll = menu.addAction(tr("Collapse all descendants"));
	menu.addSection(tr("Position"));
	auto* releasePosition = menu.addAction(tr("Use automatic position"));
	releasePosition->setToolTip(
		tr("Allow automatic layout to position this card again."));
	releasePosition->setEnabled(m_pinnedNodeKeys.contains(item->layoutKey()));
	QAction* selected = menu.exec(event->globalPos());
	if (selected && selected->property("debugValueFormat").toBool() && selectedVariable)
		m_session->setValueFormat(
			selectedVariable->fullPath(),
			static_cast<DebugValueFormat>(selected->data().toInt()));
	else if (selected == dependent && selectedVariable)
		createDisplayExpression(selectedVariable->fullPath());
	else if (selected == editValue)
		editVariableValue(selectedVariable);
	else if (selected == dereference)
		replaceDisplayWithDereference(selectedVariable);
	else if (selected == toggleEnabled && displayRoot)
		m_session->setWatchExpressionEnabled(displayRoot->name, !displayRoot->enabled);
	else if (selected == deleteDisplay && displayRoot)
		deleteDisplayExpression(displayRoot->name);
	else if (selected == expandOne) item->setExpandedRecursively(true, 1);
	else if (selected == expandAll) item->setExpandedRecursively(true, 8);
	else if (selected == collapseOne) item->setExpandedRecursively(false, 1);
	else if (selected == collapseAll) item->setExpandedRecursively(false, 8);
	else if (selected == releasePosition) {
		m_pinnedNodeKeys.remove(item->layoutKey());
		autoLayout();
	}
}

void GraphicalVariablesView::createDisplayExpression(const QString& dependsOn)
{
	if (!m_session)
		return;
	bool accepted = false;
	const QString expression = QInputDialog::getText(
		this,
		dependsOn.isEmpty() ? tr("Add expression display")
		                    : tr("Add connected expression"),
		tr("Expression to display:"), QLineEdit::Normal, {}, &accepted).trimmed();
	if (!accepted || expression.isEmpty())
		return;
	const bool alreadyExists = m_session->watchExpressions().contains(expression);
	if (!dependsOn.isEmpty())
		m_displayDependencies.insert(expression, dependsOn);
	m_session->addWatchExpression(expression);
	if (alreadyExists)
		refresh();
}

void GraphicalVariablesView::deleteDisplayExpression(const QString& expression)
{
	if (!m_session || expression.isEmpty())
		return;
	m_displayDependencies.remove(expression);
	for (auto it = m_displayDependencies.begin(); it != m_displayDependencies.end();) {
		if (it.value() == expression)
			it = m_displayDependencies.erase(it);
		else
			++it;
	}
	m_openPointerExprs.remove(expression);
	m_session->removeWatchExpression(expression);
}

void GraphicalVariablesView::replaceDisplayWithDereference(DebugVariable* variable)
{
	if (!m_session || !variable || !variable->isPointer)
		return;
	DebugVariable* displayRoot = rootOf(variable);
	if (!displayRoot || !displayRoot->isWatch)
		return;

	const QString oldExpression = displayRoot->name;
	const QString newExpression = QStringLiteral("*(%1)").arg(variable->fullPath());
	if (m_displayDependencies.contains(oldExpression)) {
		const QString dependency = m_displayDependencies.take(oldExpression);
		m_displayDependencies.insert(newExpression, dependency);
	}
	for (auto it = m_displayDependencies.begin(); it != m_displayDependencies.end(); ++it)
		if (it.value() == oldExpression)
			it.value() = newExpression;
	m_openPointerExprs.remove(oldExpression);
	m_session->replaceWatchExpression(oldExpression, newExpression);
}

void GraphicalVariablesView::editVariableValue(DebugVariable* variable)
{
	if (!m_session || !variable || !variable->enabled || variable->hasChildren)
		return;
	bool accepted = false;
	const QString value = QInputDialog::getText(
		this, tr("Edit value"),
		tr("New value for %1:").arg(variable->fullPath()),
		QLineEdit::Normal, variable->value, &accepted);
	if (accepted)
		m_session->setVariable(variable->fullPath(), value);
}

void GraphicalVariablesView::openPointerNode(DebugVariable* ptrVar, GraphicalNodeItem* fromItem)
{
	if (!m_session || !ptrVar || !fromItem)
		return;

	const QString expr = ptrVar->fullPath();
	if (expr.isEmpty())
		return;

	m_openPointerExprs.insert(expr);
	ensurePointerNodeOpen(expr, ptrVar, fromItem, /*recenterView=*/true);
}

void GraphicalVariablesView::reopenDependentPointerExpressions(
	DebugVariable* target,
	GraphicalNodeItem* targetItem)
{
	if (!target || !targetItem)
		return;

	// A pointer expression can only be resolved once the card of the value it
	// dereferences from actually exists. Session-rooted pointers are reopened
	// directly in refresh(); a pointer field living *inside* an already
	// dereferenced pointer's card (e.g. walking head->next->next) is not part
	// of DebuggerSession::variables() at all, so it can only be found here,
	// once its own parent card has just been created below it.
	//
	// Snapshot first: ensurePointerNodeOpen() below may itself append to
	// m_openPointerExprs synchronously (double-click) or trigger further
	// dereferences whose completion re-enters this function.
	const QSet<QString> pending = m_openPointerExprs;
	for (const QString& expr : pending) {
		DebugVariable* field = findVariableByPath(target, expr);
		if (!field || field == target || !field->isPointer)
			continue;
		// Automatic reopen after a step: keep the viewport where the user
		// left it instead of jumping to whichever nested pointer happened
		// to resolve first.
		ensurePointerNodeOpen(expr, field, targetItem, /*recenterView=*/false);
	}
}

void GraphicalVariablesView::ensurePointerNodeOpen(
	const QString& pointerExpr,
	DebugVariable* ptrVar,
	GraphicalNodeItem* fromItem,
	bool recenterView)
{
	if (!m_session || pointerExpr.isEmpty() || !ptrVar || !fromItem)
		return;

	// Reuse an already displayed card when the pointer resolves to the same
	// runtime address. The expression used to open the pointer may have a
	// different name (for example *&work), but the address identifies the
	// same runtime object as the existing work card.
	if (!ptrVar->pointeeAddress.isEmpty()) {
		bool targetOk = false;
		const quintptr targetAddress =
			ptrVar->pointeeAddress.toULongLong(&targetOk, 16);

		GraphicalNodeItem* existingTarget = nullptr;
		for (QGraphicsItem* sceneItem : m_scene->items()) {
			auto* nodeItem = qgraphicsitem_cast<GraphicalNodeItem*>(sceneItem);
			if (!nodeItem || nodeItem == fromItem || !nodeItem->node())
				continue;

			const QString candidateAddress = nodeItem->node()->address;
			if (candidateAddress.isEmpty())
				continue;

			bool candidateOk = false;
			const quintptr candidate =
				candidateAddress.toULongLong(&candidateOk, 16);

			const bool sameAddress =
				(targetOk && candidateOk && candidate == targetAddress)
				|| (!targetOk
					&& candidateAddress.compare(
						ptrVar->pointeeAddress,
						Qt::CaseInsensitive) == 0);

			if (sameAddress) {
				existingTarget = nodeItem;
				break;
			}
		}

		if (existingTarget) {
			bool edgeExists = false;
			for (QGraphicsItem* sceneItem : m_scene->items()) {
				auto* edge = qgraphicsitem_cast<GraphicalEdgeItem*>(sceneItem);
				if (edge && edge->sourceNode() == fromItem
				    && edge->destinationNode() == existingTarget
				    && edge->sourceExpression() == pointerExpr) {
					edgeExists = true;
					break;
				}
			}

			if (!edgeExists) {
				auto* edge = new GraphicalEdgeItem(
					fromItem,
					existingTarget,
					layoutKeyForVariable(rootOf(ptrVar)),
					pointerExpr,
					layoutKeyForVariable(existingTarget->node()));
				m_scene->addItem(edge);
				fromItem->addEdge(edge);
				existingTarget->addEdge(edge);
				edge->updatePosition();
			}

			scheduleAutoLayout();
			if (recenterView)
				centerOn(existingTarget);
			reopenDependentPointerExpressions(existingTarget->node(), existingTarget);
			return;
		}
	}

	QString key = RuntimeObjectGraph::identityFor(
		ptrVar->pointeeAddress, ptrVar->type, pointerExpr);
	if (!ptrVar->pointeeAddress.isEmpty()) {
		for (auto existing = m_dynamicRootByKey.cbegin();
		     existing != m_dynamicRootByKey.cend(); ++existing) {
			if (existing.value()
			    && existing.value()->address.compare(
				   ptrVar->pointeeAddress, Qt::CaseInsensitive) == 0) {
				key = existing.key();
				break;
			}
		}
	}
	if (m_dynamicRootByKey.contains(key)) {
		DebugVariable* rootRaw = m_dynamicRootByKey.value(key);
		if (!rootRaw)
			return;

		const QString layoutKey = QStringLiteral("dynamic:%1").arg(key);
		auto* existing = m_dynamicItems.value(rootRaw, nullptr);
		if (!existing) {
			existing = new GraphicalNodeItem(rootRaw, m_session, layoutKey);
			configureNodeItem(existing);
			m_scene->addItem(existing);
			m_dynamicItems[rootRaw] = existing;
		}

		bool edgeExists = false;
		for (QGraphicsItem* sceneItem : m_scene->items()) {
			auto* edge = qgraphicsitem_cast<GraphicalEdgeItem*>(sceneItem);
			if (edge && edge->sourceNode() == fromItem
			    && edge->destinationNode() == existing
			    && edge->sourceExpression() == pointerExpr) {
				edgeExists = true;
				break;
			}
		}
		if (!edgeExists) {
			auto* e = new GraphicalEdgeItem(fromItem, existing,
				layoutKeyForVariable(rootOf(ptrVar)), pointerExpr, key);
			m_scene->addItem(e);
			fromItem->addEdge(e);
			existing->addEdge(e);
			e->updatePosition();
		}

		existing->setPos(positionForNode(layoutKey, fromItem->pos() + QPointF(340, 0)));
		scheduleAutoLayout();
		if (recenterView)
			centerOn(existing);
		reopenDependentPointerExpressions(rootRaw, existing);
		return;
	}

	const quint64 generation = m_refreshGeneration;
	const QString pointerName = ptrVar->name;
	const QString sourceLayoutKey = fromItem->layoutKey();
	m_session->dereferencePointer(pointerExpr,
		[this, key, pointerExpr, pointerName, sourceLayoutKey, generation, recenterView]
		(const QString& value, const QString& type) {
			if (generation != m_refreshGeneration) return;
			DebugVariable* ptrVar = nullptr;
			GraphicalNodeItem* fromItem = nullptr;
			for (auto& candidate : m_session->variables()) {
				if (DebugVariable* found = findVariableByPath(candidate.get(), pointerExpr)) {
					ptrVar = found;
					break;
				}
			}
			if (!ptrVar) {
				for (auto& candidate : m_dynamicRoots) {
					if (DebugVariable* found =
						findVariableByPath(candidate.get(), pointerExpr)) {
						ptrVar = found;
						break;
					}
				}
			}
			for (QGraphicsItem* sceneItem : m_scene->items()) {
				auto* nodeItem = qgraphicsitem_cast<GraphicalNodeItem*>(sceneItem);
				if (nodeItem && nodeItem->layoutKey() == sourceLayoutKey) {
					fromItem = nodeItem;
					break;
				}
			}
			if (!ptrVar || !fromItem) return;
			if (value.isEmpty())
				return;

			auto root = std::make_unique<DebugVariable>();
			root->name = QString("*%1").arg(pointerName);
			root->expression = QStringLiteral("*(%1)").arg(pointerExpr);
			root->value = value.trimmed();
			root->type = type;
			root->address = ptrVar->pointeeAddress;
			root->parent = nullptr;
			root->isPointer = false;
			root->hasChildren = isInlineStruct(root->value);

			if (root->hasChildren)
				expandInlineStructIntoChildrenLocal(root.get(), root->value, 0, 3);

			DebugVariable* rootRaw = root.get();
			m_dynamicRoots.push_back(std::move(root));
			m_dynamicRootByKey.insert(key, rootRaw);

			const QString layoutKey = QStringLiteral("dynamic:%1").arg(key);
			auto* item = new GraphicalNodeItem(rootRaw, m_session, layoutKey);
			configureNodeItem(item);
			m_scene->addItem(item);
			item->setPos(positionForNode(layoutKey, fromItem->pos() + QPointF(340, 0)));
			m_dynamicItems[rootRaw] = item;

			auto* e = new GraphicalEdgeItem(fromItem, item,
				layoutKeyForVariable(rootOf(ptrVar)), pointerExpr, key);
			m_scene->addItem(e);
			fromItem->addEdge(e);
			item->addEdge(e);
			e->updatePosition();

			scheduleAutoLayout();
			if (recenterView)
				centerOn(item);
			reopenDependentPointerExpressions(rootRaw, item);
		});
}
