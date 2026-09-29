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
    Q_PROPERTY(int windowWidth READ windowWidth NOTIFY windowSizeChanged)
    Q_PROPERTY(int windowHeight READ windowHeight NOTIFY windowSizeChanged)
    Q_PROPERTY(QString webSurface READ webSurface NOTIFY webSurfaceChanged)
    Q_PROPERTY(QString webChromiumFlags READ webChromiumFlags NOTIFY
                   webChromiumFlagsChanged)

public:
    explicit ShellController(core::Settings *settings,
                             QObject *parent = nullptr);

    // settings.json 的 window.title；空串 = 应用默认
    QString windowTitle() const;

    // 侧栏折叠状态 / 侧栏宽度；setter 写设置并立即持久化
    bool sidebarCollapsed() const;
    void setSidebarCollapsed(bool collapsed);
    int sidebarWidth() const;

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

private:
    core::Settings *m_settings;  ///< 唯一的持久化后端，不持有所有权
};

} // namespace awb::shell

#endif // AWB_SHELL_SHELLCONTROLLER_H
