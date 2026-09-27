#include "workbench/BuiltinPages.h"

#include "agents/AgentModel.h"
#include "agents/AgentsFacade.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/ShellController.h"
#include "skills/SkillsFacade.h"
#include "web/WebTabsFacade.h"

namespace awb::workbench {

BuiltinPages::BuiltinPages(shell::NavigationModel *nav,
                           shell::ShellController *shell,
                           agents::AgentsFacade *agents,
                           web::WebTabsFacade *web,
                           shell::Notifications *notifications,
                           skills::SkillsFacade *skills, QObject *parent)
    : QObject(parent)
    , m_nav(nav)
    , m_shell(shell)
    , m_agents(agents)
    , m_web(web)
    , m_notifications(notifications)
    , m_skills(skills)
{
    registerPages();
    wireBadges();
    wirePagePersistence();
    wireWebRules();
}

void BuiltinPages::registerPages()
{
    // Page list and metadata: The `web` and
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

    shell::PageDescriptor webPage;
    webPage.id = QStringLiteral("web");
    webPage.title = tr("Agent Web UI");
    webPage.iconSource = QStringLiteral("qrc:/icons/web.svg");
    webPage.source =
        QStringLiteral("qrc:/qt/qml/AgentWorkbench/web/WebTabsPage.qml");
    webPage.section = QStringLiteral("main");
    webPage.order = 20;
    m_nav->registerPage(webPage);

    shell::PageDescriptor skillsPage;
    skillsPage.id = QStringLiteral("skills");
    skillsPage.title = tr("Skills");
    skillsPage.iconSource = QStringLiteral("qrc:/icons/skills.svg");
    skillsPage.source =
        QStringLiteral("qrc:/qt/qml/AgentWorkbench/skills/SkillGridPage.qml");
    skillsPage.section = QStringLiteral("main");
    skillsPage.order = 30;
    m_nav->registerPage(skillsPage);

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

void BuiltinPages::wireWebRules()
{
    // Cross-domain rules: agent stopped -> tab offline; agent
    // back -> reload; agent deleted -> close its tab. The external-surface
    // notice becomes a toast.
    connect(m_agents, &agents::AgentsFacade::runningChanged, this,
            [this](const QString &id, bool running) {
                if (running)
                    m_web->markOnlineForAgent(id);
                else
                    m_web->markOfflineForAgent(id);
            });
    connect(m_agents, &agents::AgentsFacade::agentRemoved, this,
            [this](const QString &id) { m_web->closeTabsForAgent(id); });

    // A session URL captured from the agent's launch output (a fresh
    // per-process token for dsh) retargets an already-open tab instead of
    // leaving it on the bare webUrl the token gate rejects with 401.
    connect(m_agents, &agents::AgentsFacade::sessionUrlChanged, this,
            [this](const QString &id, const QString &url) {
                m_web->retargetTabForAgent(id, url);
            });

    connect(m_web, &web::WebTabsFacade::externalOpened, this,
            [this](const QString &url) {
                m_notifications->notify(
                    QStringLiteral("info"), tr("Opening in the browser"),
                    url);
            });

    // Web tab count badge.
    auto updateWebBadge = [this]() {
        const int count = m_web->model() ? m_web->model()->rowCount() : 0;
        m_nav->setBadge(QStringLiteral("web"),
                        count > 0 ? QString::number(count) : QString());
    };
    connect(m_web->model(), &QAbstractItemModel::rowsInserted, this,
            updateWebBadge);
    connect(m_web->model(), &QAbstractItemModel::rowsRemoved, this,
            updateWebBadge);
    updateWebBadge();
}

void BuiltinPages::wirePagePersistence()
{
    // Restore the last visited page once and
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
