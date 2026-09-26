#include "shell/UiServices.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QUrl>

namespace awb::shell {

UiServices::UiServices(QObject *parent)
    : QObject(parent)
{
}

core::OpResult UiServices::copyText(const QString &text)
{
    if (text.isEmpty())
        return core::OpResult::failure(tr("Nothing to copy."));
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return core::OpResult::failure(tr("The clipboard is not available."));
    clipboard->setText(text);
    return core::OpResult::success();
}

core::OpResult UiServices::openExternalUrl(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return core::OpResult::failure(tr("Invalid URL."));
    if (!QDesktopServices::openUrl(url))
        return core::OpResult::failure(
            tr("No application accepted %1.").arg(url.toString()));
    return core::OpResult::success();
}

core::OpResult UiServices::revealFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return core::OpResult::failure(
            tr("The path does not exist: %1").arg(path));

#ifdef Q_OS_WIN
    // explorer /select,<path> highlights the file in its folder.
    const QString native = QDir::toNativeSeparators(info.absoluteFilePath());
    qint64 pid = 0;
    if (!QProcess::startDetached(QStringLiteral("explorer"),
                                 {QStringLiteral("/select,") + native},
                                 QString(), &pid))
        return core::OpResult::failure(tr("Could not open the file manager."));
    return core::OpResult::success();
#else
    return openFolder(info.absolutePath());
#endif
}

core::OpResult UiServices::openFolder(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return core::OpResult::failure(
            tr("Not a directory: %1").arg(path));
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath())))
        return core::OpResult::failure(
            tr("Could not open the folder: %1").arg(path));
    return core::OpResult::success();
}

} // namespace awb::shell
