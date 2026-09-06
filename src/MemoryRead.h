#pragma once
#include <QByteArray>
#include <QString>
struct MemoryRead {
    QString address;
    QByteArray bytes;
    QString error;
    bool success() const { return error.isEmpty(); }
};
inline QString formatMemory(const MemoryRead& result) {
    if (!result.success()) return result.error;
    bool ok = false;
    const auto address = result.address.toULongLong(&ok, 0);
    QString text;
    for (int offset=0; offset<result.bytes.size(); offset+=16) {
        text += ok ? QString("%1  ").arg(address+offset, 16, 16, QLatin1Char('0')) : QString("+%1  ").arg(offset, 8, 16, QLatin1Char('0'));
        QString ascii;
        for (int j=0; j<16; ++j) {
            if (offset+j >= result.bytes.size()) { text += "   "; continue; }
            const auto byte = static_cast<unsigned char>(result.bytes[offset+j]);
            text += QString("%1 ").arg(byte, 2, 16, QLatin1Char('0'));
            ascii += byte>=32 && byte<127 ? QChar(byte) : QChar('.');
        }
        text += " |" + ascii + "|\n";
    }
    return text;
}
