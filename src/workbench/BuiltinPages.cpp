#include "workbench/BuiltinPages.h"

#include "agents/AgentModel.h"
#include "agents/AgentsFacade.h"
#include "shell/NavigationModel.h"
#include "shell/ShellController.h"

namespace awb::workbench {

BuiltinPages::BuiltinPages(shell::NavigationModel *nav,
                           shell::ShellController *shell,
                           agents::AgentsFacade *agents, QObject *parent)
    : QObject(parent)
    , m_nav(nav)
    , m_shell(shell)
    , m_agents(agents)
{
    registerPages();
    wireBadges();
    wirePagePersistence();
}

void BuiltinPages::registerPages()
{
    // Page list and metadata: 02-ui-specification.md §5. The `web` and
    // `skills` pages join in S5/S6 through this same registration path.
    shell::PageDescriptor launcher;
    launcher.id = QStringLiteral("agents");
    launcher.title = tr("Agent Launcher");
    launcher.iconSource = QStringLiteral("qrc:/icons/terminal.svg");
    launcher.source =
        QStringLiteral("qrc:/qt/qml/AgentWorkbench/agents/AgentGridPage.qml");
    launcher.section = QStringLiteral("main");
    launcher.order = 10;
    m_nav->registerPage(launcher);

    shell::PageDescriptor settings;
    settings.id = QStringLiteral("settings");
    settings.title = tr("Settings");
    settings.iconSource = QStringLiteral("qrc:/icons/gear.svg");
    settings.source =
        QStringLiteral("qrc:/qt/qml/AgentWorkbench/shell/SettingsPage.qml");
    settings.section = QStringLiteral("system");
    settings.order = 100;
    m_nav->registerPage(settings);
}

void BuiltinPages::wireBadges()
{
    // Launcher badge = number of running agents (hidden at 0) —
    // 02-ui-specification.md §3.2.
    auto update = [this]() {
        const QList<awb::agents::AgentDefinition> &definitions =
            m_agents->agentModel()->definitions();
        int running = 0;
        for (const awb::agents::AgentDefinition &d : definitions) {
            if (m_agents->agentModel()->state(d.id).running)
                ++running;
        }
        m_nav->setBadge(QStringLiteral("agents"),
                        running > 0 ? QString::number(running) : QString());
    };

    const awb::agents::AgentModel *model = m_agents->agentModel();
    connect(model, &awb::agents::AgentModel::dataChanged, this,
            [update](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (roles.isEmpty()
                    || roles.contains(awb::agents::AgentModel::RunningRole))
                    update();
            });
    connect(model, &awb::agents::AgentModel::rowsInserted, this, update);
    connect(model, &awb::agents::AgentModel::rowsRemoved, this, update);
    update();
}

void BuiltinPages::wirePagePersistence()
{
    // Restore the last visited page once (03-migration-plan.md S4-T9) and
    // persist every switch.
    const QString last = m_shell->lastPageId();
    if (!last.isEmpty())
        m_nav->setCurrentPageId(last);

    connect(m_nav, &shell::NavigationModel::currentPageChanged, this,
            [this]() {
                if (!m_nav->currentPageId().isEmpty())
                    m_shell->setLastPageId(m_nav->currentPageId());
            });
}

} // namespace awb::workbench
