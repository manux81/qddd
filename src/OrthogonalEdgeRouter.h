#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class OrthogonalEdgeRouter
{
public:
	struct Request {
		QPointF source;
		QPointF target;
		QPointF sourceNormal = QPointF(1.0, 0.0);
		QPointF targetNormal;
		QRectF routingBounds;
		QVector<QRectF> obstacles;
		QVector<QPainterPath> existingEdges;
		QString stabilityKey;
		qreal sourceGap = 0.0;
		qreal laneOffset = 0.0;
	};

	struct Result {
		QPainterPath path;
		QPointF labelPosition;
		QPointF endPoint;
	};

	static Result route(const Request& request);
	static Result routeSelfLoop(const Request& request, const QRectF& cardRect);
};
