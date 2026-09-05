#include "ValueHistory.h"
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << '\n'; return 1; } } while(false)
int main() {
    QVector<ExecutionSnapshot> snapshots(5);
    snapshots[0].variableValues["a.b"]="1";
    snapshots[1].variableValues["a.b"]="1";
    snapshots[2].variableValues["a.b"]="0x10";
    snapshots[4].variableValues["a.b"]="text";
    const auto points=valueHistory(snapshots,"a.b");
    CHECK(points.size()==5); CHECK(!points[1].changed);
    CHECK(points[2].numeric && points[2].numericValue==16 && points[2].changed);
    CHECK(!points[3].available && points[3].changed);
    CHECK(points[4].available && !points[4].numeric && points[4].changed);
    CHECK(valueHistory({}, "a").isEmpty());
    return 0;
}
