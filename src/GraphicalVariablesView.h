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

#pragma once

#include "DebugSession.h"

#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QTimer>

class GraphicalNodeItem;

class GraphicalEdgeItem : public QGraphicsPathItem
{
public:
	GraphicalEdgeItem(GraphicalNodeItem* from,
					  GraphicalNodeItem* to,
					  DebugVariable* fromChild);

	void updatePosition();

private:
	GraphicalNodeItem* m_from;
	GraphicalNodeItem* m_to;
	DebugVariable*     m_fromChild;

	static constexpr int SEGMENTS = 10;
	QPointF m_pos[SEGMENTS]{};
	QPointF m_vel[SEGMENTS]{};
	QPointF m_targetEnd;
	QTimer  m_timer;

	void tick();
};


class GraphicalNodeItem : public QGraphicsItem
{
public:
	explicit GraphicalNodeItem(DebugVariable* node);

	QRectF boundingRect() const override;
	void paint(QPainter* painter,
			   const QStyleOptionGraphicsItem*,
			   QWidget*) override;

	DebugVariable* node() const { return m_node; }

	QPointF inputPort() const;
	QPointF outputPortFor(DebugVariable* child) const;

	void recalculateWidth();
	void addEdge(GraphicalEdgeItem* e);

protected:
	void mousePressEvent(QGraphicsSceneMouseEvent*) override;
	QVariant itemChange(GraphicsItemChange,
						const QVariant&) override;

private:
	void drawHeader(QPainter* painter, const QRectF& r);
	void drawSource(QPainter* painter);

private:
	DebugVariable* m_node = nullptr;
	QList<GraphicalEdgeItem*> m_edges;
	QHash<DebugVariable*, bool> m_expanded;

	int m_width = 260;

	static constexpr int HeaderHeight = 26;
	static constexpr int RowHeight    = 22;
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

protected:
	void wheelEvent(QWheelEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void drawBackground(QPainter*, const QRectF&) override;

private:
	QGraphicsScene*  m_scene   = nullptr;
	DebuggerSession* m_session = nullptr;
};

