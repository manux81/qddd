#include "GdbMiSession.h"
#include "GraphicalVariablesView.h"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QToolButton>
#include <QPolygonF>
#include <QLineF>
#include <QElapsedTimer>
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

    image.fill(QColor(25,25,25));
    view.scene()->render(&painter);
    painter.end();
    image.save("graphical-routing-preview.png");
    return 0;
}
