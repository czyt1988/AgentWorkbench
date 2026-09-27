#ifndef AWB_WORKBENCH_BUILTINPAGES_H
#define AWB_WORKBENCH_BUILTINPAGES_H

#include <QObject>
#include <QString>

namespace awb::agents {
class AgentsFacade;
} // namespace awb::agents
namespace awb::shell {
class NavigationModel;
class ShellController;
class Notifications;
} // namespace awb::shell
namespace awb::web {
class WebTabsFacade;
} // namespace awb::web
namespace awb::skills {
class SkillsFacade;
} // namespace awb::skills

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
                 agents::AgentsFacade *agents, web::WebTabsFacade *web,
                 shell::Notifications *notifications,
                 skills::SkillsFacade *skills, QObject *parent = nullptr);

private:
    void registerPages();
    void wireBadges();
    void wirePagePersistence();

    void wireWebRules();

    shell::NavigationModel *m_nav;
    shell::ShellController *m_shell;
    agents::AgentsFacade *m_agents;
    web::WebTabsFacade *m_web;
    shell::Notifications *m_notifications;
    skills::SkillsFacade *m_skills;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_BUILTINPAGES_H
