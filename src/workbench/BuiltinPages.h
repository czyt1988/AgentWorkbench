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
} // namespace awb::shell

namespace awb::workbench {

// Registers the built-in pages into the shell's NavigationModel and wires
// the cross-domain rules (01-architecture.md §4.8): sidebar badges from
// agent state, current-page persistence. Later stages add their pages
// through the same path (S5: web, S6: skills, S7: plugins).
class BuiltinPages : public QObject
{
    Q_OBJECT

public:
    BuiltinPages(shell::NavigationModel *nav, shell::ShellController *shell,
                 agents::AgentsFacade *agents, QObject *parent = nullptr);

private:
    void registerPages();
    void wireBadges();
    void wirePagePersistence();

    shell::NavigationModel *m_nav;
    shell::ShellController *m_shell;
    agents::AgentsFacade *m_agents;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_BUILTINPAGES_H
