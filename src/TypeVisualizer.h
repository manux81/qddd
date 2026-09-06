#pragma once
#include <QString>
#include <QVector>
#include <QStringList>
#include <memory>
#include <vector>

struct SemanticChild { QString name; QString value; QString type; QString expression; };
struct SemanticValue {
    enum Kind { Scalar, Sequence, Associative, String, Optional, Variant, SmartPointer, Object } kind = Object;
    QString displayName;
    QString summary;
    QVector<SemanticChild> children;
    int logicalSize = -1;
    bool truncated = false;
    QString error;
};
class TypeVisualizer {
public:
    virtual ~TypeVisualizer() = default;
    virtual bool supports(const QString& type) const = 0;
    virtual SemanticValue visualize(const QString& type, const QString& summary,
                                    const QVector<SemanticChild>& children, const QString& hint) const = 0;
};
class StandardTypeVisualizer final : public TypeVisualizer {
public:
    bool supports(const QString& type) const override { return classify(type) != SemanticValue::Object; }
    SemanticValue visualize(const QString& type, const QString& summary,
                            const QVector<SemanticChild>& children, const QString& hint) const override {
        SemanticValue result; result.displayName = type; result.summary = summary;
        result.kind = classify(type); result.children = children;
        if (hint == "string") result.kind = SemanticValue::String;
        if (hint == "map") result.kind = SemanticValue::Associative;
        if (hint == "array" && result.kind == SemanticValue::Object) result.kind = SemanticValue::Sequence;
        if (result.kind == SemanticValue::Sequence) result.logicalSize = children.size();
        // MI's map displayhint defines alternating key/value logical children.
        if (hint == "map") {
            result.children.clear();
            for (int i=0; i+1<children.size(); i+=2) {
                auto child = children[i+1]; child.name = children[i].value;
                result.children.append(child);
            }
            result.logicalSize = result.children.size();
        }
        return result;
    }
private:
    static SemanticValue::Kind classify(QString type) {
        type.replace("std::__1::", "std::"); type.replace("std::__cxx11::", "std::");
        const QString base = type.section('<',0,0).trimmed();
        if (QStringList{"std::vector","std::array","std::list","std::deque","QList","QVector","std::set","std::unordered_set"}.contains(base)) return SemanticValue::Sequence;
        if (QStringList{"std::map","std::unordered_map","QMap","QHash"}.contains(base)) return SemanticValue::Associative;
        if (QStringList{"std::string","std::basic_string","QString","QByteArray"}.contains(base)) return SemanticValue::String;
        if (base == "std::optional") return SemanticValue::Optional;
        if (base == "std::variant") return SemanticValue::Variant;
        if (QStringList{"std::unique_ptr","std::shared_ptr","std::weak_ptr","QSharedPointer"}.contains(base)) return SemanticValue::SmartPointer;
        return SemanticValue::Object;
    }
};
class TypeVisualizerRegistry {
public:
    TypeVisualizerRegistry() { add(std::make_unique<StandardTypeVisualizer>()); }
    void add(std::unique_ptr<TypeVisualizer> provider) { if (provider) providers.insert(providers.begin(), std::move(provider)); }
    SemanticValue visualize(const QString& type, const QString& summary,
                            const QVector<SemanticChild>& children, const QString& hint = {}) const {
        for (const auto& provider : providers) if (provider->supports(type)) return provider->visualize(type,summary,children,hint);
        return StandardTypeVisualizer().visualize(type,summary,children,hint);
    }
private:
    std::vector<std::unique_ptr<TypeVisualizer>> providers;
};
