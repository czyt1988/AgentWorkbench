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
// plugin::Services level -> toast level (0 = info, 1 = warning, 2 = error).
QString levelName(int level)
{
    if (level == 1)
        return QStringLiteral("warning");
    if (level == 2)
        return QStringLiteral("error");
    return QStringLiteral("info");
}
} // namespace

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

void PluginServices::registerPage(const plugin::PageDescriptor &page)
{
    // The SAME registration path as the built-in pages (specs/01 §9.1) —
    // duplicate ids are rejected inside NavigationModel.
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

void PluginServices::unregisterPage(const QString &id)
{
    m_nav->unregisterPage(id);
}

void PluginServices::addWebSurface(const QString &kind,
                                   const QString &componentUrl)
{
    m_web->registerSurface(kind, componentUrl);
}

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

void PluginServices::log(int level, const QString &message)
{
    const QString prefix = QStringLiteral("[plugin] ");
    if (level == 2)
        qWarning().noquote() << prefix + message;
    else if (level == 1)
        qWarning().noquote() << prefix + message;
    else
        qInfo().noquote() << prefix + message;
}

void PluginServices::notify(int level, const QString &title,
                            const QString &text)
{
    m_notifications->notify(levelName(level), title, text);
}

QString PluginServices::themeColor(const QString &token)
{
    const QColor color = m_theme->color(token);
    if (!color.isValid())
        return {};
    return color.name(QColor::HexRgb);
}

QString PluginServices::settingsValue(const QString &key)
{
    // Read-only, a small known-key surface (specs/01 §9.1: plugins never
    // write settings or agents.json).
    if (key == QLatin1String("appearance.theme"))
        return m_settings->themeId();
    if (key == QLatin1String("window.title"))
        return m_settings->windowTitle();
    if (key == QLatin1String("web.surface"))
        return m_settings->webOptions().surface;
    if (key == QLatin1String("locale.override"))
        return m_settings->locale().overrideName;
    qWarning().noquote() << QStringLiteral(
        "[plugin] settings key \"%1\" is not exposed; returning empty").arg(key);
    return {};
}

} // namespace awb::workbench
