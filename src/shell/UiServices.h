#ifndef AWB_SHELL_UISERVICES_H
#define AWB_SHELL_UISERVICES_H

#include "core/OpResult.h"

#include <QObject>
#include <QUrl>

namespace awb::shell {

/// 页面需要但不能自己实现的通用 UI 操作：剪贴板、外部 URL、文件管理器。
///
/// QML 只与门面对话，不直接碰 Qt 的全局服务。
class UiServices : public QObject
{
    Q_OBJECT

public:
    explicit UiServices(QObject *parent = nullptr);

    // 返回类型必须写全限定名：Qt 5 的 moc 按头文件书写形式记录返回类型名，
    // QML 调用端按 QMetaType 注册名（即类全名 awb::core::OpResult）解析；
    // 短名解析不到注册表就抛 "Unknown method return type"，调用静默失效。
    // 复制文本到系统剪贴板。失败（如无剪贴板服务）以 OpResult 返回，
    // 调用方可展示原因
    Q_INVOKABLE awb::core::OpResult copyText(const QString &text);

    // 用系统处理器（浏览器、文件关联）打开一个 URL
    Q_INVOKABLE awb::core::OpResult openExternalUrl(const QUrl &url);

    // 在文件管理器中定位到该文件（Windows 上是 explorer /select）
    Q_INVOKABLE awb::core::OpResult revealFile(const QString &path);

    // 在文件管理器中打开一个文件夹
    Q_INVOKABLE awb::core::OpResult openFolder(const QString &path);

    // 弹系统「选文件夹」对话框，返回所选目录；取消返回空串
    Q_INVOKABLE QString pickFolder(const QString &title);
};

} // namespace awb::shell

#endif // AWB_SHELL_UISERVICES_H
