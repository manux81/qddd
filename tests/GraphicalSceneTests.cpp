#include "GdbMiSession.h"
#include "GraphicalVariablesView.h"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QToolButton>
#include <QPolygonF>
#include <QLineF>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QtTest/QTest>
#include <QScrollBar>
#include <QDebug>
int main(int argc, char** argv) {
    QApplication app(argc,argv);
    GdbMiSession session;
    GraphicalVariablesView view;
    view.setSession(&session);
    session.replaceExternalVariables({{"counter","1"},{"other","2"}});
    GraphicalNodeItem* retained = nullptr;
    for (auto* item : view.scene()->items()) if (auto* node=qgraphicsitem_cast<GraphicalNodeItem*>(item))
        if (node->node()->name=="counter") retained=node;
    if (!retained) return 1;
    session.replaceExternalVariables({{"counter","3"},{"new","4"}});
    bool found=false; int nodes=0;
    for (auto* item : view.scene()->items()) if (auto* node=qgraphicsitem_cast<GraphicalNodeItem*>(item)) {
        ++nodes;
        if (node->node()->name=="counter") {
            if (node!=retained || node->node()->value!="3") return 2;
            found=true;
        }
    }
    if (!found || nodes!=2) return 3;
    QImage image(900,600,QImage::Format_ARGB32); QPainter painter(&image);
    view.scene()->render(&painter);
    // Regression for the reported drawSource -> fullPath crash: a card must
    // still render after its source tree and cached child rows are destroyed.
    {
        auto source=std::make_unique<DebugVariable>();source->name="object";source->hasChildren=true;
        auto child=std::make_unique<DebugVariable>();child->name="field";child->value="7";child->parent=source.get();
        source->children.push_back(std::move(child));
        auto* stable=new GraphicalNodeItem(source.get(),&session,"owned");
        view.scene()->addItem(stable);stable->setExpandedRecursively(true);
        stable->boundingRect();
        source->children.clear();source.reset();
        if(stable->node()->children.front()->fullPath()!="object.field") return 17;
        view.scene()->render(&painter);
        DebugVariable next;next.name="object";next.value="8";
        stable->rebind(&next);
        if(!stable->node()->children.empty() || stable->node()->value!="8") return 18;
        delete stable;
    }
    session.replaceExternalVariables({});
    for (auto* item : view.scene()->items()) if (qgraphicsitem_cast<GraphicalNodeItem*>(item)) return 4;
    // A moved third-party card must invalidate an edge even when it has no
    // incident edges. Stable reroutes must not use stale paths as obstacles.
    session.replaceExternalVariables({{"a","1"},{"b","2"},{"obstacle","3"}});
    for (auto* button : view.findChildren<QToolButton*>())
        if (button->accessibleName()=="Automatic layout") button->setChecked(false);
    QHash<QString,GraphicalNodeItem*> byName;
    for (auto* item : view.scene()->items()) if (auto* node=qgraphicsitem_cast<GraphicalNodeItem*>(item)) byName[node->node()->name]=node;
    auto* a=byName["a"]; auto* b=byName["b"]; auto* obstacle=byName["obstacle"];
    a->setPos(0,0);b->setPos(800,0);obstacle->setPos(300,250);
    auto* edge=new GraphicalEdgeItem(a,b,"a","pointer","b");
    view.scene()->addItem(edge);a->addEdge(edge);b->addEdge(edge);edge->updatePosition();
    QCoreApplication::processEvents();
    if(edge->path().isEmpty()) return 5;
    const auto original=edge->path();
    obstacle->setPos(400,0);
    QCoreApplication::processEvents();
    if(edge->path().isEmpty() || edge->path()==original) return 6;
    for(auto* card : {a,b,obstacle}) {
        const QRectF interior=card->sceneBoundingRect().adjusted(9,1,-9,-1);
        for(const auto& polygon:edge->path().toSubpathPolygons())
            for(int i=1;i<polygon.size();++i) {
                const QLineF segment(polygon[i-1],polygon[i]);
                if(interior.contains(segment.p1()) || interior.contains(segment.p2())) return 7;
                for(const auto& side : {QLineF(interior.topLeft(),interior.topRight()),QLineF(interior.topRight(),interior.bottomRight()),QLineF(interior.bottomRight(),interior.bottomLeft()),QLineF(interior.bottomLeft(),interior.topLeft())}) {
                    QPointF intersection;
                    if(segment.intersects(side,&intersection)==QLineF::BoundedIntersection) return 8;
                }
            }
    }
    const auto routed=edge->path();
    view.scheduleEdgeRouting();QCoreApplication::processEvents();
    if(edge->path()!=routed) return 9;
    if(edge->zValue()>=a->zValue()) return 10;
    auto* alias=new GraphicalEdgeItem(a,b,"a","secondPointer","b");
    view.scene()->addItem(alias);a->addEdge(alias);b->addEdge(alias);alias->updatePosition();
    QCoreApplication::processEvents();
    if(edge->isVisible()==alias->isVisible()) return 11;
    auto* representative=edge->isVisible() ? edge : alias;
    if(!representative->toolTip().contains("pointer") || !representative->toolTip().contains("secondPointer")) return 12;

    // The socket is painted by the card, inside the header; the edge starts
    // at the visible card boundary and has no half-hidden circle of its own.
    const auto socket=a->outputSocketRect();
    if(!a->boundingRect().adjusted(8,0,-8,0).contains(socket.adjusted(-1,-1,1,1))) return 13;
    if(socket.bottom()>=30 || socket.top()<=0) return 14;
    view.resize(1000,600);view.show();view.centerOn(QPointF(400,0));
    QCoreApplication::processEvents();
    const QPoint click=view.mapFromScene(a->mapToScene(QPointF(25,15)));
    QTest::mousePress(view.viewport(),Qt::LeftButton,Qt::NoModifier,click);
    if(view.scene()->mouseGrabberItem()!=a) { qWarning() << "Drag not grabbed" << click << view.viewport()->rect() << view.itemAt(click) << a; return 15; }
    const QRectF bounds=view.scene()->sceneRect();
    const int hScroll=view.horizontalScrollBar()->value(),vScroll=view.verticalScrollBar()->value();
    QTest::mouseMove(view.viewport(),click+QPoint(40,20));
    view.scheduleEdgeRouting();QCoreApplication::processEvents();
    if(view.scene()->sceneRect()!=bounds || view.horizontalScrollBar()->value()!=hScroll || view.verticalScrollBar()->value()!=vScroll) return 16;
    QTest::mouseRelease(view.viewport(),Qt::LeftButton,Qt::NoModifier,click+QPoint(40,20));QCoreApplication::processEvents();
    image.fill(QColor(25,25,25));
    view.scene()->render(&painter);
    painter.end();
    image.save("graphical-routing-preview.png");
    return 0;
}
