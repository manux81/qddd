#pragma once

// Execution-flow (path analyzer) data model.
//
// Minimal prototype over recorded model data: builds a call tree from
// FunctionEnter/FunctionExit events. No backend interaction; empty when the
// backend only records stops (current state). Placeholder UX renders this
// tree; see HistoryView.

#include "HistoryTypes.h"

#include <QString>

#include <memory>
#include <vector>

namespace qddd {
namespace history {

struct CallNode {
    QString function;
    SourceLocation location;
    int samples = 0;
    std::vector<std::unique_ptr<CallNode>> children;
};

// Fold a FunctionEnter/Exit event stream into a call tree. Unbalanced exits
// pop the stack defensively; non-function events are ignored.
inline std::unique_ptr<CallNode> buildCallTree(const std::vector<TraceEvent> &events)
{
    auto root = std::make_unique<CallNode>();
    root->function = QStringLiteral("<root>");
    std::vector<CallNode *> stack{root.get()};
    for (const TraceEvent &event : events) {
        if (event.type == TraceEventType::FunctionEnter) {
            auto child = std::make_unique<CallNode>();
            child->function = event.source ? event.source->function
                                           : traceEventTypeName(event.type);
            if (event.source)
                child->location = *event.source;
            child->samples = 1;
            CallNode *raw = child.get();
            stack.back()->children.push_back(std::move(child));
            stack.push_back(raw);
        } else if (event.type == TraceEventType::FunctionExit) {
            if (stack.size() > 1)
                stack.pop_back();
        }
    }
    return root;
}

} // namespace history
} // namespace qddd
