#ifndef AWB_SHELL_SHELLCONTROLLER_H
#define AWB_SHELL_SHELLCONTROLLER_H

#include <QObject>
#include <QString>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::shell {

/// 窗口级状态——每个值都经 core::Settings 持久化进 settings.json：侧栏
/// 折叠与宽度、窗口尺寸、上次停留的页面。重启即恢复上一会话的形状。
class ShellController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(bool sidebarCollapsed READ sidebarCollapsed WRITE
                   setSidebarCollapsed NOTIFY sidebarCollapsedChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth NOTIFY sidebarWidthChanged)
    // 侧栏宽度的钳制边界（拖拽手柄的极限值）
    Q_PROPERTY(int sidebarMinWidth READ sidebarMinWidth CONSTANT)
    Q_PROPERTY(int sidebarMaxWidth READ sidebarMaxWidth CONSTANT)
    Q_PROPERTY(int windowWidth READ windowWidth NOTIFY windowSizeChanged)
    Q_PROPERTY(int windowHeight READ windowHeight NOTIFY windowSizeChanged)
    Q_PROPERTY(QString webSurface READ webSurface NOTIFY webSurfaceChanged)
    Q_PROPERTY(QString webChromiumFlags READ webChromiumFlags NOTIFY
                   webChromiumFlagsChanged)
    // 托盘与单实例开关（launcher.* 一节）。设置页只经这三个属性读写，
    // 真正用它们的是 shell::TrayController 与 main.cpp 的实例守卫。
    Q_PROPERTY(bool trayEnabled READ trayEnabled WRITE setTrayEnabled NOTIFY
                   trayEnabledChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY
                   closeToTrayChanged)
    Q_PROPERTY(bool singleInstance READ singleInstance WRITE setSingleInstance
                   NOTIFY singleInstanceChanged)

public:
    explicit ShellController(core::Settings *settings,
                             QObject *parent = nullptr);

    // settings.json 的 window.title；空串 = 应用默认
    QString windowTitle() const;

    // 侧栏折叠状态 / 侧栏宽度；setter 写设置并立即持久化。宽度经
    // setSidebarWidth 钳制到 [sidebarMinWidth, sidebarMaxWidth]
    bool sidebarCollapsed() const;
    void setSidebarCollapsed(bool collapsed);
    int sidebarWidth() const;
    Q_INVOKABLE void setSidebarWidth(int width);

    // 侧栏宽度的钳制边界（180 / 480）
    int sidebarMinWidth() const { return kSidebarMinWidth; }
    int sidebarMaxWidth() const { return kSidebarMaxWidth; }
    /// 钳制边界：读设置时不强制（0 = 回退主题令牌），只约束用户写入
    static constexpr int kSidebarMinWidth = 180;
    static constexpr int kSidebarMaxWidth = 480;

    // 窗口尺寸（MainWindow 关闭时经 saveWindowSize() 存回）
    int windowWidth() const;
    int windowHeight() const;

    // 持久化当前窗口几何（MainWindow 关闭时调用）
    Q_INVOKABLE void saveWindowSize(int width, int height);

    // 上次停留的页面：启动时恢复，每次切页都保存
    QString lastPageId() const;
    void setLastPageId(const QString &id);

    // Web 选项：展示面策略 + Chromium flags
    // Q_INVOKABLE——与 NavigationModel::setCurrentPageId 同因：这些没有
    // WRITE 访问器，QML 无法把它们当方法调用（设置页的展示面/flags 编辑
    // 曾因此静默失效）
    QString webSurface() const;
    Q_INVOKABLE void setWebSurface(const QString &surface);
    QString webChromiumFlags() const;
    Q_INVOKABLE void setWebChromiumFlags(const QString &flags);

    // 托盘三开关：图标显示、点 × 收进托盘、单实例。都即时写盘；其中
    // singleInstance 只在启动时被读一次，改动要到下次启动才生效
    bool trayEnabled() const;
    void setTrayEnabled(bool on);
    bool closeToTray() const;
    void setCloseToTray(bool on);
    bool singleInstance() const;
    void setSingleInstance(bool on);

Q_SIGNALS:
    /**
     * @brief window.title 变化时发射（含外部改写 settings.json 的情形）
     */
    void windowTitleChanged();

    /**
     * @brief 侧栏折叠状态变化时发射
     */
    void sidebarCollapsedChanged();

    /**
     * @brief 侧栏宽度变化时发射
     */
    void sidebarWidthChanged();

    /**
     * @brief 窗口宽度或高度变化时发射（两值共用一个信号）
     */
    void windowSizeChanged();

    /**
     * @brief Web 展示面（embedded | external）变化时发射
     */
    void webSurfaceChanged();

    /**
     * @brief Chromium 命令行开关变化时发射
     */
    void webChromiumFlagsChanged();

    /**
     * @brief 托盘图标开关（launcher.trayEnabled）变化时发射
     */
    void trayEnabledChanged();

    /**
     * @brief 「点 × 收进托盘」开关（launcher.closeToTray）变化时发射
     */
    void closeToTrayChanged();

    /**
     * @brief 单实例开关（launcher.singleInstance）变化时发射
     *
     * @note 值本身要到下一次启动才生效：实例守卫在界面装配之前就跑完了
     */
    void singleInstanceChanged();

private:
    core::Settings *m_settings;  ///< 唯一的持久化后端，不持有所有权
};

} // namespace awb::shell

#endif // AWB_SHELL_SHELLCONTROLLER_H
