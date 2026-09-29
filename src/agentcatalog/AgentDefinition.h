#ifndef AWB_AGENTS_AGENTDEFINITION_H
#define AWB_AGENTS_AGENTDEFINITION_H

#include <QString>

namespace awb::agentcatalog {

/// agent 的持久化定义：agents.json 里写下的全部字段。
///
/// 运行期状态在 AgentState 里，从不持久化到这里。
struct AgentDefinition
{
    QString id;
    QString name;
    QString command;         ///< agent 的启动命令（长期运行的主进程，区别于一次性命令）
    QString webUrl;          ///< Web UI 地址：健康检查探测它，页面入口都从它派生
    QString configDir;
    QString icon;            ///< 图标 URL，经 core::IconResolver 解析
    QString color;           ///< 强调色；留空时按位置循环取主题 agentPalette
    QString cardColor;       ///< 卡片底色（非运行状态的视觉），可留空
    QString installCommand;  ///< 一次性安装命令，统一经 core::ScriptRunner 运行
    QString updateCommand;   ///< 一次性更新命令
    QString versionCommand;  ///< 一次性版本查询命令
    QString setupCommand;    ///< 首次启动前的一次性初始化命令；成功后写 agent_state.json
    QString tokenFile;       ///< bearer token 文件路径（经 EnvExpander 展开）；
                             ///< 启动时设为 QWEN_SERVER_TOKEN，打开浏览器时以
                             ///< #token=<value> 追加到 Web URL
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTDEFINITION_H
