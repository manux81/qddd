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
    const auto* edge=graph.reference(RuntimeObjectGraph::referenceIdentity(source,"alias"));
    if (!edge || edge->destinationObjectId!=target) return 2;
    vars[1]->pointeeAddress="0x0"; graph=buildRuntimeGraph(vars);
    if (graph.reference(RuntimeObjectGraph::referenceIdentity(source,"alias"))) return 3;
    if (RuntimeObjectGraph::identityFor("0x0","Node","a")==RuntimeObjectGraph::identityFor("0x0","Node","b")) return 4;
    return 0;
}
