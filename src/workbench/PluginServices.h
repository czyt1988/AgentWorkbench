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

// The host-side implementation of plugin::Services (specs/01 §9.3): the
// bridge between the plugin ABI and the shell/theme/web facilities. Lives
// in the application layer because it is the only layer allowed to touch
// every module.
class PluginServices : public plugin::Services
{
public:
    PluginServices(shell::NavigationModel *nav, shell::UiServices *ui,
                   shell::Notifications *notifications, theme::Theme *theme,
                   web::WebTabsFacade *web, core::Settings *settings);

    void registerPage(const plugin::PageDescriptor &page) override;
    void unregisterPage(const QString &id) override;
    void addWebSurface(const QString &kind,
                       const QString &componentUrl) override;
    QString dataDir(const QString &pluginId) override;
    void log(int level, const QString &message) override;
    void notify(int level, const QString &title, const QString &text) override;
    QString themeColor(const QString &token) override;
    QString settingsValue(const QString &key) override;

private:
    shell::NavigationModel *m_nav;
    shell::UiServices *m_ui;
    shell::Notifications *m_notifications;
    theme::Theme *m_theme;
    web::WebTabsFacade *m_web;
    core::Settings *m_settings;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_PLUGINSERVICES_H
