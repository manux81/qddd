#include "OrthogonalEdgeRouter.h"
#include <QLineF>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace {
constexpr qreal clearance = 9.0;
constexpr qreal bendCost = 18.0;
bool same(const QPointF& a,const QPointF& b) { return QLineF(a,b).length()<0.001; }
bool hits(const QPointF& a,const QPointF& b,const QRectF& r) {
    if (std::abs(a.y()-b.y())<0.001)
        return a.y()>=r.top() && a.y()<=r.bottom() && std::max(a.x(),b.x())>=r.left() && std::min(a.x(),b.x())<=r.right();
    if (std::abs(a.x()-b.x())<0.001)
        return a.x()>=r.left() && a.x()<=r.right() && std::max(a.y(),b.y())>=r.top() && std::min(a.y(),b.y())<=r.bottom();
    return true;
}
bool clear(const QPointF& a,const QPointF& b,const QVector<QRectF>& obstacles) {
    for (const auto& rect:obstacles) if(hits(a,b,rect)) return false;
    return true;
}
bool conflicts(const QLineF& a,const QLineF& b,bool allowSharedEndpoint) {
    const bool horizontal=std::abs(a.dy())<0.001 && std::abs(b.dy())<0.001 && std::abs(a.y1()-b.y1())<0.001;
    const bool vertical=std::abs(a.dx())<0.001 && std::abs(b.dx())<0.001 && std::abs(a.x1()-b.x1())<0.001;
    if(horizontal && std::min(std::max(a.x1(),a.x2()),std::max(b.x1(),b.x2()))-std::max(std::min(a.x1(),a.x2()),std::min(b.x1(),b.x2()))>0.001) return true;
    if(vertical && std::min(std::max(a.y1(),a.y2()),std::max(b.y1(),b.y2()))-std::max(std::min(a.y1(),a.y2()),std::min(b.y1(),b.y2()))>0.001) return true;
    QPointF point;
    if(a.intersects(b,&point)!=QLineF::BoundedIntersection) return false;
    return !allowSharedEndpoint || !((same(point,a.p1()) || same(point,a.p2())) && (same(point,b.p1()) || same(point,b.p2())));
}
QVector<QPointF> simplify(const QVector<QPointF>& input) {
    QVector<QPointF> result;
    for(const auto& p:input) {
        if(!result.isEmpty() && same(result.back(),p)) continue;
        while(result.size()>=2) {
            const auto a=result[result.size()-2], b=result.back();
            const auto u=b-a,v=p-b;
            if(std::abs(u.x()*v.y()-u.y()*v.x())>0.001 || QPointF::dotProduct(u,v)<0) break;
            result.removeLast();
        }
        result.append(p);
    }
    return result;
}
QPainterPath pathFor(const QVector<QPointF>& input,qreal gap) {
    const auto points=simplify(input); QPainterPath path;
    if(points.size()<2) return path;
    QLineF first(points[0],points[1]); first.setLength(std::min(first.length(),std::max(qreal(0),gap)));
    path.moveTo(first.p2());
    for(int i=1;i+1<points.size();++i) {
        QLineF before(points[i],points[i-1]),after(points[i],points[i+1]);
        const qreal radius=std::min(qreal(6),std::min(before.length(),after.length())/4);
        before.setLength(radius); after.setLength(radius);
        path.lineTo(before.p2()); path.quadTo(points[i],after.p2());
    }
    path.lineTo(points.back()); return path;
}
qreal manhattan(const QPointF& a,const QPointF& b) { return std::abs(a.x()-b.x())+std::abs(a.y()-b.y()); }
QVector<QLineF> linesFor(const QVector<QPainterPath>& paths) {
    QVector<QLineF> lines;
    for(const auto& path:paths) for(const auto& polygon:path.toSubpathPolygons())
        for(int i=1;i<polygon.size();++i) lines.append(QLineF(polygon[i-1],polygon[i]));
    return lines;
}
qreal crossingCost(const QPointF& a,const QPointF& b,const QVector<QLineF>& lines) {
    qreal cost=0; QLineF segment(a,b);
    for(const auto& line:lines) {
        QPointF p;
        if(segment.intersects(line,&p)==QLineF::BoundedIntersection && !same(p,a) && !same(p,b) && !same(p,line.p1()) && !same(p,line.p2())) cost+=12;
    }
    // A crossing may justify a small detour, never a tour around the graph.
    return std::min(cost,qreal(36));
}
QVector<QPointF> search(const QPointF& start,const QPointF& goal,const QVector<QRectF>& obstacles,
                       const OrthogonalEdgeRouter::Request& request) {
    QVector<qreal> xs{start.x(),goal.x()},ys{start.y(),goal.y()};
    for(const auto& rect:obstacles) { xs<<rect.left()-1<<rect.right()+1; ys<<rect.top()-1<<rect.bottom()+1; }
    // Nearby free lanes also handle coincident ports without any third-party obstacles.
    xs<<start.x()-24<<start.x()+24<<goal.x()-24<<goal.x()+24;
    ys<<start.y()-24<<start.y()+24<<goal.y()-24<<goal.y()+24;
    auto unique=[](QVector<qreal>& values) {
        std::sort(values.begin(),values.end());
        values.erase(std::unique(values.begin(),values.end()),values.end());
    }; unique(xs);unique(ys);
    const int width=xs.size(),height=ys.size();
    // Bound allocation for malformed/hostile requests. No unsafe fallback is drawn.
    if(qint64(width)*height>2000000) return {};
    auto coord=[](const QVector<qreal>& values,qreal value){return int(std::lower_bound(values.begin(),values.end(),value)-values.begin());};
    const int first=coord(ys,start.y())*width+coord(xs,start.x());
    const int last=coord(ys,goal.y())*width+coord(xs,goal.x());
    const int states=width*height*2;
    QVector<qreal> distance(states,std::numeric_limits<qreal>::infinity());
    QVector<int> previous(states,-1);
    struct Visit {qreal estimate; qreal distance; int state;};
    auto compare=[](const Visit& a,const Visit& b){return a.estimate!=b.estimate ? a.estimate>b.estimate : a.state>b.state;};
    std::priority_queue<Visit,std::vector<Visit>,decltype(compare)> pending(compare);
    const int initial=first*2+(std::abs(request.sourceNormal.y())>0.5 ? 1:0);
    distance[initial]=0;pending.push({manhattan(start,goal),0,initial});
    const auto existing=linesFor(request.existingEdges);
    int found=-1;
    while(!pending.empty()) {
        const auto current=pending.top();pending.pop();
        if(current.distance!=distance[current.state]) continue;
        const int vertex=current.state/2,x=vertex%width,y=vertex/width;
        const QPointF a(xs[x],ys[y]);
        if(vertex==last) { found=current.state;break; }
        const int adjacent[]={x>0?vertex-1:-1,x+1<width?vertex+1:-1,y>0?vertex-width:-1,y+1<height?vertex+width:-1};
        for(int next:adjacent) {
            if(next<0) continue;
            const QPointF b(xs[next%width],ys[next/width]);
            if(vertex==first && QPointF::dotProduct(b-a,request.sourceNormal)<-0.001) continue;
            if(next==last && QPointF::dotProduct(b-a,request.targetNormal)>0.001) continue;
            if(!clear(a,b,obstacles)) continue;
            if(conflicts(QLineF(a,b),QLineF(request.source,start),true) ||
               conflicts(QLineF(a,b),QLineF(goal,request.target),true)) continue;
            const int direction=std::abs(a.x()-b.x())<0.001 ? 1:0;
            const int state=next*2+direction;
            qreal cost=current.distance+manhattan(a,b)+(direction!=current.state%2 ? bendCost:0)+crossingCost(a,b,existing);
            if(cost+0.001>=distance[state]) continue;
            distance[state]=cost;previous[state]=current.state;
            pending.push({cost+manhattan(b,goal),cost,state});
        }
    }
    QVector<QPointF> result;
    for(int state=found;state>=0;state=previous[state]) { const int vertex=state/2;result.prepend(QPointF(xs[vertex%width],ys[vertex/width])); }
    return simplify(result);
}
}

OrthogonalEdgeRouter::Result OrthogonalEdgeRouter::route(const Request& request) {
    Result result;result.endPoint=request.target;
    QVector<QRectF> obstacles;
    for(const auto& rect:request.obstacles) if(rect.isValid()) obstacles.append(rect.adjusted(-clearance,-clearance,clearance,clearance));
    const QPointF direction=request.target-request.source;
    const bool straight=manhattan(request.source,request.target)>0 &&
        (std::abs(direction.x())<0.001 || std::abs(direction.y())<0.001) &&
        QPointF::dotProduct(direction,request.sourceNormal)>0 &&
        QPointF::dotProduct(direction,request.targetNormal)<=0 &&
        std::abs(direction.x()*request.sourceNormal.y()-direction.y()*request.sourceNormal.x())<0.001 &&
        std::abs(direction.x()*request.targetNormal.y()-direction.y()*request.targetNormal.x())<0.001;
    if(straight && clear(request.source,request.target,obstacles) &&
        (!request.sourceRect.isValid() || !hits(request.source,request.target,request.sourceRect.adjusted(0.1,0.1,-0.1,-0.1))) &&
        (!request.targetRect.isValid() || !hits(request.source,request.target,request.targetRect.adjusted(0.1,0.1,-0.1,-0.1)))) {
        result.path=pathFor({request.source,request.target},request.sourceGap);
        result.labelPosition=(request.source+request.target)/2; return result;
    }
    qreal stub=20, portClearance=clearance;
    if(QPointF::dotProduct(request.sourceNormal,request.targetNormal)<-0.99) {
        const qreal gap=QPointF::dotProduct(direction,request.sourceNormal);
        if(gap>0) { stub=std::min(stub,gap/2); portClearance=std::min(clearance,gap/3); }
    }
    const auto sourceCard=request.sourceRect.adjusted(-portClearance,-portClearance,portClearance,portClearance);
    const auto targetCard=request.targetRect.adjusted(-portClearance,-portClearance,portClearance,portClearance);
    const QPointF sourceLead=request.source+request.sourceNormal*stub;
    const QPointF targetLead=request.target+request.targetNormal*stub;
    if(!clear(request.source,sourceLead,obstacles) || !clear(targetLead,request.target,obstacles)) return result;
    if(request.sourceRect.isValid() && hits(targetLead,request.target,sourceCard) && request.sourceRect!=request.targetRect) return result;
    if(request.targetRect.isValid() && hits(request.source,sourceLead,targetCard) && request.sourceRect!=request.targetRect) return result;
    if(request.sourceRect.isValid()) obstacles.append(sourceCard);
    if(request.targetRect.isValid() && request.targetRect!=request.sourceRect) obstacles.append(targetCard);
    auto middle=search(sourceLead,targetLead,obstacles,request);
    if(middle.isEmpty()) return result;
    QVector<QPointF> points{request.source};points+=middle;points.append(request.target);
    points=simplify(points);
    for(int i=1;i<points.size();++i) for(int j=i+1;j<points.size();++j)
        if(conflicts(QLineF(points[i-1],points[i]),QLineF(points[j-1],points[j]),j==i+1)) return result;
    // Validate the complete path, including the two mandatory port leads.
    // A missing route stays empty; no unchecked line is drawn through a card.
    for(int i=1;i<points.size();++i) {
        for(const auto& obstacle:request.obstacles)
            if(hits(points[i-1],points[i],obstacle.adjusted(-clearance,-clearance,clearance,clearance))) return result;
        for(const auto& card:{request.sourceRect,request.targetRect})
            if(card.isValid() && hits(points[i-1],points[i],card.adjusted(0.1,0.1,-0.1,-0.1))) return result;
    }
    result.path=pathFor(points,request.sourceGap);
    qreal longest=-1;
    for(int i=1;i<points.size();++i) {
        const auto length=QLineF(points[i-1],points[i]).length();
        if(length>longest) {longest=length;result.labelPosition=(points[i-1]+points[i])/2;}
    }
    return result;
}

OrthogonalEdgeRouter::Result OrthogonalEdgeRouter::routeSelfLoop(const Request& request,const QRectF& cardRect) {
    Request loop=request;
    loop.sourceRect=cardRect;loop.targetRect=cardRect;
    loop.target=QPointF(cardRect.center().x(),cardRect.top());loop.targetNormal=QPointF(0,-1);
    return route(loop);
}
