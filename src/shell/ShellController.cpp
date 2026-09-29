#include "shell/ShellController.h"

#include "core/Settings.h"

namespace awb::shell {

// ShellController 把窗口级状态投影成 QML 可绑定的属性，值唯一存在
// core::Settings 里，这里不做本地缓存——setter 先写设置再立即 save()，
// 外部改写 settings.json 的情形经构造函数里的 valueChanged 连接联动回来。

/**
 * @brief 构造窗口控制器
 *
 * @param settings 持久化后端（调用方保证非空且存活期覆盖本对象）
 * @param parent   QObject 父项
 */
ShellController::ShellController(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // settings.json 的外部改写（含本进程其它入口）也要让窗口保持同步
    connect(m_settings, &core::Settings::valueChanged, this,
            [this](const QString &key) {
                if (key == QStringLiteral("window.sidebarCollapsed")) {
                    Q_EMIT sidebarCollapsedChanged();
                }
                else if (key == QStringLiteral("window.sidebarWidth")) {
                    Q_EMIT sidebarWidthChanged();
                }
                else if (key == QStringLiteral("window.width")
                         || key == QStringLiteral("window.height")) {
                    Q_EMIT windowSizeChanged();
                }
                else if (key == QStringLiteral("window.title")) {
                    Q_EMIT windowTitleChanged();
                }
                else if (key == QStringLiteral("web.surface")) {
                    Q_EMIT webSurfaceChanged();
                }
                else if (key == QStringLiteral("web.chromiumFlags")) {
                    Q_EMIT webChromiumFlagsChanged();
                }
            });
}

/**
 * @brief 取窗口标题
 *
 * @return settings.json 的 window.title；空串表示应用默认
 */
QString ShellController::windowTitle() const
{
    return m_settings->windowTitle();
}

/**
 * @brief 取侧栏折叠状态
 *
 * @return true = 折叠
 */
bool ShellController::sidebarCollapsed() const
{
    return m_settings->window().sidebarCollapsed;
}

/**
 * @brief 写侧栏折叠状态并持久化
 *
 * 同值直接返回（不发信号、不写盘）。
 *
 * @param collapsed true = 折叠
 */
void ShellController::setSidebarCollapsed(bool collapsed)
{
    if (m_settings->window().sidebarCollapsed == collapsed) {
        return;
    }
    m_settings->setSidebarCollapsed(collapsed);
    m_settings->save();
    Q_EMIT sidebarCollapsedChanged();
}

/**
 * @brief 取侧栏宽度
 *
 * @return 像素值
 */
int ShellController::sidebarWidth() const
{
    return m_settings->window().sidebarWidth;
}

/**
 * @brief 取窗口宽度
 *
 * @return 像素值
 */
int ShellController::windowWidth() const
{
    return m_settings->window().width;
}

/**
 * @brief 取窗口高度
 *
 * @return 像素值
 */
int ShellController::windowHeight() const
{
    return m_settings->window().height;
}

/**
 * @brief 持久化当前窗口几何
 *
 * MainWindow 关闭时调用。同值直接返回。变化经 Settings 的
 * valueChanged 已经会发 windowSizeChanged，这里再补发一道覆盖
 * save() 静默失败之外的全部正常路径。
 *
 * @param width  新宽度（像素）
 * @param height 新高度（像素）
 */
void ShellController::saveWindowSize(int width, int height)
{
    if (m_settings->window().width == width
        && m_settings->window().height == height) {
        return;
    }
    m_settings->setWindowSize(width, height);
    m_settings->save();
    Q_EMIT windowSizeChanged();
}

/**
 * @brief 取 Web 展示面
 *
 * @return embedded | external
 */
QString ShellController::webSurface() const
{
    return m_settings->webOptions().surface;
}

/**
 * @brief 写 Web 展示面并持久化
 *
 * 同值直接返回。
 *
 * @param surface embedded | external
 */
void ShellController::setWebSurface(const QString &surface)
{
    if (m_settings->webOptions().surface == surface) {
        return;
    }
    m_settings->setWebSurface(surface);
    m_settings->save();
    Q_EMIT webSurfaceChanged();
}

/**
 * @brief 取 Chromium 命令行开关
 *
 * @return 空格分隔的 flags；空串 = 无
 */
QString ShellController::webChromiumFlags() const
{
    return m_settings->webOptions().chromiumFlags;
}

/**
 * @brief 写 Chromium 命令行开关并持久化
 *
 * 同值直接返回。
 *
 * @param flags 空格分隔的 Chromium flags
 */
void ShellController::setWebChromiumFlags(const QString &flags)
{
    if (m_settings->webOptions().chromiumFlags == flags) {
        return;
    }
    m_settings->setWebChromiumFlags(flags);
    m_settings->save();
    Q_EMIT webChromiumFlagsChanged();
}

/**
 * @brief 取上次停留的页面 id
 *
 * @return 页面 id；空串表示无记录
 */
QString ShellController::lastPageId() const
{
    return m_settings->window().lastPageId;
}

/**
 * @brief 写上次停留的页面并持久化
 *
 * 不发信号——lastPageId 只供启动恢复，QML 侧的当前页状态归
 * NavigationModel 管。
 *
 * @param id 页面 id
 */
void ShellController::setLastPageId(const QString &id)
{
    if (m_settings->window().lastPageId == id) {
        return;
    }
    m_settings->setLastPageId(id);
    m_settings->save();
}

} // namespace awb::shell
