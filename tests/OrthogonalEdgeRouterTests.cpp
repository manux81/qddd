#include "OrthogonalEdgeRouter.h"

#include <QCoreApplication>
#include <QLineF>
#include <QPolygonF>
#include <cassert>
#include <cmath>

static bool pathHitsRect(const QPainterPath& path, const QRectF& rect)
{
	for (const QPolygonF& polygon : path.toSubpathPolygons()) {
		for (int i = 1; i < polygon.size(); ++i) {
			const QLineF line(polygon[i - 1], polygon[i]);
			if (rect.contains(line.p1()) || rect.contains(line.p2()))
				return true;
			const QLineF sides[] = {
				QLineF(rect.topLeft(), rect.topRight()),
				QLineF(rect.topRight(), rect.bottomRight()),
				QLineF(rect.bottomRight(), rect.bottomLeft()),
				QLineF(rect.bottomLeft(), rect.topLeft())
			};
			QPointF p;
			for (const QLineF& side : sides)
				if (line.intersects(side, &p) == QLineF::BoundedIntersection)
					return true;
		}
	}
	return false;
}

static bool pathHasSelfConflict(const QPainterPath& path)
{
	QVector<QLineF> lines;
	for (const QPolygonF& polygon : path.toSubpathPolygons())
		for (int i = 1; i < polygon.size(); ++i)
			lines.push_back(QLineF(polygon[i - 1], polygon[i]));

	auto overlap = [](const QLineF& a, const QLineF& b) {
		const qreal epsilon = 0.1;
		const bool aHorizontal = std::abs(a.y1() - a.y2()) < epsilon;
		const bool bHorizontal = std::abs(b.y1() - b.y2()) < epsilon;
		if (aHorizontal && bHorizontal
		    && std::abs(a.y1() - b.y1()) < epsilon) {
			const qreal lo = std::max(std::min(a.x1(), a.x2()),
			                          std::min(b.x1(), b.x2()));
			const qreal hi = std::min(std::max(a.x1(), a.x2()),
			                          std::max(b.x1(), b.x2()));
			return hi - lo > 1.0;
		}

		const bool aVertical = std::abs(a.x1() - a.x2()) < epsilon;
		const bool bVertical = std::abs(b.x1() - b.x2()) < epsilon;
		if (aVertical && bVertical
		    && std::abs(a.x1() - b.x1()) < epsilon) {
			const qreal lo = std::max(std::min(a.y1(), a.y2()),
			                          std::min(b.y1(), b.y2()));
			const qreal hi = std::min(std::max(a.y1(), a.y2()),
			                          std::max(b.y1(), b.y2()));
			return hi - lo > 1.0;
		}
		return false;
	};

	for (int i = 0; i < lines.size(); ++i) {
		for (int j = i + 1; j < lines.size(); ++j) {
			if (overlap(lines[i], lines[j]))
				return true;
			if (j == i + 1)
				continue;

			QPointF crossing;
			if (lines[i].intersects(lines[j], &crossing)
			    == QLineF::BoundedIntersection)
				return true;
		}
	}
	return false;
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	OrthogonalEdgeRouter::Request direct;
	direct.source = QPointF(0, 0);
	direct.target = QPointF(300, 120);
	auto directResult = OrthogonalEdgeRouter::route(direct);
	assert(!directResult.path.isEmpty());

	OrthogonalEdgeRouter::Request blocked = direct;
	blocked.obstacles = {QRectF(120, 20, 80, 100)};
	auto blockedResult = OrthogonalEdgeRouter::route(blocked);
	assert(!blockedResult.path.isEmpty());
	assert(!pathHitsRect(blockedResult.path, blocked.obstacles.front().adjusted(-7, -7, 7, 7)));

	OrthogonalEdgeRouter::Request inverse;
	inverse.source = QPointF(300, 120);
	inverse.target = QPointF(0, 0);
	inverse.stabilityKey = QStringLiteral("inverse");
	auto inverseResult = OrthogonalEdgeRouter::route(inverse);
	assert(!inverseResult.path.isEmpty());

	OrthogonalEdgeRouter::Request leftEntry;
	leftEntry.source = QPointF(0, 80);
	leftEntry.target = QPointF(300, 120);
	leftEntry.sourceNormal = QPointF(1, 0);
	leftEntry.targetNormal = QPointF(-1, 0);
	auto leftEntryResult = OrthogonalEdgeRouter::route(leftEntry);
	assert(!leftEntryResult.path.isEmpty());
	const QPointF leftBefore = leftEntryResult.path.pointAtPercent(0.995);
	const QPointF leftTip = leftEntryResult.path.pointAtPercent(1.0);
	assert(std::abs(leftBefore.y() - leftTip.y()) < 0.5);

	OrthogonalEdgeRouter::Request topEntry;
	topEntry.source = QPointF(80, 300);
	topEntry.target = QPointF(140, 0);
	topEntry.sourceNormal = QPointF(1, 0);
	topEntry.targetNormal = QPointF(0, -1);
	auto topEntryResult = OrthogonalEdgeRouter::route(topEntry);
	assert(!topEntryResult.path.isEmpty());
	const QPointF topBefore = topEntryResult.path.pointAtPercent(0.995);
	const QPointF topTip = topEntryResult.path.pointAtPercent(1.0);
	assert(std::abs(topBefore.x() - topTip.x()) < 0.5);

	// Regression: both endpoints face right. The old simplifier could remove
	// the turning point and produce an edge that travelled right and then back
	// over the same horizontal segment.
	OrthogonalEdgeRouter::Request backtracking;
	backtracking.source = QPointF(0, 180);
	backtracking.target = QPointF(70, 180);
	backtracking.sourceNormal = QPointF(1, 0);
	backtracking.targetNormal = QPointF(1, 0);
	backtracking.stabilityKey = QStringLiteral("backtracking");
	auto backtrackingResult = OrthogonalEdgeRouter::route(backtracking);
	assert(!backtrackingResult.path.isEmpty());
	assert(!pathHasSelfConflict(backtrackingResult.path));

	OrthogonalEdgeRouter::Request outerBackEdge;
	outerBackEdge.source = QPointF(420, 150);
	outerBackEdge.target = QPointF(120, 170);
	outerBackEdge.sourceNormal = QPointF(1, 0);
	outerBackEdge.targetNormal = QPointF(-1, 0);
	outerBackEdge.routingBounds = QRectF(80, 80, 400, 220);
	outerBackEdge.stabilityKey = QStringLiteral("parent-back-edge");
	auto outerBackResult = OrthogonalEdgeRouter::route(outerBackEdge);
	assert(!outerBackResult.path.isEmpty());
	assert(!pathHasSelfConflict(outerBackResult.path));

	bool leavesGraphInterior = false;
	for (const QPolygonF& polygon : outerBackResult.path.toSubpathPolygons()) {
		for (const QPointF& point : polygon) {
			if (point.y() < outerBackEdge.routingBounds.top()
			    || point.y() > outerBackEdge.routingBounds.bottom()) {
				leavesGraphInterior = true;
				break;
			}
		}
	}
	assert(leavesGraphInterior);

	OrthogonalEdgeRouter::Request loop;
	loop.source = QPointF(200, 50);
	loop.stabilityKey = QStringLiteral("self");
	auto loopResult = OrthogonalEdgeRouter::routeSelfLoop(loop, QRectF(0, 0, 200, 120));
	assert(!loopResult.path.isEmpty());

	return 0;
}
