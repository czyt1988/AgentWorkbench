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

// Copy one file from src to dst, never overwriting what is already there.
// Returns true when a file was written.
bool copyFileIfAbsent(const QString &src, const QString &dst)
{
    if (!QFile::exists(src) || QFile::exists(dst))
        return false;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    return QFile::copy(src, dst);
}

// A data root holding nothing but its log directory counts as untouched:
// Logging::install() creates that directory before anything else runs
// so on the first start after an upgrade the only entry present is `log/`. Every other entry means the new version
// has already written user data here — do not import over it.
bool isUntouched(const QString &root)
{
    const QDir dir(root);
    if (!dir.exists())
        return true;

    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &fi : entries) {
        if (fi.isDir() && fi.fileName() == QStringLiteral("log"))
            continue;
        return false;
    }
    return true;
}

} // namespace

bool LegacyImport::runOnce(const QString &newRoot, QString *outNotice)
{
    const QString oldRoot =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + QStringLiteral("/.AgentLauncher");
    return importOnce(newRoot, oldRoot, outNotice);
}

bool LegacyImport::importOnce(const QString &newRoot, const QString &oldRoot,
                              QString *outNotice)
{
    if (outNotice)
        outNotice->clear();

    if (!isUntouched(newRoot))
        return false;
    if (!QDir(oldRoot).exists())
        return false;

    int copied = 0;
    const QStringList dataFiles = {QStringLiteral("agents.json"),
                                   QStringLiteral("agent_state.json")};
    for (const QString &name : dataFiles) {
        if (copyFileIfAbsent(oldRoot + QLatin1Char('/') + name,
                             newRoot + QLatin1Char('/') + name))
            ++copied;
    }

    // The old log directory comes along so past diagnostics stay reachable;
    // the new version writes agentworkbench.log alongside it.
    const QDir oldLogDir(oldRoot + QStringLiteral("/log"));
    if (oldLogDir.exists()) {
        const QFileInfoList files = oldLogDir.entryInfoList(
            QDir::Files | QDir::Hidden | QDir::System);
        for (const QFileInfo &fi : files) {
            if (copyFileIfAbsent(fi.absoluteFilePath(),
                                 newRoot + QStringLiteral("/log/")
                                     + fi.fileName()))
                ++copied;
        }
    }

    if (copied == 0)
        return false;

    // The legacy directory itself is never modified or deleted.
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
