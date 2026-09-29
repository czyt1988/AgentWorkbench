#include "workbench/PluginServices.h"

#include "core/Paths.h"
#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/UiServices.h"
#include "theme/Theme.h"
#include "web/WebTabsFacade.h"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>

namespace awb::workbench {

namespace {

/**
 * @brief plugin::Services 的级别常量 → toast 级别名
 *
 * @param level ABI 约定的级别：0 = info、1 = warning、2 = error
 * @return Notifications 接受的级别名
 */
QString levelName(int level)
{
    if (level == 1) {
        return QStringLiteral("warning");
    }
    if (level == 2) {
        return QStringLiteral("error");
    }
    return QStringLiteral("info");
}
} // namespace

/**
 * @brief 构造宿主侧服务桥
 *
 * 只把各域的指针接进来；插件在启动时经 PluginHost::loadEnabled() 拿到
 * 本对象。
 *
 * @param nav 页面注册
 * @param ui 系统能力
 * @param notifications toast 通知
 * @param theme 主题色 token
 * @param web web 表面注册
 * @param settings 设置只读面
 */
PluginServices::PluginServices(shell::NavigationModel *nav,
                               shell::UiServices *ui,
                               shell::Notifications *notifications,
                               theme::Theme *theme, web::WebTabsFacade *web,
                               core::Settings *settings)
    : m_nav(nav)
    , m_ui(ui)
    , m_notifications(notifications)
    , m_theme(theme)
    , m_web(web)
    , m_settings(settings)
{
}

/**
 * @brief 把一个插件页面注册进导航
 *
 * 与内置页面同一条 registerPage 路径（重复 id 由 NavigationModel 拒绝）。
 *
 * @param page 插件声明的页面描述符
 */
void PluginServices::registerPage(const plugin::PageDescriptor &page)
{
    shell::PageDescriptor shell;
    shell.id = page.id;
    shell.title = page.title;
    shell.iconSource = page.icon;
    shell.source = page.source;
    shell.section = page.section.isEmpty() ? QStringLiteral("extensions")
                                           : page.section;
    shell.order = page.order;
    m_nav->registerPage(shell);
}

/**
 * @brief 按 id 注销一个页面
 *
 * @param id 页面 id
 */
void PluginServices::unregisterPage(const QString &id)
{
    m_nav->unregisterPage(id);
}

/**
 * @brief 注册一种 web 表面
 *
 * @param kind 表面类型（如 "embedded"）
 * @param componentUrl 承载该表面的 QML 组件 URL
 */
void PluginServices::addWebSurface(const QString &kind,
                                   const QString &componentUrl)
{
    m_web->registerSurface(kind, componentUrl);
}

/**
 * @brief 取插件的数据目录
 *
 * pluginId 里的非法路径字符替换成 _（插件 id 来自 manifest，不能直接
 * 拼进路径）；目录不存在则创建。
 *
 * @param pluginId 插件 id
 * @return <dataRoot>/plugins/<安全化 id>/data 的绝对路径
 */
QString PluginServices::dataDir(const QString &pluginId)
{
    QString safe = pluginId;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                 QStringLiteral("_"));
    const QString dir = QStringLiteral("%1/%2/data")
                            .arg(core::Paths::pluginsDir(), safe);
    QDir().mkpath(dir);
    return dir;
}

/**
 * @brief 打一条带 [plugin] 前缀的日志
 *
 * level 2（error）与 1（warning）走 qWarning，其余走 qInfo。
 *
 * @param level ABI 级别：0 = info、1 = warning、2 = error
 * @param message 日志文本
 */
void PluginServices::log(int level, const QString &message)
{
    const QString prefix = QStringLiteral("[plugin] ");
    if (level == 2) {
        qWarning().noquote() << prefix + message;
    }
    else if (level == 1) {
        qWarning().noquote() << prefix + message;
    }
    else {
        qInfo().noquote() << prefix + message;
    }
}

/**
 * @brief 发一条 toast
 *
 * @param level ABI 级别（0/1/2 → info/warning/error）
 * @param title 标题
 * @param text 正文
 */
void PluginServices::notify(int level, const QString &title,
                            const QString &text)
{
    m_notifications->notify(levelName(level), title, text);
}

/**
 * @brief 按 token 取主题色
 *
 * 插件拿到的总是 "#rrggbb" 字符串，不接触 QColor 对象（ABI 边界只过
 * 基本类型）。
 *
 * @param token 主题色 token 名
 * @return 十六进制颜色串；token 不存在时返回空串
 */
QString PluginServices::themeColor(const QString &token)
{
    const QColor color = m_theme->color(token);
    if (!color.isValid()) {
        return {};
    }
    return color.name(QColor::HexRgb);
}

/**
 * @brief 读一个暴露给插件的设置键
 *
 * 只读、小范围白名单——插件永远不写设置，也不碰 agents.json。
 *
 * @param key 白名单键（appearance.theme / window.title / web.surface /
 *            locale.override）
 * @return 键值；键不在白名单时记警告并返回空串
 */
QString PluginServices::settingsValue(const QString &key)
{
    if (key == QStringLiteral("appearance.theme")) {
        return m_settings->themeId();
    }
    if (key == QStringLiteral("window.title")) {
        return m_settings->windowTitle();
    }
    if (key == QStringLiteral("web.surface")) {
        return m_settings->webOptions().surface;
    }
    if (key == QStringLiteral("locale.override")) {
        return m_settings->locale().overrideName;
    }
    qWarning().noquote() << QStringLiteral(
        "[plugin] settings key \"%1\" is not exposed; returning empty").arg(key);
    return {};
}

} // namespace awb::workbench
