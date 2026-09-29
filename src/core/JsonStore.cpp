#include "core/JsonStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace awb::core {

/**
 * @brief 读取 JSON 对象文件
 *
 * 文件缺失不告警（首次启动的正常路径）；打不开或解析失败记一条
 * qWarning 并返回空对象——读取方拿到空对象后走默认值即可。
 *
 * @param path JSON 文件路径
 * @return 解析出的对象；缺失/不可读/格式坏时为空对象
 */
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

/**
 * @brief 以缩进 JSON 原子写入对象
 *
 * @param path 目标文件路径
 * @param object 待写入的对象
 * @return 写入结果；父目录缺失会自动创建
 * @sa writeBytes
 */
OpResult JsonStore::writeFile(const QString &path, const QJsonObject &object)
{
    return writeBytes(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

/**
 * @brief 原子写入原始字节
 *
 * QSaveFile 先写临时文件再提交，进程中途死亡不会留下半截文件。
 * 用于必须与随包原始文件逐字节一致的场合（如内置 agent 定义）。
 *
 * @param path 目标文件路径
 * @param bytes 原始内容
 * @return 打不开、写短、提交失败时为带英文原因的失败结果
 * @sa writeFile
 */
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
