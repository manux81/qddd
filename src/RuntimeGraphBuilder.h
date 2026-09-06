#pragma once
#include "DebugSession.h"
#include "RuntimeObjectGraph.h"

// Builds debugger-domain objects without requiring a scene or allocating by alias.
// The registry owns objects; paths and pointer edges are separate identities.
inline RuntimeObjectGraph buildRuntimeGraph(const std::vector<std::unique_ptr<DebugVariable>>& variables)
{
    RuntimeObjectGraph graph;
    std::function<void(const DebugVariable*, const QString&)> visit;
    visit = [&](const DebugVariable* value, const QString& parent) {
        if (!value) return;
        const QString path = value->fullPath();
        auto& object = graph.ensureObject(value->address, value->type, path);
        const QString id = object.id;
        graph.setMember(id, path, value->value, value->type);
        graph.setReference(QStringLiteral("variables"), path, id, RuntimeRelationship::Alias);
        if (!parent.isEmpty()) graph.setReference(parent, path, id, RuntimeRelationship::Member);
        if (value->isPointer) {
            bool valid = false;
            const auto address = value->pointeeAddress.toULongLong(&valid, 0);
            if (valid && address != 0) {
                QString pointeeType = value->type.trimmed();
                if (pointeeType.endsWith('*')) pointeeType.chop(1);
                auto& target = graph.ensureObject(value->pointeeAddress, pointeeType, "*("+path+")");
                graph.setReference(id, path, target.id, RuntimeRelationship::Pointer);
            }
        }
        for (const auto& child : value->children) visit(child.get(), id);
    };
    for (const auto& variable : variables) visit(variable.get(), {});
    return graph;
}
