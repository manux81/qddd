#include "RuntimeGraphBuilder.h"
// This test only needs the domain path implementation, not a process transport.
QString DebugVariable::fullPath() const { return expression.isEmpty() ? (parent ? parent->fullPath()+"."+name : name) : expression; }
int main() {
    std::vector<std::unique_ptr<DebugVariable>> vars;
    auto object=std::make_unique<DebugVariable>(); object->name="work"; object->address="0x100"; object->type="Node";
    auto ptr=std::make_unique<DebugVariable>(); ptr->name="alias"; ptr->address="0x200"; ptr->type="const Node *"; ptr->isPointer=true; ptr->pointeeAddress="0x100";
    vars.push_back(std::move(object)); vars.push_back(std::move(ptr));
    auto graph=buildRuntimeGraph(vars);
    if (graph.objects().size()!=2) return 1;
    const auto target=RuntimeObjectGraph::identityFor("0x100", "Node");
    const auto source=RuntimeObjectGraph::identityFor("0x200", "const Node *");
    const auto* edge=graph.reference(RuntimeObjectGraph::referenceIdentity(source,"$pointee"));
    if (!edge || edge->destinationObjectId!=target) return 2;
    vars[1]->pointeeAddress="0x0"; graph=buildRuntimeGraph(vars);
    if (graph.reference(RuntimeObjectGraph::referenceIdentity(source,"$pointee"))) return 3;
    if (RuntimeObjectGraph::identityFor("0x0","Node","a")==RuntimeObjectGraph::identityFor("0x0","Node","b")) return 4;
    vars[1]->pointeeAddress="0x100";
    auto member=std::make_unique<DebugVariable>(); member->name="x"; member->value="42";member->parent=vars[0].get();
    vars[0]->children.push_back(std::move(member));vars[0]->hasChildren=true;
    member=std::make_unique<DebugVariable>(); member->name="x";member->value="42";member->parent=vars[1].get();
    vars[1]->children.push_back(std::move(member));vars[1]->hasChildren=true;
    graph=buildRuntimeGraph(vars,"frame1","1");
    const auto* owner=graph.object(target);
    if(!owner || owner->members.value("x").value!="42") return 5;
    if(graph.object(source)->members.contains("x")) return 6;
    const auto* direct=graph.reference(RuntimeObjectGraph::referenceIdentity("variables","work.x"));
    const auto* indirect=graph.reference(RuntimeObjectGraph::referenceIdentity("variables","alias.x"));
    if(!direct || !indirect || direct->destinationObjectId!=indirect->destinationObjectId) return 7;
    if(graph.objects().size()!=3) return 8;
    if(!owner->threads.contains("1")) return 9;
    vars[1]->type.clear(); graph=buildRuntimeGraph(vars);
    const auto unknownSource=RuntimeObjectGraph::identityFor("0x200","");
    edge=graph.reference(RuntimeObjectGraph::referenceIdentity(unknownSource,"$pointee"));
    if(!edge || edge->destinationObjectId!=target) return 10;
    return 0;
}
