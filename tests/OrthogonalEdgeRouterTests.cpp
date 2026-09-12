#include "OrthogonalEdgeRouter.h"

#include <QCoreApplication>
#include <QLineF>
#include <QPolygonF>
#include <algorithm>
// These regression checks must also execute in Release/RelWithDebInfo builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
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

	// Alternating tall cards require several bends through the gaps. A finite
	// menu of one-dogleg candidates either crosses a card or takes a huge tour
	// around all three; the nearby free corridor is less than 1100 pixels long.
	OrthogonalEdgeRouter::Request staggered;
	staggered.source = QPointF(0, 100);
	staggered.target = QPointF(520, 100);
	staggered.sourceNormal = QPointF(1, 0);
	staggered.targetNormal = QPointF(-1, 0);
	staggered.obstacles = {
		QRectF(70, -1000, 80, 1140),
		QRectF(230, 60, 80, 1040),
		QRectF(390, -1000, 60, 1140)
	};
	for (int lane = -2; lane <= 2; ++lane) {
		staggered.laneOffset = lane * 12.0;
		staggered.stabilityKey = QStringLiteral("staggered-%1").arg(lane);
		const auto routed = OrthogonalEdgeRouter::route(staggered);
		assert(!routed.path.isEmpty());
		assert(QLineF(routed.path.pointAtPercent(0), staggered.source).length() < 0.01);
		assert(QLineF(routed.path.pointAtPercent(1), staggered.target).length() < 0.01);
		assert(!pathHasSelfConflict(routed.path));
		for (const QRectF& obstacle : staggered.obstacles)
			assert(!pathHitsRect(routed.path, obstacle.adjusted(-1, -1, 1, 1)));
		assert(routed.path.length() < 1100.0);
	}

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

	// A distant unrelated card can enlarge graph bounds. It must not force a
	// nearby backward edge to circumnavigate the complete graph.
	OrthogonalEdgeRouter::Request nearbyBackEdge;
	nearbyBackEdge.source = QPointF(420, 150);
	nearbyBackEdge.target = QPointF(120, 170);
	nearbyBackEdge.sourceNormal = QPointF(1, 0);
	nearbyBackEdge.targetNormal = QPointF(-1, 0);
	nearbyBackEdge.routingBounds = QRectF(-5000, -5000, 12000, 12000);
	nearbyBackEdge.obstacles = {QRectF(220, 100, 100, 130), QRectF(6000, 6000, 200, 120)};
	nearbyBackEdge.stabilityKey = QStringLiteral("parent-back-edge");
	auto nearbyBackResult = OrthogonalEdgeRouter::route(nearbyBackEdge);
	assert(!nearbyBackResult.path.isEmpty());
	assert(!pathHasSelfConflict(nearbyBackResult.path));
	for (const QRectF& obstacle : nearbyBackEdge.obstacles)
		assert(!pathHitsRect(nearbyBackResult.path, obstacle.adjusted(-1, -1, 1, 1)));
	assert(nearbyBackResult.path.length() < 900.0);

	OrthogonalEdgeRouter::Request loop;
	loop.source = QPointF(200, 50);
	loop.stabilityKey = QStringLiteral("self");
	auto loopResult = OrthogonalEdgeRouter::routeSelfLoop(loop, QRectF(0, 0, 200, 120));
	assert(!loopResult.path.isEmpty());

	// A neighboring card occupies the usual vertical leg of a self-loop.
	// Another card above it makes checking only the first obstacle insufficient.
	const QRectF loopCard(0, 0, 200, 120);
	loop.source = QPointF(200, 85);
	loop.obstacles = {QRectF(215, 25, 125, 35), QRectF(245, -100, 95, 85)};
	const auto crowdedLoop = OrthogonalEdgeRouter::routeSelfLoop(loop, loopCard);
	assert(!crowdedLoop.path.isEmpty());
	assert(!pathHasSelfConflict(crowdedLoop.path));
	assert(!pathHitsRect(crowdedLoop.path, loopCard.adjusted(1, 1, -1, -1)));
	for (const QRectF& obstacle : loop.obstacles)
		assert(!pathHitsRect(crowdedLoop.path, obstacle.adjusted(-1, -1, 1, 1)));
	assert(QLineF(crowdedLoop.path.pointAtPercent(0), loop.source).length() < 0.01);
	assert(QLineF(crowdedLoop.path.pointAtPercent(1), crowdedLoop.endPoint).length() < 0.01);
	assert(crowdedLoop.path.length() < 1400.0);

    OrthogonalEdgeRouter::Request closePorts;
    closePorts.source=QPointF(0,0);closePorts.target=QPointF(30,0);
    closePorts.sourceNormal=QPointF(1,0);closePorts.targetNormal=QPointF(-1,0);
    closePorts.sourceRect=QRectF(-200,-60,200,120);closePorts.targetRect=QRectF(30,-60,200,120);
    const auto closeRoute=OrthogonalEdgeRouter::route(closePorts);
    assert(!closeRoute.path.isEmpty());assert(closeRoute.path.length()<=30.1);
    assert(!pathHasSelfConflict(closeRoute.path));
    closePorts.source=QPointF(0,-45);closePorts.target=QPointF(30,-34.615);
    const auto offsetRoute=OrthogonalEdgeRouter::route(closePorts);
    assert(!offsetRoute.path.isEmpty());assert(offsetRoute.path.length()<50);
    assert(!pathHasSelfConflict(offsetRoute.path));


    // Nearly equal obstacle coordinates must not move the port past a blocker.
    OrthogonalEdgeRouter::Request precision;
    precision.source=QPointF(0,0);precision.target=QPointF(300,0);
    precision.sourceNormal=QPointF(1,0);precision.targetNormal=QPointF(-1,0);
    precision.obstacles={QRectF(0,100,9.9995,20),QRectF(30,-5,2,10)};
    const auto preciseRoute=OrthogonalEdgeRouter::route(precision);
    assert(!preciseRoute.path.isEmpty());
    for(const auto& obstacle:precision.obstacles) assert(!pathHitsRect(preciseRoute.path,obstacle));

	return 0;
}
