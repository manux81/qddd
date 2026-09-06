#include "MemoryRead.h"
int main() {
    const QString formatted = formatMemory({"0x1000", QByteArray::fromHex("00417fff"), {}});
    if (!formatted.contains("0000000000001000") || !formatted.contains("00 41 7f ff") || !formatted.contains("|.A..|")) return 1;
    if (formatMemory({{}, {}, "unreadable"}) != "unreadable") return 2;
    return 0;
}
