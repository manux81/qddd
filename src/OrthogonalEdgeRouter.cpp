#include "OrthogonalEdgeRouter.h"

#include <QLineF>
#include <QPolygonF>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qreal kObstacleMargin = 8.0;
constexpr qreal kLaneSpacing = 6.0;
constexpr qreal kBendPenalty = 45.0;
constexpr qreal kCrossingPenalty = 260.0;
constexpr qreal kOverlapPenalty = 180.0;
constexpr qreal kHugePenalty = 1000000.0;
constexpr qreal kCornerRadius = 10.0;

struct Candidate {
	QVector<QPointF> points;
	qreal cost = std::numeric_limits<qreal>::max();
};

bool fuzzyEqual(qreal a, qreal b)
{
	return std::abs(a - b) < 0.01;
}

bool pointEqual(const QPointF& a, const QPointF& b)
{
	return QLineF(a, b).length() < 0.01;
}

bool between(qreal value, qreal a, qreal b)
{
	return value >= std::min(a, b) - 0.01
		&& value <= std::max(a, b) + 0.01;
}

QVector<QPointF> simplify(QVector<QPointF> points)
{
	for (int i = points.size() - 2; i > 0; --i) {
		const QPointF& a = points[i - 1];
		const QPointF& b = points[i];
		const QPointF& c = points[i + 1];

		const bool horizontal =
			fuzzyEqual(a.y(), b.y())
			&& fuzzyEqual(b.y(), c.y())
			&& between(b.x(), a.x(), c.x());

		const bool vertical =
			fuzzyEqual(a.x(), b.x())
			&& fuzzyEqual(b.x(), c.x())
			&& between(b.y(), a.y(), c.y());

		// Remove a collinear point only when it lies between its neighbours.
		// Removing a turning-back point would turn the route into a segment
		// that travels out and then overlaps itself on the way back.
		if (pointEqual(a, b) || pointEqual(b, c) || horizontal || vertical)
			points.removeAt(i);
	}
	return points;
}

QVector<QLineF> segments(const QVector<QPointF>& points)
{
	QVector<QLineF> out;
	for (int i = 1; i < points.size(); ++i)
		out.push_back(QLineF(points[i - 1], points[i]));
	return out;
}

bool segmentHitsRect(const QLineF& line, const QRectF& rect)
{
	if (rect.contains(line.p1()) || rect.contains(line.p2()))
		return true;
	const QLineF sides[] = {
		QLineF(rect.topLeft(), rect.topRight()),
		QLineF(rect.topRight(), rect.bottomRight()),
		QLineF(rect.bottomRight(), rect.bottomLeft()),
		QLineF(rect.bottomLeft(), rect.topLeft())
	};
	QPointF intersection;
	for (const QLineF& side : sides)
		if (line.intersects(side, &intersection) == QLineF::BoundedIntersection)
			return true;
	return false;
}

bool orthogonal(const QLineF& line)
{
	return fuzzyEqual(line.x1(), line.x2()) || fuzzyEqual(line.y1(), line.y2());
}

qreal segmentLength(const QVector<QPointF>& points)
{
	qreal total = 0.0;
	for (const QLineF& line : segments(points))
		total += line.length();
	return total;
}

QVector<QLineF> pathSegments(const QPainterPath& path)
{
	QVector<QLineF> out;
	const QList<QPolygonF> polygons = path.toSubpathPolygons();
	for (const QPolygonF& polygon : polygons)
		for (int i = 1; i < polygon.size(); ++i)
			out.push_back(QLineF(polygon[i - 1], polygon[i]));
	return out;
}

bool overlapsAxisAligned(const QLineF& a, const QLineF& b)
{
	if (fuzzyEqual(a.y1(), a.y2()) && fuzzyEqual(b.y1(), b.y2())
	    && fuzzyEqual(a.y1(), b.y1())) {
		const qreal a0 = std::min(a.x1(), a.x2());
		const qreal a1 = std::max(a.x1(), a.x2());
		const qreal b0 = std::min(b.x1(), b.x2());
		const qreal b1 = std::max(b.x1(), b.x2());
		return std::min(a1, b1) - std::max(a0, b0) > 3.0;
	}
	if (fuzzyEqual(a.x1(), a.x2()) && fuzzyEqual(b.x1(), b.x2())
	    && fuzzyEqual(a.x1(), b.x1())) {
		const qreal a0 = std::min(a.y1(), a.y2());
		const qreal a1 = std::max(a.y1(), a.y2());
		const qreal b0 = std::min(b.y1(), b.y2());
		const qreal b1 = std::max(b.y1(), b.y2());
		return std::min(a1, b1) - std::max(a0, b0) > 3.0;
	}
	return false;
}

bool hasSelfConflict(const QVector<QPointF>& points)
{
	const QVector<QLineF> routeSegments = segments(points);

	for (int i = 0; i < routeSegments.size(); ++i) {
		for (int j = i + 1; j < routeSegments.size(); ++j) {
			const QLineF& a = routeSegments[i];
			const QLineF& b = routeSegments[j];

			// Overlap is invalid even for adjacent segments: it means the route
			// leaves a point and then travels back over the same geometry.
			if (overlapsAxisAligned(a, b))
				return true;

			// Adjacent segments are expected to meet at their common endpoint.
			if (j == i + 1)
				continue;

			QPointF crossing;
			if (a.intersects(b, &crossing) == QLineF::BoundedIntersection)
				return true;
		}
	}

	return false;
}

qreal score(const QVector<QPointF>& points,
            const QVector<QRectF>& obstacles,
            const QVector<QPainterPath>& existingEdges)
{
	const QVector<QLineF> routeSegments = segments(points);
	qreal cost = segmentLength(points);
	cost += qMax(0, points.size() - 2) * kBendPenalty;

	for (const QLineF& segment : routeSegments) {
		if (!orthogonal(segment))
			cost += kHugePenalty;

		for (const QRectF& obstacle : obstacles) {
			const QRectF expanded = obstacle.adjusted(
				-kObstacleMargin, -kObstacleMargin,
				 kObstacleMargin,  kObstacleMargin);
			if (segmentHitsRect(segment, expanded))
				cost += kHugePenalty;
		}

		for (const QPainterPath& path : existingEdges) {
			for (const QLineF& other : pathSegments(path)) {
				if (overlapsAxisAligned(segment, other)) {
					cost += kOverlapPenalty;
					continue;
				}
				QPointF crossing;
				if (segment.intersects(other, &crossing) == QLineF::BoundedIntersection
				    && !pointEqual(crossing, segment.p1())
				    && !pointEqual(crossing, segment.p2()))
					cost += kCrossingPenalty;
			}
		}
	}
	return cost;
}

void addCandidate(QVector<Candidate>& candidates, QVector<QPointF> points,
                  const OrthogonalEdgeRouter::Request& request)
{
	points = simplify(std::move(points));
	if (points.size() < 2 || hasSelfConflict(points))
		return;

	Candidate candidate;
	candidate.points = std::move(points);
	candidate.cost = score(candidate.points, request.obstacles, request.existingEdges);
	candidates.push_back(std::move(candidate));
}

QPainterPath roundedPath(QVector<QPointF> points, qreal sourceGap)
{
	points = simplify(std::move(points));
	QPainterPath path;
	if (points.size() < 2)
		return path;

	QPointF start = points.first();
	QLineF first(start, points[1]);
	if (first.length() > sourceGap) {
		first.setLength(sourceGap);
		start = first.p2();
	}
	path.moveTo(start);

	for (int i = 1; i + 1 < points.size(); ++i) {
		const QPointF previous = points[i - 1];
		const QPointF corner = points[i];
		const QPointF next = points[i + 1];
		QLineF incoming(corner, previous);
		QLineF outgoing(corner, next);
		const qreal radius = qMin(kCornerRadius,
			qMin(incoming.length() * 0.25, outgoing.length() * 0.25));
		if (incoming.length() > 0.001) incoming.setLength(radius);
		if (outgoing.length() > 0.001) outgoing.setLength(radius);
		path.lineTo(incoming.p2());
		path.quadTo(corner, outgoing.p2());
	}
	path.lineTo(points.last());
	return path;
}

QPointF labelPosition(const QVector<QPointF>& points)
{
	qreal bestLength = -1.0;
	QPointF best;
	for (const QLineF& line : segments(points)) {
		if (!fuzzyEqual(line.y1(), line.y2()))
			continue;
		if (line.length() > bestLength) {
			bestLength = line.length();
			best = (line.p1() + line.p2()) * 0.5;
		}
	}
	if (bestLength >= 0.0)
		return best;
	return points.size() >= 2 ? (points[0] + points[1]) * 0.5 : QPointF();
}
}

OrthogonalEdgeRouter::Result OrthogonalEdgeRouter::route(const Request& request)
{
	Result result;
	result.endPoint = request.target;

	const qreal lane =
		(static_cast<int>(qHash(request.stabilityKey) % 5) - 2) * kLaneSpacing
		+ request.laneOffset * 0.20;
	const qreal stubLength = 24.0 + std::abs(lane) * 0.35;

	const QPointF sourceLead =
		request.source + request.sourceNormal * stubLength;
	const QPointF targetLead =
		request.target + request.targetNormal * stubLength;

	QRectF graphBounds = request.routingBounds;
	if (!graphBounds.isValid() || graphBounds.isEmpty()) {
		graphBounds = QRectF(request.source, request.target).normalized();
		for (const QRectF& rect : request.obstacles)
			graphBounds = graphBounds.united(rect);
	}

	qreal minX = std::min(sourceLead.x(), targetLead.x());
	qreal maxX = std::max(sourceLead.x(), targetLead.x());
	qreal minY = std::min(sourceLead.y(), targetLead.y());
	qreal maxY = std::max(sourceLead.y(), targetLead.y());

	for (const QRectF& rect : request.obstacles) {
		minX = std::min(minX, rect.left());
		maxX = std::max(maxX, rect.right());
		minY = std::min(minY, rect.top());
		maxY = std::max(maxY, rect.bottom());
	}

	minX = std::min(minX, graphBounds.left());
	maxX = std::max(maxX, graphBounds.right());
	minY = std::min(minY, graphBounds.top());
	maxY = std::max(maxY, graphBounds.bottom());

	const qreal midX = (sourceLead.x() + targetLead.x()) * 0.5 + lane;
	const qreal midY = (sourceLead.y() + targetLead.y()) * 0.5 + lane;
	const qreal outer = 30.0 + std::abs(lane);

	auto withStubs = [&](std::initializer_list<QPointF> middle) {
		QVector<QPointF> points;
		points << request.source << sourceLead;
		for (const QPointF& point : middle)
			points << point;
		points << targetLead << request.target;
		return points;
	};

	QVector<Candidate> candidates;

	// The graph is laid out primarily from left to right. A target clearly to
	// the left of the source is therefore a back edge (for example child.parent
	// -> parent). Keep those connections out of the interior of the graph:
	// they must use a corridor above or below the complete card group.
	constexpr qreal BackEdgeTolerance = 18.0;
	const bool backEdge =
		request.target.x() < request.source.x() - BackEdgeTolerance;

	if (backEdge) {
		const qreal topLane =
			graphBounds.top() - outer - std::abs(lane);
		const qreal bottomLane =
			graphBounds.bottom() + outer + std::abs(lane);

		addCandidate(
			candidates,
			withStubs({
				QPointF(sourceLead.x(), topLane),
				QPointF(targetLead.x(), topLane)
			}),
			request);

		addCandidate(
			candidates,
			withStubs({
				QPointF(sourceLead.x(), bottomLane),
				QPointF(targetLead.x(), bottomLane)
			}),
			request);
	} else {
		// Normal forward edges may use compact internal routes.
		addCandidate(candidates,
			withStubs({
				QPointF(midX, sourceLead.y()),
				QPointF(midX, targetLead.y())
			}), request);

		addCandidate(candidates,
			withStubs({
				QPointF(sourceLead.x(), midY),
				QPointF(targetLead.x(), midY)
			}), request);

		addCandidate(candidates,
			withStubs({
				QPointF(sourceLead.x(), minY - outer),
				QPointF(targetLead.x(), minY - outer)
			}), request);

		addCandidate(candidates,
			withStubs({
				QPointF(sourceLead.x(), maxY + outer),
				QPointF(targetLead.x(), maxY + outer)
			}), request);

		addCandidate(candidates,
			withStubs({
				QPointF(minX - outer, sourceLead.y()),
				QPointF(minX - outer, targetLead.y())
			}), request);

		addCandidate(candidates,
			withStubs({
				QPointF(maxX + outer, sourceLead.y()),
				QPointF(maxX + outer, targetLead.y())
			}), request);
	}

	if (candidates.isEmpty()) {
		// Preserve the same policy in fallback: a back edge still leaves the
		// graph before returning to its target.
		const qreal fallbackY = backEdge
			? graphBounds.top() - outer - 24.0
			: minY - outer - 24.0;

		QVector<QPointF> fallback = {
			request.source,
			sourceLead,
			QPointF(sourceLead.x(), fallbackY),
			QPointF(targetLead.x(), fallbackY),
			targetLead,
			request.target
		};
		fallback = simplify(std::move(fallback));

		result.path = roundedPath(fallback, request.sourceGap);
		result.labelPosition = labelPosition(fallback);
		return result;
	}

	std::stable_sort(
		candidates.begin(), candidates.end(),
		[](const Candidate& a, const Candidate& b) {
			return a.cost < b.cost;
		});

	const Candidate& best = candidates.first();
	result.path = roundedPath(best.points, request.sourceGap);
	result.labelPosition = labelPosition(best.points);
	return result;
}

OrthogonalEdgeRouter::Result OrthogonalEdgeRouter::routeSelfLoop(
	const Request& request, const QRectF& cardRect)
{
	Result result;
	const qreal lane = 26.0 + static_cast<int>(qHash(request.stabilityKey) % 4) * kLaneSpacing;
	const QPointF end(cardRect.center().x(), cardRect.top());
	const qreal right = cardRect.right() + lane;
	const qreal top = cardRect.top() - lane;
	QVector<QPointF> points = {
		request.source,
		QPointF(right, request.source.y()),
		QPointF(right, top),
		QPointF(end.x(), top),
		end
	};
	result.path = roundedPath(points, request.sourceGap);
	result.labelPosition = labelPosition(points);
	result.endPoint = end;
	return result;
}
