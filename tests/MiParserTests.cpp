#include "MiParser.h"
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; return 1; } } while (false)
int main() {
    auto r = MiParser::parse(R"(42^done,stack=[frame={level="0",func="main",args=[{name="x",value="}"}]},frame={level="1"}],empty="",dup="1",dup="2")");
    CHECK(r.valid()); CHECK(r.token == 42); CHECK(r.payload.children.size() == 4);
    CHECK(r.payload.field("stack")->children.size() == 2);
    CHECK(r.payload.field("empty")->text.isEmpty());
    CHECK(r.payload.children[3].text == "2");
    r = MiParser::parse(R"(~"literal \\n and \n\t\101\"\\")");
    CHECK(r.valid()); CHECK(r.payload.text == QString("literal \\n and \n\tA\"\\"));
    for (const QString& input : {QString("*stopped,reason=\"breakpoint-hit\",thread-id=\"2\""), QString("=thread-created,id=\"2\""), QString("+download,section=\".text\""), QString("@\"output\""), QString("&\"log\""), QString("7^error,msg=\"bad expression\"")}) CHECK(MiParser::parse(input).valid());
    for (const QString& input : {QString(""), QString("1^"), QString("^done,a=\""), QString("^done,a=[{},]"), QString("^done,a={x=\"1\""), QString("^done,a=\"\\q\""), QString("999999999999^done"), QString("^done junk")}) CHECK(!MiParser::parse(input).valid());
    QString deep = "^done,a=";
    deep += QString(140, '['); deep += "\"x\""; deep += QString(140, ']');
    CHECK(!MiParser::parse(deep).valid());
    return 0;
}
