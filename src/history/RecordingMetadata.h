#pragma once

// Versioned QDDD history container metadata (".qddd-history").
//
// Deliberately small: metadata + references only. Backend-owned trace and
// checkpoint blobs stay in backend storage; this schema only points at them,
// so richer QEMU support can be connected later without a format redesign.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace qddd {
namespace history {

inline constexpr int HistoryFileVersion = 1;
inline const char *HistoryFileFormat = "qddd-history";

struct HistoryRecordingMetadata {
    int version = HistoryFileVersion;
    QString target;   // TargetDescriptor::machine() or equivalent
    QString backend;  // backend id, e.g. "gdb-stop-recording", "qemu-replay"
    QString firmwarePath;
    QString firmwareBuildId;
    QString createdIso8601;
    QStringList capabilities; // historyCapabilityNames() at record time

    QJsonObject toJson() const
    {
        QJsonObject firmware;
        firmware[QStringLiteral("path")] = firmwarePath;
        firmware[QStringLiteral("buildId")] = firmwareBuildId;
        QJsonObject root;
        root[QStringLiteral("format")] = QString::fromLatin1(HistoryFileFormat);
        root[QStringLiteral("version")] = version;
        root[QStringLiteral("target")] = target;
        root[QStringLiteral("backend")] = backend;
        root[QStringLiteral("firmware")] = firmware;
        root[QStringLiteral("created")] = createdIso8601;
        QJsonArray caps;
        for (const QString &cap : capabilities)
            caps.append(cap);
        root[QStringLiteral("capabilities")] = caps;
        return root;
    }

    static HistoryRecordingMetadata fromJson(const QJsonObject &root, bool *ok = nullptr)
    {
        HistoryRecordingMetadata meta;
        const bool valid = root.value(QStringLiteral("format")).toString()
                == QString::fromLatin1(HistoryFileFormat)
            && root.value(QStringLiteral("version")).toInt(-1) == HistoryFileVersion;
        if (ok)
            *ok = valid;
        if (!valid)
            return meta;
        meta.target = root.value(QStringLiteral("target")).toString();
        meta.backend = root.value(QStringLiteral("backend")).toString();
        const QJsonObject firmware = root.value(QStringLiteral("firmware")).toObject();
        meta.firmwarePath = firmware.value(QStringLiteral("path")).toString();
        meta.firmwareBuildId = firmware.value(QStringLiteral("buildId")).toString();
        meta.createdIso8601 = root.value(QStringLiteral("created")).toString();
        const QJsonArray caps = root.value(QStringLiteral("capabilities")).toArray();
        for (const QJsonValue &cap : caps)
            meta.capabilities << cap.toString();
        return meta;
    }

    QByteArray serialize() const
    {
        return QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    }

    static HistoryRecordingMetadata deserialize(const QByteArray &data, bool *ok = nullptr)
    {
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            if (ok)
                *ok = false;
            return {};
        }
        return fromJson(doc.object(), ok);
    }
};

} // namespace history
} // namespace qddd
