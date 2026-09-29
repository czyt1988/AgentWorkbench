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

// UiServices 把系统级 UI 操作（剪贴板、桌面打开、文件管理器、原生目录
// 选择框）收拢成一个 QML 可调用的门面。可失败的操作都返回 OpResult，
// 失败原因已经是面向用户的 tr() 源串，调用方直接展示即可。

/**
 * @brief 构造 UI 服务
 *
 * @param parent QObject 父项
 */
UiServices::UiServices(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 复制文本到系统剪贴板
 *
 * @param text 要复制的文本
 * @return 成功返回 ok 的 OpResult；文本为空或剪贴板不可用时返回失败，
 *         错误信息可直接展示给用户
 */
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

/**
 * @brief 用系统处理器打开一个 URL
 *
 * @param url 目标地址；无效或为空时直接返回失败
 * @return 成功返回 ok 的 OpResult；没有应用接受该 URL 时返回失败，
 *         错误信息内嵌 URL 原文
 */
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

/**
 * @brief 在文件管理器中定位到一个文件
 *
 * Windows 上经 explorer /select 打开父文件夹并高亮该文件；其它平台
 * 退化为打开所在文件夹。
 *
 * @param path 目标文件路径
 * @return 成功返回 ok 的 OpResult；路径不存在或无法启动文件管理器时
 *         返回失败
 * @sa openFolder
 */
core::OpResult UiServices::revealFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return core::OpResult::failure(
            tr("The path does not exist: %1").arg(path));
    }

#ifdef Q_OS_WIN
    // explorer /select,<path> 会在其所在文件夹里高亮该文件
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

/**
 * @brief 在文件管理器中打开一个文件夹
 *
 * @param path 目标目录
 * @return 成功返回 ok 的 OpResult；路径不存在、不是目录或无法打开时
 *         返回失败
 * @sa revealFile
 */
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

/**
 * @brief 弹系统「选文件夹」对话框
 *
 * 走 Win32 IFileDialog（FOS_PICKFOLDERS），同一实现在 Qt 5/Qt 6 都
 * 可用：Qt 5 没有 Controls 的 FolderDialog，Qt.labs.platform 又强依赖
 * QApplication（本项目是 QGuiApplication），原生对话框是唯一能在
 * 两个版本下行为一致的路线。
 *
 * @param title 对话框标题；空串用系统默认
 * @return 所选目录的绝对路径；取消返回空串
 */
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
