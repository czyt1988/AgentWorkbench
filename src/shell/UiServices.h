#ifndef AWB_SHELL_UISERVICES_H
#define AWB_SHELL_UISERVICES_H

#include "core/OpResult.h"

#include <QObject>
#include <QUrl>

namespace awb::shell {

// Generic UI operations the pages need but must not implement themselves.
// Clipboard, external URLs, the file manager.
// QML only talks to facades — never to Qt directly.
class UiServices : public QObject
{
    Q_OBJECT

public:
    explicit UiServices(QObject *parent = nullptr);

    // 返回类型必须写全限定名：Qt 5 的 moc 按头文件书写形式记录返回类型名，
    // QML 调用端按 QMetaType 注册名（即类全名 awb::core::OpResult）解析；
    // 短名解析不到注册表就抛 "Unknown method return type"，调用静默失效。
    // Copy to the system clipboard. Failure (e.g. no clipboard service)
    // comes back as OpResult so the caller can show the reason.
    Q_INVOKABLE awb::core::OpResult copyText(const QString &text);

    // Open a URL/path with the system handler (browser, file association).
    Q_INVOKABLE awb::core::OpResult openExternalUrl(const QUrl &url);

    // Reveal a file in the file manager (explorer /select on Windows).
    Q_INVOKABLE awb::core::OpResult revealFile(const QString &path);

    // Open a folder in the file manager.
    Q_INVOKABLE awb::core::OpResult openFolder(const QString &path);

    /// 弹系统「选文件夹」对话框，返回所选目录；取消返回空串。
    ///
    /// 走 Win32 IFileDialog（FOS_PICKFOLDERS），同一实现在 Qt 5/Qt 6 都
    /// 可用：Qt 5 没有 Controls 的 FolderDialog，Qt.labs.platform 又强依赖
    /// QApplication（本项目是 QGuiApplication），原生对话框是唯一能在
    /// 两个版本下行为一致的路线。
    Q_INVOKABLE QString pickFolder(const QString &title);
};

} // namespace awb::shell

#endif // AWB_SHELL_UISERVICES_H
