#pragma once
#include "DebugSession.h"
#include "RuntimeObjectGraph.h"

// Variable bindings describe access paths. Members belong to their physical
// parent object, including when the children were obtained by dereferencing a
// pointer. Unknown-address members use that canonical parent plus member name.
inline RuntimeObjectGraph buildRuntimeGraph(
    const std::vector<std::unique_ptr<DebugVariable>>& variables,
    const QString& context = {}, const QString& threadId = {})
{
    RuntimeObjectGraph graph;
    QHash<qulonglong, QSet<QString>> knownTypes;
    auto numericAddress=[](const QString& address) {
        bool ok=false; const auto result=address.toULongLong(&ok,0);
        return ok ? result : qulonglong(0);
    };
    std::function<void(const DebugVariable*)> index=[&](const DebugVariable* value) {
        if(!value) return;
        const auto address=numericAddress(value->address);
        if(address && !value->type.isEmpty()) knownTypes[address].insert(RuntimeObjectGraph::normalizedType(value->type));
        for(const auto& child:value->children) index(child.get());
    };
    for(const auto& variable:variables) index(variable.get());
    auto typeAt=[&](const QString& address, const QString& type) {
        if(!type.trimmed().isEmpty()) return type;
        const auto candidates=knownTypes.value(numericAddress(address));
        return candidates.size()==1 ? *candidates.cbegin() : QString();
    };
    std::function<void(const DebugVariable*, const QString&)> visit;
    visit = [&](const DebugVariable* value, const QString& parent) {
        if (!value) return;
        const QString path=value->fullPath();
        const QString fallback=parent.isEmpty() ? context+":"+path : parent+"::member:"+value->name;
        auto& object=graph.ensureObject(value->address,typeAt(value->address,value->type),fallback);
        const QString id=object.id;
        object.kind=value->isPointer ? RuntimeValueKind::PointerStorage :
            value->hasChildren ? RuntimeValueKind::Object : RuntimeValueKind::Scalar;
        object.aliases.insert(path);
        if(!threadId.isEmpty()) object.threads.insert(threadId);
        graph.setMember(id,QStringLiteral("$value"),value->value,value->type);
        graph.setReference(QStringLiteral("variables"),path,id,
            value->type.trimmed().endsWith('&') ? RuntimeRelationship::Reference : RuntimeRelationship::Alias);
        if(!parent.isEmpty()) {
            graph.setMember(parent,value->name,value->value,value->type);
            graph.setReference(parent,value->name,id,
                value->name.startsWith('[') ? RuntimeRelationship::Element : RuntimeRelationship::Member);
        }
        QString childOwner=id;
        if(value->isPointer) {
            const auto address=numericAddress(value->pointeeAddress);
            if(address) {
                QString pointeeType=value->type.trimmed();
                if(pointeeType.endsWith('*')) pointeeType.chop(1);
                auto& target=graph.ensureObject(value->pointeeAddress,typeAt(value->pointeeAddress,pointeeType),"*("+path+")");
                target.aliases.insert("*("+path+")");
                if(!threadId.isEmpty()) target.threads.insert(threadId);
                graph.setReference(id,QStringLiteral("$pointee"),target.id,RuntimeRelationship::Pointer);
                childOwner=target.id;
            } else {
                // A null/invalid pointer has no dereferenced members or target.
                return;
            }
        }
        for(const auto& child:value->children) visit(child.get(),childOwner);
    };
    for(const auto& variable:variables) visit(variable.get(),{});
    return graph;
}
