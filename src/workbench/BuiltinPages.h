#ifndef AWB_WORKBENCH_BUILTINPAGES_H
#define AWB_WORKBENCH_BUILTINPAGES_H

#include <QObject>
#include <QString>

namespace awb::agentcatalog {
class AgentsFacade;
} // namespace awb::agentcatalog
namespace awb::shell {
class NavigationModel;
class ShellController;
class Notifications;
} // namespace awb::shell
namespace awb::web {
class WebTabsFacade;
} // namespace awb::web
namespace awb::skillcatalog {
class SkillsFacade;
} // namespace awb::skillcatalog

namespace awb::workbench {

// Registers the built-in pages into the shell's NavigationModel and wires
// the cross-domain rules: sidebar badges from
// agent state, current-page persistence. Later stages add their pages
// through the same path (S5: web, S6: skills, S7: plugins).
class BuiltinPages : public QObject
{
    Q_OBJECT

public:
    BuiltinPages(shell::NavigationModel *nav, shell::ShellController *shell,
                 agentcatalog::AgentsFacade *agents, web::WebTabsFacade *web,
                 shell::Notifications *notifications,
                 skillcatalog::SkillsFacade *skills, QObject *parent = nullptr);

private:
    void registerPages();
    void wireBadges();
    void wirePagePersistence();

    void wireWebRules();

    shell::NavigationModel *m_nav;
    shell::ShellController *m_shell;
    agentcatalog::AgentsFacade *m_agents;
    web::WebTabsFacade *m_web;
    shell::Notifications *m_notifications;
    skillcatalog::SkillsFacade *m_skills;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_BUILTINPAGES_H
