#include "core/JsonStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace awb::core {

QJsonObject JsonStore::readFile(const QString &path)
{
    QFile file(path);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral(
            "JsonStore: cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    const QByteArray data = file.readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qWarning().noquote() << QStringLiteral(
            "JsonStore: %1 is not a valid JSON object; ignoring it").arg(path);
        return {};
    }
    return doc.object();
}

OpResult JsonStore::writeFile(const QString &path, const QJsonObject &object)
{
    return writeBytes(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

OpResult JsonStore::writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return OpResult::failure(QStringLiteral("cannot open %1 for writing: %2")
                                     .arg(path, file.errorString()));
    }
    if (file.write(bytes) != bytes.size()) {
        return OpResult::failure(QStringLiteral("short write to %1: %2")
                                     .arg(path, file.errorString()));
    }
    if (!file.commit()) {
        return OpResult::failure(QStringLiteral("cannot commit %1: %2")
                                     .arg(path, file.errorString()));
    }
    return OpResult::success();
}

} // namespace awb::core
