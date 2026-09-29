#ifndef AWB_WORKBENCH_PLUGINSERVICES_H
#define AWB_WORKBENCH_PLUGINSERVICES_H

#include "plugin_api/PluginApi.h"

namespace awb::core {
class Settings;
} // namespace awb::core
namespace awb::shell {
class NavigationModel;
class Notifications;
class UiServices;
} // namespace awb::shell
namespace awb::theme {
class Theme;
} // namespace awb::theme
namespace awb::web {
class WebTabsFacade;
} // namespace awb::web

namespace awb::workbench {

/// plugin::Services 的宿主侧实现：插件 ABI 与 shell/theme/web 设施之间的桥。
///
/// 放在应用层（awb_workbench）是因为只有这一层被允许同时触碰每个模块。
/// 术语与安全规则见 core::PluginHost（manifest 发现、版本校验、加载策略）。
class PluginServices : public plugin::Services
{
public:
    // 构造只接指针，无初始化动作
    PluginServices(shell::NavigationModel *nav, shell::UiServices *ui,
                   shell::Notifications *notifications, theme::Theme *theme,
                   web::WebTabsFacade *web, core::Settings *settings);

    // 把一个插件页面注册进导航（与内置页面同一条注册路径）
    void registerPage(const plugin::PageDescriptor &page) override;
    // 按 id 注销一个页面
    void unregisterPage(const QString &id) override;
    // 注册一种 web 表面（kind → 组件 URL）
    void addWebSurface(const QString &kind,
                       const QString &componentUrl) override;
    // 取插件的数据目录（<dataRoot>/plugins/<id>/data），不存在则创建
    QString dataDir(const QString &pluginId) override;
    // 打一条带 [plugin] 前缀的日志
    void log(int level, const QString &message) override;
    // 发一条 toast
    void notify(int level, const QString &title, const QString &text) override;
    // 按 token 取主题色（"#rrggbb"；未知 token 返回空串）
    QString themeColor(const QString &token) override;
    // 读一个暴露给插件的设置键（只读白名单）
    QString settingsValue(const QString &key) override;

private:
    shell::NavigationModel *m_nav;         ///< 页面注册
    shell::UiServices *m_ui;               ///< 系统能力（打开 URL/目录等）
    shell::Notifications *m_notifications; ///< toast 通知
    theme::Theme *m_theme;                 ///< 主题色 token
    web::WebTabsFacade *m_web;             ///< web 表面注册
    core::Settings *m_settings;            ///< 设置只读面
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_PLUGINSERVICES_H
