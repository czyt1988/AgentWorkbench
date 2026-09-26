#ifndef AWB_WORKBENCH_WORKBENCHCONTEXT_H
#define AWB_WORKBENCH_WORKBENCHCONTEXT_H

#include <QObject>
#include <QString>
#include <QUrl>

namespace awb::agents {
class AgentsFacade;
} // namespace awb::agents
namespace awb::shell {
class NavigationModel;
class Notifications;
class UiServices;
} // namespace awb::shell

namespace awb::workbench {

// The QML global `workbench`: cross-domain intents and generic actions
// (01-architecture.md §4.8). It is the only place allowed to talk to more
// than one domain — e.g. openWeb needs the agent's URL and the web/UI side.
class WorkbenchContext : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentPageId READ currentPageId NOTIFY currentPageChanged)
    // One-shot legacy-import notice for the startup popup (01 §7.3);
    // empty on every normal start.
    Q_PROPERTY(QString legacyImportNotice READ legacyImportNotice
               NOTIFY legacyImportNoticeChanged)

public:
    WorkbenchContext(shell::NavigationModel *nav, shell::UiServices *ui,
                     shell::Notifications *notifications,
                     agents::AgentsFacade *agents, QObject *parent = nullptr);

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

    // Cross-domain intents.
    // S4: opens the system browser (S5 replaces it with an embedded tab
    // when web.surface == "embedded").
    Q_INVOKABLE void openWeb(const QString &agentId);
    Q_INVOKABLE void closeWeb(const QString &agentId);
    Q_INVOKABLE void reloadWeb(const QString &agentId);
    // Restart an agent (offline overlay, launcher cards).
    Q_INVOKABLE void launchAgent(const QString &agentId);

    // Generic actions (01 §4.8).
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void notify(const QString &level, const QString &title,
                            const QString &text);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    Q_INVOKABLE void openFolder(const QString &path);
    Q_INVOKABLE void openConfigDir(const QString &agentId);
    Q_INVOKABLE void quit();

signals:
    void currentPageChanged();
    void legacyImportNoticeChanged();

private:
    shell::NavigationModel *m_nav;
    shell::UiServices *m_ui;
    shell::Notifications *m_notifications;
    agents::AgentsFacade *m_agents;
    QString m_legacyImportNotice;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_WORKBENCHCONTEXT_H
