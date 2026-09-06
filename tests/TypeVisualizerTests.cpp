#include "TypeVisualizer.h"
int main() {
    TypeVisualizerRegistry registry;
    auto value=registry.visualize("std::vector<int>","length 2",{{"[0]","1","int",{}},{"[1]","2","int",{}}},"array");
    if(value.kind!=SemanticValue::Sequence || value.logicalSize!=2) return 1;
    value=registry.visualize("std::map<int, int>","map",{{"0","key","int",{}},{"1","value","int",{}}},"map");
    if(value.kind!=SemanticValue::Associative || value.children.size()!=1 || value.children[0].name!="key") return 2;
    for(const QString& type : {"std::array<int,2>","std::list<int>","std::deque<int>","std::set<int>","std::unordered_set<int>","QVector<int>","QList<int>"})
        if(registry.visualize(type,{},{}).kind!=SemanticValue::Sequence) return 3;
    if(registry.visualize("std::__1::basic_string<char>","hello",{},"string").kind!=SemanticValue::String) return 4;
    if(registry.visualize("std::optional<int>",{},{}).kind!=SemanticValue::Optional) return 5;
    if(registry.visualize("std::variant<int, bool>",{},{}).kind!=SemanticValue::Variant) return 6;
    for(const QString& type : {"std::unique_ptr<int>","std::shared_ptr<int>","std::weak_ptr<int>","QSharedPointer<int>"})
        if(registry.visualize(type,{},{}).kind!=SemanticValue::SmartPointer) return 7;
    return 0;
}
