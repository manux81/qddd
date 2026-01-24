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
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <QColor>
#include <QString>

class GraphicalNodeItem;

class GraphicalEdgeItem : public QGraphicsPathItem
{
public:
    GraphicalEdgeItem(GraphicalNodeItem* from,
                      GraphicalNodeItem* to);

    void updatePosition();

private:
    GraphicalNodeItem* m_from;
    GraphicalNodeItem* m_to;
};


class GraphicalNodeItem : public QGraphicsItem
{
public:
    explicit GraphicalNodeItem(VarNode* node);

    QRectF boundingRect() const override;
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    VarNode* node() const;

    QPointF inputPort() const;
    QPointF outputPortFor(VarNode* child) const;
    void recalculateWidth();
    void addEdge(GraphicalEdgeItem* e);
    QHash<VarNode*, bool> m_expanded;


private:
    void drawHeader(QPainter* painter);
    void drawSource(QPainter* painter);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent*) override;
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override;

private:
    VarNode* m_node = nullptr;
    QList<GraphicalEdgeItem*> m_edges;
    int m_width = 260;

    static constexpr int HeaderHeight = 26;
    static constexpr int RowHeight    = 22;
    static constexpr int PortRadius   = 5;
    static constexpr int LeftPadding  = 10;
    static constexpr int RightPadding = 10;
};



class GraphicalVariablesView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit GraphicalVariablesView(QWidget* parent = nullptr);
    ~GraphicalVariablesView() override;

    void setSession(DebugSession* session);

public slots:
    void refresh();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitGraph();


protected:
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void drawBackground(QPainter* painter,
                        const QRectF& rect) override;


private:
    void layoutTree(GraphicalNodeItem* item,
                    int depth,
                    int& y) __deprecated;

private:
    QGraphicsScene* m_scene = nullptr;
    DebugSession*   m_session = nullptr;
};
