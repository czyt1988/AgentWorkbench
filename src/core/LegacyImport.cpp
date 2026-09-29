#include "core/LegacyImport.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace awb::core {

namespace {

/**
 * @brief 复制单个文件（仅在目标不存在时）
 *
 * @param src 源文件路径
 * @param dst 目标文件路径，父目录缺失时自动创建
 * @return 实际写入了文件时返回 true；源缺失或目标已存在时返回 false
 */
bool copyFileIfAbsent(const QString &src, const QString &dst)
{
    if (!QFile::exists(src) || QFile::exists(dst)) {
        return false;
    }
    QDir().mkpath(QFileInfo(dst).absolutePath());
    return QFile::copy(src, dst);
}

/**
 * @brief 判断数据根是否还是「未动过」的状态
 *
 * 只剩 log 目录的数据根视为未动过：Logging::install() 比一切先跑，
 * 升级后首次启动时目录里唯一的条目就是 log/。出现其它任何条目都说明
 * 新版本已经往这里写过用户数据，导入不得覆盖。
 *
 * @param root 数据根路径
 * @return 目录不存在或只含 log/ 时返回 true
 */
bool isUntouched(const QString &root)
{
    const QDir dir(root);
    if (!dir.exists()) {
        return true;
    }

    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &fi : entries) {
        if (fi.isDir() && fi.fileName() == QStringLiteral("log")) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

/**
 * @brief 从默认旧目录收编一次数据
 *
 * 旧目录固定为 home 下的 ~/.AgentLauncher，实际逻辑在 importOnce()。
 *
 * @param newRoot 应用数据根
 * @param outNotice 可选；发生导入时收到一条用户可读的一次性提示
 * @return 发生了导入时返回 true
 * @sa importOnce
 */
bool LegacyImport::runOnce(const QString &newRoot, QString *outNotice)
{
    const QString oldRoot =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + QStringLiteral("/.AgentLauncher");
    return importOnce(newRoot, oldRoot, outNotice);
}

/**
 * @brief 把旧目录内容收编进新数据根（只复制，不动旧目录）
 *
 * 新根不是「未动过」状态、或旧目录不存在时直接返回 false，不报错——
 * 每次启动调用都安全。收编范围：agents.json、agent_state.json 与旧
 * log/ 里的全部文件（历史诊断仍可达，新日志写在旁边）。
 *
 * @param newRoot 应用数据根
 * @param oldRoot 旧版 AgentLauncher 的数据目录
 * @param outNotice 可选；发生导入时收到一条英文 tr() 源串提示
 * @return 复制了至少一个文件时返回 true
 * @sa runOnce
 */
bool LegacyImport::importOnce(const QString &newRoot, const QString &oldRoot,
                              QString *outNotice)
{
    if (outNotice) {
        outNotice->clear();
    }

    if (!isUntouched(newRoot)) {
        return false;
    }
    if (!QDir(oldRoot).exists()) {
        return false;
    }

    int copied = 0;
    const QStringList dataFiles = {QStringLiteral("agents.json"),
                                   QStringLiteral("agent_state.json")};
    for (const QString &name : dataFiles) {
        if (copyFileIfAbsent(oldRoot + QLatin1Char('/') + name,
                             newRoot + QLatin1Char('/') + name)) {
            ++copied;
        }
    }

    // 旧 log 目录一并带走，历史诊断仍可达；新版本的
    // agentworkbench.log 与它们同目录。
    const QDir oldLogDir(oldRoot + QStringLiteral("/log"));
    if (oldLogDir.exists()) {
        const QFileInfoList files = oldLogDir.entryInfoList(
            QDir::Files | QDir::Hidden | QDir::System);
        for (const QFileInfo &fi : files) {
            if (copyFileIfAbsent(fi.absoluteFilePath(),
                                 newRoot + QStringLiteral("/log/")
                                     + fi.fileName())) {
                ++copied;
            }
        }
    }

    if (copied == 0) {
        return false;
    }

    // 旧目录本身从不被修改或删除。
    qInfo().noquote() << QStringLiteral(
                             "LegacyImport: adopted %1 file(s) from the legacy "
                             "AgentLauncher directory %2 into %3")
                             .arg(copied)
                             .arg(oldRoot, newRoot);
    if (outNotice) {
        *outNotice = QCoreApplication::translate(
            "LegacyImport",
            "Imported configuration from the previous AgentLauncher "
            "installation.");
    }
    return true;
}

} // namespace awb::core
