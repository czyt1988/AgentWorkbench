#ifndef AWB_WORKBENCH_WORKBENCHCONTEXT_H
#define AWB_WORKBENCH_WORKBENCHCONTEXT_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

namespace awb::core {
class Settings;
} // namespace awb::core
namespace awb::agentcatalog {
class AgentsFacade;
} // namespace awb::agentcatalog
namespace awb::shell {
class NavigationModel;
class Notifications;
class UiServices;
} // namespace awb::shell
namespace awb::web {
class WebTabsFacade;
} // namespace awb::web

namespace awb::workbench {

// The QML global `workbench`: cross-domain intents and generic actions.
// It is the only place allowed to talk to more
// than one domain — e.g. openWeb needs the agent's URL and the web/UI side.
class WorkbenchContext : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentPageId READ currentPageId NOTIFY currentPageChanged)
    // One-shot legacy-import notice for the startup popup;
    // empty on every normal start.
    Q_PROPERTY(QString legacyImportNotice READ legacyImportNotice
               NOTIFY legacyImportNoticeChanged)

public:
    WorkbenchContext(shell::NavigationModel *nav, shell::UiServices *ui,
                     shell::Notifications *notifications,
                     agentcatalog::AgentsFacade *agents, web::WebTabsFacade *web,
                     core::Settings *settings, QObject *parent = nullptr);

    QString currentPageId() const;
    QString legacyImportNotice() const { return m_legacyImportNotice; }
    void setLegacyImportNotice(const QString &notice)
    {
        if (notice == m_legacyImportNotice)
            return;
        m_legacyImportNotice = notice;
        emit legacyImportNoticeChanged();
    }

    // Navigation intent.
    Q_INVOKABLE void showPage(const QString &id);

    // Cross-domain intents (S5): open the agent's WebUI as a tab — the
    // surface policy (embedded/external) lives in WebTabsFacade.
    Q_INVOKABLE void openWeb(const QString &agentId);
    Q_INVOKABLE void closeWeb(const QString &agentId);
    Q_INVOKABLE void reloadWeb(const QString &agentId);
    // Restart an agent (offline overlay, launcher cards).
    Q_INVOKABLE void launchAgent(const QString &agentId);

    // Generic actions.
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void notify(const QString &level, const QString &title,
                            const QString &text);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    Q_INVOKABLE void openFolder(const QString &path);
    Q_INVOKABLE void openConfigDir(const QString &agentId);
    Q_INVOKABLE void quit();

    // --- Plugins (experimental) -------------------------------
    // Discovered plugins for the settings list:
    // [{id,name,version,description,enabled}]. Effective on the NEXT start —
    // libraries are loaded once at boot.
    Q_INVOKABLE QVariantList pluginList() const;
    Q_INVOKABLE void setPluginEnabled(const QString &id, bool enabled);
    // Master switch in Settings -> Plugins (default off).
    Q_INVOKABLE bool pluginsEnabled() const;
    Q_INVOKABLE void setPluginsEnabled(bool enabled);
    // The trust notice the settings page must show.
    Q_INVOKABLE QString pluginTrustNotice() const;
    void setDiscoveredPlugins(const QVariantList &plugins);

signals:
    void currentPageChanged();
    void legacyImportNoticeChanged();

private:
    shell::NavigationModel *m_nav;
    shell::UiServices *m_ui;
    shell::Notifications *m_notifications;
    agentcatalog::AgentsFacade *m_agents;
    web::WebTabsFacade *m_web;
    core::Settings *m_settings;
    QString m_legacyImportNotice;
    QVariantList m_discoveredPlugins;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_WORKBENCHCONTEXT_H
