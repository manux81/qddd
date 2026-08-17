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

#pragma once

#include "DebugSession.h"
#include "RuntimeObjectGraph.h"
#include "RuntimeGraphLayout.h"

#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QTimer>
#include <QContextMenuEvent>
#include <functional>
#include <vector>

class GraphicalNodeItem;
class QGraphicsSceneWheelEvent;
class QToolButton;

struct VisibleRow {
	DebugVariable* node;
	int indent;
};

class GraphicalEdgeItem : public QGraphicsPathItem
{
public:
	enum { Type = UserType + 1 };
	int type() const override { return Type; }

	GraphicalEdgeItem(GraphicalNodeItem* from,
					  GraphicalNodeItem* to,
					  QString sourceObjectId,
					  QString sourceExpression,
					  QString destinationObjectId,
					  RuntimeChangeState change = RuntimeChangeState::Unchanged);

	void updatePosition();
	QRectF boundingRect() const override;
	void paint(QPainter* painter,
			   const QStyleOptionGraphicsItem* option,
			   QWidget* widget = nullptr) override;
	GraphicalNodeItem* sourceNode() const { return m_from; }
	GraphicalNodeItem* destinationNode() const { return m_to; }
	const QString& sourceExpression() const { return m_sourceExpression; }

private:
	GraphicalNodeItem* m_from;
	GraphicalNodeItem* m_to;
	QString m_sourceObjectId;
	QString m_sourceExpression;
	QString m_destinationObjectId;
	RuntimeChangeState m_change;

	static constexpr int SEGMENTS = 10;
	QPointF m_pos[SEGMENTS]{};
	QPointF m_vel[SEGMENTS]{};
	QPointF m_targetEnd;
	QPointF m_labelPosition;
	qreal   m_routeOffset = 0.0;
	QTimer  m_timer;

	void tick();
};


class GraphicalNodeItem : public QGraphicsItem
{
public:
	enum { Type = UserType + 2 };
	int type() const override { return Type; }

	explicit GraphicalNodeItem(DebugVariable* node,
	                           DebuggerSession* session,
	                           QString layoutKey = {});

	QRectF boundingRect() const override;
	void paint(QPainter* painter,
			   const QStyleOptionGraphicsItem*,
			   QWidget*) override;

	DebugVariable* node() const { return m_node; }
	const QString& layoutKey() const { return m_layoutKey; }

	QPointF inputPort() const;
	QPointF outputPortFor(DebugVariable* child) const;
	QPointF outputPortForExpression(const QString& expression) const;
	void setExpandedRecursively(bool expanded, int maxDepth = 8);
	QSet<QString> expandedExpressions() const;
	void restoreExpandedExpressions(const QSet<QString>& expressions);

	void recalculateWidth();
	void addEdge(GraphicalEdgeItem* e);
	DebugVariable* variableAt(const QPointF& localPos) const;
	void setPositionChangedCallback(std::function<void(const QString&, const QPointF&)> cb);
	void setUserMovedCallback(std::function<void(const QString&, const QPointF&)> cb);
	void setGeometryChangedCallback(std::function<void()> cb);

protected:
	void mousePressEvent(QGraphicsSceneMouseEvent*) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent*) override;
	void wheelEvent(QGraphicsSceneWheelEvent*) override;
	QVariant itemChange(GraphicsItemChange,
						const QVariant&) override;

private:
	void drawHeader(QPainter* painter, const QRectF& r);
	void drawSource(QPainter* painter);
	void setPage(int page, int pageCount);
	const QVector<VisibleRow>& cachedRows() const;

private:
	DebugVariable* m_node = nullptr;
	DebuggerSession* m_session = nullptr;
	QString m_layoutKey;
	std::function<void(const QString&, const QPointF&)> m_onPositionChanged;
	std::function<void(const QString&, const QPointF&)> m_onUserMoved;
	std::function<void()> m_onGeometryChanged;
	QList<GraphicalEdgeItem*> m_edges;
	QHash<DebugVariable*, bool> m_expanded;
	QSet<QString> m_expandedExpressionState;

	int m_width = 260;
	int m_page = 0;
	QPointF m_dragStartPosition;
	bool m_draggingHeader = false;
	mutable QVector<VisibleRow> m_cachedRows;
	mutable bool m_rowsDirty = true;

	static constexpr int HeaderHeight = 30;
	static constexpr int RowHeight    = 26;
	static constexpr int LeftPadding  = 10;
};


class GraphicalVariablesView : public QGraphicsView
{
	Q_OBJECT
public:
	explicit GraphicalVariablesView(QWidget* parent = nullptr);
	~GraphicalVariablesView() override;

	void setSession(DebuggerSession* session);

public slots:
	void refresh();
	void zoomIn();
	void zoomOut();
	void resetZoom();
	void fitGraph();
	void autoLayout();

protected:
	void wheelEvent(QWheelEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void contextMenuEvent(QContextMenuEvent*) override;
	void drawBackground(QPainter*, const QRectF&) override;

private:
	void createDisplayExpression(const QString& dependsOn = {});
	void deleteDisplayExpression(const QString& expression);
	void replaceDisplayWithDereference(DebugVariable* variable);
	void editVariableValue(DebugVariable* variable);
	void openPointerNode(DebugVariable* ptrVar, GraphicalNodeItem* fromItem);
	void ensurePointerNodeOpen(const QString& pointerExpr,
							   DebugVariable* ptrVar,
							   GraphicalNodeItem* fromItem,
							   bool recenterView = true);
	void reopenDependentPointerExpressions(DebugVariable* target,
	                                       GraphicalNodeItem* targetItem);
	void scheduleAutoLayout();
	void rememberNodePosition(const QString& key, const QPointF& pos);
	QPointF positionForNode(const QString& key, const QPointF& defaultPos) const;
	void configureNodeItem(GraphicalNodeItem* item);
	void applyAutomaticLayout(bool fitAfterLayout = false);
	void setAutoLayoutEnabled(bool enabled);

	QGraphicsScene*  m_scene   = nullptr;
	DebuggerSession* m_session = nullptr;

	QHash<QString, QPointF> m_nodePositions;
	QHash<QString, QSet<QString>> m_nodeExpandedExpressions;
	QSet<QString> m_pinnedNodeKeys;
	std::vector<std::unique_ptr<DebugVariable>> m_dynamicRoots;
	QHash<QString, DebugVariable*> m_dynamicRootByKey;
	QHash<DebugVariable*, GraphicalNodeItem*> m_dynamicItems;
	QSet<QString> m_openPointerExprs;
	QHash<QString, QString> m_displayDependencies;
	quint64 m_refreshGeneration = 0;
	QToolButton* m_autoLayoutButton = nullptr;
	bool m_autoLayoutEnabled = true;
	bool m_refreshInProgress = false;
	bool m_refreshPending = false;
	bool m_layoutScheduled = false;
};
