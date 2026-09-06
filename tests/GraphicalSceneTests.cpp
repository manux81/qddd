#include "GraphicalVariablesView.h"
#include <QApplication>
#include <QImage>
#include <QPainter>
int main(int argc, char** argv) {
    QApplication app(argc,argv);
    DebuggerSession session;
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
    return 0;
}
