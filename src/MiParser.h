#pragma once
#include <QString>
#include <QVector>
#include <limits>

// Ordered children preserve repeated MI result names. No UI or transport dependency.
struct MiValue {
    enum Kind { String, Tuple, List } kind = Tuple;
    QString name;
    QString text;
    QVector<MiValue> children;
    const MiValue* field(const QString& key) const {
        for (const auto& child : children) if (child.name == key) return &child;
        return nullptr;
    }
};
struct MiRecord {
    int token = 0;
    QChar kind;
    QString resultClass;
    MiValue payload;
    QString error;
    bool valid() const { return error.isEmpty(); }
};

class MiParser {
public:
    static MiRecord parse(const QString& input) {
        MiParser p(input.trimmed());
        MiRecord r;
        const int begin = p.pos;
        while (p.peek().isDigit()) ++p.pos;
        if (p.pos != begin) {
            bool ok;
            r.token = p.s.left(p.pos).toInt(&ok);
            if (!ok) p.fail("MI token overflow");
        }
        r.kind = p.peek(); ++p.pos;
        if (QStringLiteral("~@&").contains(r.kind) && !r.kind.isNull()) {
            r.payload = p.value(0);
            if (r.payload.kind != MiValue::String) p.fail("Expected stream string");
        } else if (QStringLiteral("^*+=").contains(r.kind) && !r.kind.isNull()) {
            r.resultClass = p.identifier();
            if (r.resultClass.isEmpty()) p.fail("Missing record class");
            while (p.peek() == ',' && p.error.isEmpty()) {
                ++p.pos;
                r.payload.children.append(p.result(0));
            }
        } else p.fail("Unknown MI record kind");
        if (p.pos != p.s.size()) p.fail("Trailing or incomplete MI data");
        r.error = p.error;
        return r;
    }
private:
    explicit MiParser(QString input) : s(std::move(input)) {}
    QString s, error;
    int pos = 0;
    QChar peek() const { return pos < s.size() ? s[pos] : QChar(); }
    void fail(const char* message) { if (error.isEmpty()) error = QString::fromLatin1(message); }
    QString identifier() {
        const int start = pos;
        while (peek().isLetterOrNumber() || peek() == '-' || peek() == '_') ++pos;
        return s.mid(start, pos - start);
    }
    MiValue result(int depth) {
        const QString name = identifier();
        if (name.isEmpty() || peek() != '=') { fail("Expected named result"); return {}; }
        ++pos;
        auto v = value(depth); v.name = name; return v;
    }
    MiValue value(int depth) {
        MiValue v;
        if (depth > 128) { fail("MI nesting limit exceeded"); return v; }
        const QChar opening = peek();
        if (opening == '"') {
            v.kind = MiValue::String; ++pos;
            while (!peek().isNull() && peek() != '"' && error.isEmpty()) {
                QChar c = s[pos++];
                if (c != '\\') { v.text += c; continue; }
                if (peek().isNull()) { fail("Incomplete escape"); break; }
                c = s[pos++];
                const QString codes = QStringLiteral("abfnrtv\\\"'");
                const QString decoded = QStringLiteral("\a\b\f\n\r\t\v\\\"'");
                const int index = codes.indexOf(c);
                if (index >= 0) v.text += decoded[index];
                else if (c >= '0' && c <= '7') {
                    ushort n = c.unicode() - '0';
                    for (int i = 1; i < 3 && peek() >= '0' && peek() <= '7'; ++i)
                        n = n * 8 + s[pos++].unicode() - '0';
                    v.text += QChar(n);
                } else { fail("Invalid C string escape"); }
            }
            if (peek() != '"') fail("Unterminated string"); else ++pos;
            return v;
        }
        if (opening != '{' && opening != '[') { fail("Expected MI value"); return v; }
        v.kind = opening == '{' ? MiValue::Tuple : MiValue::List;
        const QChar closing = opening == '{' ? '}' : ']';
        ++pos;
        while (peek() != closing && error.isEmpty()) {
            if (peek() == '"' || peek() == '{' || peek() == '[') {
                if (v.kind == MiValue::Tuple) { fail("Expected tuple result"); break; }
                v.children.append(value(depth + 1));
            } else v.children.append(result(depth + 1));
            if (peek() == closing || !error.isEmpty()) break;
            if (peek() != ',') { fail("Expected separator"); break; }
            ++pos;
            if (peek() == closing) fail("Trailing separator");
        }
        if (peek() == closing) ++pos; else fail("Unterminated collection");
        return v;
    }
};
