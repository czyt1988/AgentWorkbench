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
namespace awb::tools {
class ToolsFacade;
} // namespace awb::tools

namespace awb::workbench {

/// 把内置页面注册进 shell 的 NavigationModel，并布线跨域规则。
///
/// 注册内容包括侧栏徽标（来自 agent 状态）与当前页持久化；后续阶段的
/// 页面（S5 web、S6 skills、S7 plugins）走同一条注册路径。
class BuiltinPages : public QObject
{
    Q_OBJECT

public:
    // 构造即完成注册与全部布线
    BuiltinPages(shell::NavigationModel *nav, shell::ShellController *shell,
                 agentcatalog::AgentsFacade *agents, web::WebTabsFacade *web,
                 shell::Notifications *notifications,
                 skillcatalog::SkillsFacade *skills,
                 tools::ToolsFacade *tools, QObject *parent = nullptr);

private:
    // 注册五个内置页面（agents/web/skills/tools/settings）
    void registerPages();
    // 布线侧栏徽标：launcher 徽标 = 运行中的 agent 数，web 徽标 = 标签数
    void wireBadges();
    // 布线当前页持久化：启动恢复上次页面，切换即保存
    void wirePagePersistence();

    // 布线 web 相关的跨域规则（离线遮罩、会话 URL 换靶、外部打开提示）
    void wireWebRules();

    shell::NavigationModel *m_nav;         ///< 页面注册与徽标
    shell::ShellController *m_shell;       ///< 上次页面的持久化
    agentcatalog::AgentsFacade *m_agents;  ///< agent 运行状态（徽标、跨域规则）
    web::WebTabsFacade *m_web;             ///< Web 标签页域
    shell::Notifications *m_notifications; ///< toast 通知
    skillcatalog::SkillsFacade *m_skills;  ///< Skills 页的门面
    tools::ToolsFacade *m_tools;           ///< Tools 页的门面
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_BUILTINPAGES_H
