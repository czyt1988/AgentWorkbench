#include "workbench/WorkbenchContext.h"

#include "agents/AgentModel.h"
#include "agents/AgentUrls.h"
#include "core/Settings.h"
#include "agents/AgentsFacade.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/UiServices.h"
#include "web/WebTabsFacade.h"

#include <QCoreApplication>

namespace awb::workbench {

WorkbenchContext::WorkbenchContext(shell::NavigationModel *nav,
                                   shell::UiServices *ui,
                                   shell::Notifications *notifications,
                                   agents::AgentsFacade *agents,
                                   web::WebTabsFacade *web,
                                   core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_nav(nav)
    , m_ui(ui)
    , m_notifications(notifications)
    , m_agents(agents)
    , m_web(web)
    , m_settings(settings)
{
    connect(m_nav, &shell::NavigationModel::currentPageChanged, this,
            &WorkbenchContext::currentPageChanged);
}

QString WorkbenchContext::currentPageId() const
{
    return m_nav->currentPageId();
}

void WorkbenchContext::showPage(const QString &id)
{
    m_nav->setCurrentPageId(id);
}

void WorkbenchContext::openWeb(const QString &agentId)
{
    const int row = m_agents->agentModel()->indexOf(agentId);
    if (row < 0)
        return;
    const awb::agents::AgentDefinition def =
        m_agents->agentModel()->definitions().at(row);
    if (def.webUrl.isEmpty())
        return;

    QVariantMap fields;
    fields[QStringLiteral("agentId")] = def.id;
    // The final URL, not the bare webUrl: a configured tokenFile becomes a
    // #token=<value> fragment that the web UI needs for mutation routes
    // (01 §4.7: "取 AgentUrls 的最终 URL").
    fields[QStringLiteral("url")] = agents::AgentUrls::finalUrl(def);
    fields[QStringLiteral("title")] = def.name;
    fields[QStringLiteral("icon")] = def.icon;
    fields[QStringLiteral("color")] = def.color;
    // Same-agent dedup, embedded/external policy and the external toast all
    // live in the web domain (01 §4.5).
    m_web->openTab(fields);
}

void WorkbenchContext::closeWeb(const QString &agentId)
{
    const QVariantMap tab = m_web->tabForAgent(agentId);
    const QString id = tab.value(QStringLiteral("id")).toString();
    if (!id.isEmpty())
        m_web->closeTab(id);
}

void WorkbenchContext::reloadWeb(const QString &agentId)
{
    const QVariantMap tab = m_web->tabForAgent(agentId);
    const QString id = tab.value(QStringLiteral("id")).toString();
    if (!id.isEmpty())
        m_web->reloadTab(id);
}

void WorkbenchContext::launchAgent(const QString &agentId)
{
    m_agents->launch(agentId);
}

void WorkbenchContext::copyText(const QString &text)
{
    const core::OpResult result = m_ui->copyText(text);
    if (result.ok) {
        m_notifications->notify(QStringLiteral("success"),
                                QCoreApplication::translate("WorkbenchContext",
                                                            "Copied"),
                                text);
    } else {
        m_notifications->notify(QStringLiteral("error"),
                                QCoreApplication::translate("WorkbenchContext",
                                                            "Copy failed"),
                                result.error);
    }
}

void WorkbenchContext::notify(const QString &level, const QString &title,
                              const QString &text)
{
    m_notifications->notify(level, title, text);
}

void WorkbenchContext::openExternalUrl(const QUrl &url)
{
    const core::OpResult result = m_ui->openExternalUrl(url);
    if (!result.ok) {
        m_notifications->notify(QStringLiteral("error"),
                                QCoreApplication::translate(
                                    "WorkbenchContext", "Cannot open link"),
                                result.error);
    }
}

void WorkbenchContext::openFolder(const QString &path)
{
    const core::OpResult result = m_ui->openFolder(path);
    if (!result.ok) {
        m_notifications->notify(QStringLiteral("error"),
                                QCoreApplication::translate(
                                    "WorkbenchContext", "Cannot open folder"),
                                result.error);
    }
}

void WorkbenchContext::openConfigDir(const QString &agentId)
{
    m_agents->openConfigDir(agentId);
}

QVariantList WorkbenchContext::pluginList() const
{
    return m_discoveredPlugins;
}

void WorkbenchContext::setDiscoveredPlugins(const QVariantList &plugins)
{
    m_discoveredPlugins = plugins;
}

void WorkbenchContext::setPluginEnabled(const QString &id, bool enabled)
{
    QStringList disabled = m_settings->pluginsOptions().disabledIds;
    if (enabled)
        disabled.removeAll(id);
    else if (!disabled.contains(id))
        disabled.append(id);

    // Persist through the settings object (typed access stays in core).
    m_settings->setPluginsDisabledIds(disabled);
    m_settings->save();

    // Keep the snapshot handed to the settings page consistent.
    for (QVariant &entry : m_discoveredPlugins) {
        QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            map[QStringLiteral("enabled")] = enabled;
            entry = map;
        }
    }
}

bool WorkbenchContext::pluginsEnabled() const
{
    return m_settings->pluginsOptions().enabled;
}

void WorkbenchContext::setPluginsEnabled(bool enabled)
{
    if (m_settings->pluginsOptions().enabled == enabled)
        return;
    m_settings->setPluginsGloballyEnabled(enabled);
    m_settings->save();
}

QString WorkbenchContext::pluginTrustNotice() const
{
    return tr("Plugins run inside this application's process. Their trust "
              "level is the same as the application itself — only enable "
              "plugins you trust. Changes take effect after a restart.");
}

void WorkbenchContext::quit()
{
    QCoreApplication::quit();
}

} // namespace awb::workbench
