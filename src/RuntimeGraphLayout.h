/*
 * Copyright (c) 2026, Manuele Conti
 * All rights reserved.
 */

#pragma once

#include <QHash>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

struct RuntimeLayoutNode
{
	QString id;
	QSizeF size;
	QPointF previousPosition;
	bool hasPreviousPosition = false;
	bool pinned = false;
};

struct RuntimeLayoutEdge
{
	QString id;
	QString sourceId;
	QString destinationId;
};

struct RuntimeLayoutOptions
{
	qreal layerSpacing = 110.0;
	qreal nodeSpacing = 42.0;
	qreal componentSpacing = 100.0;
	int crossingReductionSweeps = 6;
};

struct RuntimeLayoutResult
{
	QHash<QString, QPointF> positions;
	QHash<QString, int> layers;
};

// Deterministic, UI-independent, left-to-right layout for runtime object
// graphs. Cycles are condensed with Tarjan SCC before layer assignment.
class RuntimeGraphLayout
{
public:
	static RuntimeLayoutResult compute(
		const QVector<RuntimeLayoutNode>& nodes,
		const QVector<RuntimeLayoutEdge>& edges,
		const RuntimeLayoutOptions& options = {});
};
