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
	int alignmentSweeps = 6;
	bool useLongEdgeHints = true;
};

struct RuntimeLayoutResult
{
	QHash<QString, QPointF> positions;
	QHash<QString, int> layers;
};

// Deterministic, UI-independent, left-to-right layout for runtime object
// graphs. Cycles are condensed with Tarjan SCC before layer assignment;
// virtual nodes guide long edges through intermediate layers.
class RuntimeGraphLayout
{
public:
	static RuntimeLayoutResult compute(
		const QVector<RuntimeLayoutNode>& nodes,
		const QVector<RuntimeLayoutEdge>& edges,
		const RuntimeLayoutOptions& options = {});
};

class RuntimeLayoutStrategy {
public:
    virtual ~RuntimeLayoutStrategy() = default;
    virtual RuntimeLayoutResult layout(const QVector<RuntimeLayoutNode>& nodes,
                                        const QVector<RuntimeLayoutEdge>& edges) const = 0;
};
class HierarchicalLayoutStrategy final : public RuntimeLayoutStrategy {
public:
    RuntimeLayoutResult layout(const QVector<RuntimeLayoutNode>& nodes,
                               const QVector<RuntimeLayoutEdge>& edges) const override {
        return RuntimeGraphLayout::compute(nodes,edges);
    }
};
// Compact object layout retains cycle handling and manual pins from the layered
// algorithm, with shorter routing corridors and reduced inter-component space.
class CompactObjectLayoutStrategy final : public RuntimeLayoutStrategy {
public:
    RuntimeLayoutResult layout(const QVector<RuntimeLayoutNode>& nodes,
                               const QVector<RuntimeLayoutEdge>& edges) const override {
        RuntimeLayoutOptions options;
        options.layerSpacing=55; options.nodeSpacing=20; options.componentSpacing=45;
        return RuntimeGraphLayout::compute(nodes,edges,options);
    }
};
