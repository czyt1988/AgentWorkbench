#ifndef AWB_SHELL_UISERVICES_H
#define AWB_SHELL_UISERVICES_H

#include "core/OpResult.h"

#include <QObject>
#include <QStringList>
#include <QUrl>

namespace awb::shell {

/// 页面需要但不能自己实现的通用 UI 操作：剪贴板、外部 URL、文件管理器、
/// 原生目录/颜色选择对话框，以及通用颜色选择器（AColorPicker）的支撑
/// 数据——主题色/标准色色板与最近自定义颜色的进程内记忆。
///
/// QML 只与门面对话，不直接碰 Qt 的全局服务。
class UiServices : public QObject
{
    Q_OBJECT

    // 最近用过的自定义颜色（#rrggbb 串，最新在前；进程内记忆不落盘）
    Q_PROPERTY(QStringList recentColors READ recentColors NOTIFY recentColorsChanged)
    // 颜色选择器的默认主题色（10 列，每列随后是其 5 档深浅）
    Q_PROPERTY(QStringList colorThemes READ colorThemes CONSTANT)
    // 颜色选择器的固定标准色（10 色）
    Q_PROPERTY(QStringList standardColors READ standardColors CONSTANT)

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

    // 弹系统「选颜色」对话框（Windows 上是 comdlg32 的原生颜色框）。
    // 返回所选颜色的 #rrggbb 串；取消或对话框不可用时返回空串
    Q_INVOKABLE QString pickColor(const QString &initialColor);

    // 把一个自定义颜色记进最近记忆（进程内，见 recentColors）
    Q_INVOKABLE void rememberColor(const QString &color);

    // 最近用过的自定义颜色（#rrggbb 串，最新在前）
    QStringList recentColors() const;

    // 颜色选择器（AColorPicker）的支撑数据：默认主题色与固定标准色
    QStringList colorThemes() const;
    QStringList standardColors() const;

Q_SIGNALS:
    /**
     * @brief 最近自定义颜色记忆变化后发射
     *
     * recentColors 属性的 NOTIFY：AColorPicker 的「最近」行据它重算绑定。
     */
    void recentColorsChanged();

private:
    // 最近自定义颜色（#rrggbb 串，最新在前；进程内记忆，不落盘）
    QStringList m_recentColors;
};

} // namespace awb::shell

#endif // AWB_SHELL_UISERVICES_H
