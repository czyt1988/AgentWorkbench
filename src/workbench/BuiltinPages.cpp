#include "workbench/BuiltinPages.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentsFacade.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/ShellController.h"
#include "skillcatalog/SkillsFacade.h"
#include "tools/ToolsFacade.h"
#include "web/WebTabsFacade.h"

namespace awb::workbench {

BuiltinPages::BuiltinPages(shell::NavigationModel *nav,
                           shell::ShellController *shell,
                           agentcatalog::AgentsFacade *agents,
                           web::WebTabsFacade *web,
                           shell::Notifications *notifications,
                           skillcatalog::SkillsFacade *skills,
                           tools::ToolsFacade *tools, QObject *parent)
    : QObject(parent)
    , m_nav(nav)
    , m_shell(shell)
    , m_agents(agents)
    , m_web(web)
    , m_notifications(notifications)
    , m_skills(skills)
    , m_tools(tools)
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
        QStringLiteral("qrc:/qt/qml/AgentWorkbench/agentcatalog/AgentGridPage.qml");
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
    // Web 页常驻：WebEngineView 的页面状态搬不进 C++，切页销毁会让
    // Web UI 整页重载；Workspace 对 keepAlive 页只隐藏不销毁。
    webPage.keepAlive = true;
    m_nav->registerPage(webPage);

    shell::PageDescriptor skillsPage;
    skillsPage.id = QStringLiteral("skills");
    skillsPage.title = tr("Skills");
    skillsPage.iconSource = QStringLiteral("qrc:/icons/skills.svg");
    skillsPage.source =
        QStringLiteral("qrc:/qt/qml/AgentWorkbench/skillcatalog/SkillGridPage.qml");
    skillsPage.section = QStringLiteral("main");
    skillsPage.order = 30;
    m_nav->registerPage(skillsPage);

    // Agent Tools joins after Skills: the prompt workbench is part of the
    // daily workflow, not a system page.
    shell::PageDescriptor toolsPage;
    toolsPage.id = QStringLiteral("tools");
    toolsPage.title = tr("Agent Tools");
    toolsPage.iconSource = QStringLiteral("qrc:/icons/tools.svg");
    toolsPage.source = QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/tools/ToolsPage.qml");
    toolsPage.section = QStringLiteral("main");
    toolsPage.order = 40;
    m_nav->registerPage(toolsPage);

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
        const QList<awb::agentcatalog::AgentDefinition> &definitions =
            m_agents->agentModel()->definitions();
        int running = 0;
        for (const awb::agentcatalog::AgentDefinition &d : definitions) {
            if (m_agents->agentModel()->state(d.id).running)
                ++running;
        }
        m_nav->setBadge(QStringLiteral("agents"),
                        running > 0 ? QString::number(running) : QString());
    };

    const awb::agentcatalog::AgentModel *model = m_agents->agentModel();
    connect(model, &awb::agentcatalog::AgentModel::dataChanged, this,
            [update](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
                if (roles.isEmpty()
                    || roles.contains(awb::agentcatalog::AgentModel::RunningRole))
                    update();
            });
    connect(model, &awb::agentcatalog::AgentModel::rowsInserted, this, update);
    connect(model, &awb::agentcatalog::AgentModel::rowsRemoved, this, update);
    update();
}

void BuiltinPages::wireWebRules()
{
    // Cross-domain rules: agent stopped -> tab offline; agent
    // back -> reload; agent deleted -> close its tab. The external-surface
    // notice becomes a toast.
    connect(m_agents, &agentcatalog::AgentsFacade::runningChanged, this,
            [this](const QString &id, bool running) {
                if (running)
                    m_web->markOnlineForAgent(id);
                else
                    m_web->markOfflineForAgent(id);
            });
    connect(m_agents, &agentcatalog::AgentsFacade::agentRemoved, this,
            [this](const QString &id) { m_web->closeTabsForAgent(id); });

    // A session URL captured from the agent's launch output (a fresh
    // per-process token for dsh) retargets an already-open tab instead of
    // leaving it on the bare webUrl the token gate rejects with 401.
    connect(m_agents, &agentcatalog::AgentsFacade::sessionUrlChanged, this,
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
