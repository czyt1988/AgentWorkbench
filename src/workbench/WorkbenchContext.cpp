#include "workbench/WorkbenchContext.h"

#include "agents/AgentsFacade.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/UiServices.h"

#include <QCoreApplication>

namespace awb::workbench {

WorkbenchContext::WorkbenchContext(shell::NavigationModel *nav,
                                   shell::UiServices *ui,
                                   shell::Notifications *notifications,
                                   agents::AgentsFacade *agents,
                                   QObject *parent)
    : QObject(parent)
    , m_nav(nav)
    , m_ui(ui)
    , m_notifications(notifications)
    , m_agents(agents)
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
    // S4 behaviour (specs/03 S4-T5): no embedded surface yet — open the
    // system browser. S5 routes this through WebTabsFacade when the
    // embedded surface is available.
    m_agents->openWeb(agentId);
}

void WorkbenchContext::closeWeb(const QString &agentId)
{
    // Tabs arrive with S5 (specs/03 S4-T5).
    Q_UNUSED(agentId)
}

void WorkbenchContext::reloadWeb(const QString &agentId)
{
    Q_UNUSED(agentId)
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

void WorkbenchContext::quit()
{
    QCoreApplication::quit();
}

} // namespace awb::workbench
