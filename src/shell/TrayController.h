#ifndef AWB_SHELL_TRAYCONTROLLER_H
#define AWB_SHELL_TRAYCONTROLLER_H

#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QWindow;

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::shell {

/// 系统托盘图标：把主窗口收进托盘、再从托盘唤回。菜单只有「显示 /
/// 隐藏」和「退出」两项。
///
/// 只认 Qt Gui / Widgets 与 core::Settings，不认识 agent、web 等域；
/// 「退出」也不在这里执行——它只发 quitRequested，退出确认的语义留在
/// MainWindow.qml，托盘不会绕过确认把在跑的 agent 丢掉。
class TrayController : public QObject
{
    Q_OBJECT

    // 托盘此刻是否真的可用：系统有托盘 + launcher.trayEnabled + 图标就绪
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // 窗口是否正被收在托盘里
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)

public:
    explicit TrayController(core::Settings *settings,
                            QObject *parent = nullptr);
    ~TrayController() override;

    // 主窗口。不持有所有权；未设置时 hideToTray / showWindow 什么都不做
    void setWindow(QWindow *window);

    // 托盘可用 / 窗口已收起（setter 无：两者都由 hideToTray、showWindow
    // 与 launcher.trayEnabled 驱动）
    bool available() const;
    bool hidden() const;

    // 收进托盘 / 唤回并聚焦。同一侧重复调用是幂等的，不会重复发信号，
    // 首次收起的气泡提示也只发一次
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void showWindow();

    // 点窗口 × 时该不该收进托盘：托盘可用、关闭选择已记住且
    // launcher.closeToTray 打开。用 Q_INVOKABLE 而不是属性——QML 在 onClosing
    // 里即时问一次，不需要绑定，也就没有属性缓存过期的问题
    Q_INVOKABLE bool canHideToTray() const;

Q_SIGNALS:
    /**
     * @brief 托盘可用性变化时发射（开关切换、图标创建成功或失败）
     */
    void availableChanged();

    /**
     * @brief 窗口在「显示」与「收在托盘里」之间切换时发射（边沿触发，
     *        状态没变不发）
     */
    void hiddenChanged();

    /**
     * @brief 托盘菜单点「退出」时发射
     *
     * 订阅方（MainWindow.qml）负责跑既有的退出确认流程并显式结束事件
     * 循环。托盘自己不调 quit()：那样会跳过「后台终端还在跑」的询问。
     */
    void quitRequested();

private:
    // 按 launcher.trayEnabled 与系统托盘是否存在，建/销毁并显示/隐藏图标
    void syncIcon();
    // 菜单首项的文字随收起状态切换
    void refreshToggleText();
    // 写入隐藏状态；只有真的变化时才发 hiddenChanged
    void applyHidden(bool hidden);

    core::Settings *m_settings;  ///< 持久化后端，不持有所有权
    QWindow *m_window = nullptr; ///< 主窗口，不持有所有权
    QSystemTrayIcon *m_icon = nullptr; ///< 托盘图标；空 = 系统不兼容或无图标
    QMenu *m_menu = nullptr;     ///< 右键菜单，父项是 this
    QAction *m_toggleAction = nullptr; ///< 菜单首项「显示 / 隐藏」
    bool m_available = false;    ///< available() 的缓存值
    bool m_hidden = false;       ///< hidden() 的缓存值
    bool m_noticeShown = false;  ///< 首次收起的气泡提示是否已发过
};

} // namespace awb::shell

#endif // AWB_SHELL_TRAYCONTROLLER_H
