#ifndef AWB_AGENTS_AGENTSTATE_H
#define AWB_AGENTS_AGENTSTATE_H

#include <QString>

namespace awb::agentcatalog {

/// agent 的运行期状态：与 AgentDefinition（持久化在 agents.json 的定义）相对。
///
/// 除 setupDone（记录在 agent_state.json）外，这些字段只存在于内存。
struct AgentState
{
    bool running = false;         ///< 健康检查判定为运行中
    bool launching = false;       ///< 瞬态 UI 状态，不持久化
    bool installed = false;       ///< 运行期判定，经 versionCommand 检测
    QString version;              ///< 运行期数据，从 versionCommand 输出解析
    bool versionKnown = false;    ///< installed/version 是否已有探测结论——
                                  ///< 超时或未探测时为 false，界面据此把
                                  ///< 「不知道」与「确认未安装」区分开
    bool installing = false;      ///< 瞬态 UI 状态，不持久化
    bool setupDone = false;       ///< setup 已成功执行过，记录在 agent_state.json
    bool setupping = false;       ///< 瞬态 UI 状态，不持久化
    bool checkingVersion = false; ///< 版本查询进行中
    QString consoleOutput;        ///< install/update/setup 的实时 stdout/stderr，
                                  ///< 上卡片显示让用户看到进度
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTSTATE_H
