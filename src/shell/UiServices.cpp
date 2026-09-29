#include "shell/UiServices.h"

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QUrl>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace awb::shell {

UiServices::UiServices(QObject *parent)
    : QObject(parent)
{
}

core::OpResult UiServices::copyText(const QString &text)
{
    if (text.isEmpty()) {
        return core::OpResult::failure(tr("Nothing to copy."));
    }
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return core::OpResult::failure(tr("The clipboard is not available."));
    }
    clipboard->setText(text);
    return core::OpResult::success();
}

core::OpResult UiServices::openExternalUrl(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty()) {
        return core::OpResult::failure(tr("Invalid URL."));
    }
    if (!QDesktopServices::openUrl(url)) {
        return core::OpResult::failure(
            tr("No application accepted %1.").arg(url.toString()));
    }
    return core::OpResult::success();
}

core::OpResult UiServices::revealFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return core::OpResult::failure(
            tr("The path does not exist: %1").arg(path));
    }

#ifdef Q_OS_WIN
    // explorer /select,<path> highlights the file in its folder.
    const QString native = QDir::toNativeSeparators(info.absoluteFilePath());
    qint64 pid = 0;
    if (!QProcess::startDetached(QStringLiteral("explorer"),
                                 {QStringLiteral("/select,") + native},
                                 QString(), &pid)) {
        return core::OpResult::failure(tr("Could not open the file manager."));
    }
    return core::OpResult::success();
#else
    return openFolder(info.absolutePath());
#endif
}

core::OpResult UiServices::openFolder(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        return core::OpResult::failure(
            tr("Not a directory: %1").arg(path));
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) {
        return core::OpResult::failure(
            tr("Could not open the folder: %1").arg(path));
    }
    return core::OpResult::success();
}

QString UiServices::pickFolder(const QString &title)
{
#ifdef Q_OS_WIN
    // IFileOpenDialog + FOS_PICKFOLDERS：Vista 之后的标准目录选择框，Qt 6
    // 的 FolderDialog 在 Windows 上内部走的也是它。COM STA 单元由 QPA 在
    // GUI 线程初始化，这里直接 CoCreateInstance 即可。对话框模态挂在顶层
    // 窗口上，运行自己的消息泵，期间主事件循环暂停——与 QML FolderDialog
    // 的模态行为一致。
    IFileDialog *dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_IFileDialog,
                                  reinterpret_cast<void **>(&dialog));
    if (FAILED(hr)) {
        qWarning() << "[ui] pickFolder: CoCreateInstance failed" << hr;
        return QString();
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (!title.isEmpty()) {
        dialog->SetTitle(reinterpret_cast<const wchar_t *>(title.utf16()));
    }

    HWND parent = nullptr;
    const QWindowList windows = QGuiApplication::topLevelWindows();
    if (!windows.isEmpty()) {
        parent = reinterpret_cast<HWND>(windows.constFirst()->winId());
    }

    QString result;
    hr = dialog->Show(parent);
    if (SUCCEEDED(hr)) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))
                && path) {
                result = QString::fromWCharArray(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
#else
    // 非 Windows 暂无实现（QFileDialog 需要 QtWidgets）。返回空串按
    // 「用户取消」处理，调用方已有的空路径分支会静默返回。
    Q_UNUSED(title);
    qWarning() << "[ui] pickFolder is only implemented on Windows";
    return QString();
#endif
}

} // namespace awb::shell
