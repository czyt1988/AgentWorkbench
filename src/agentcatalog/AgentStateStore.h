#ifndef AWB_AGENTS_AGENTSTATESTORE_H
#define AWB_AGENTS_AGENTSTATESTORE_H

#include <QHash>
#include <QString>

namespace awb::agentcatalog {

/// agent_state.json 的读写：记录哪些 agent 已完成一次性 setup。
///
/// 启动时整体读入一次；写回经 core::JsonStore 原子落盘。
class AgentStateStore
{
public:
    explicit AgentStateStore(const QString &dataRoot);

    // 读入整个文件；文件缺失或损坏一律视为「未做过 setup」
    void load();

    // 该 agent 是否已完成 setup
    bool isSetupDone(const QString &id) const;

    // 记录 setup 完成；文件写不下去返回 false（调用方提示下次启动会重跑 setup）
    bool markSetupDone(const QString &id);

    // 抹掉记录，让 setup 在下次启动前重跑；无东西可写时返回 false
    bool reset(const QString &id);

    // 状态文件的路径
    QString stateFilePath() const;

private:
    QString m_dataRoot;          ///< 数据根，状态文件从它派生
    QHash<QString, bool> m_setupDone;  ///< 已完成 setup 的 agent id 集合
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTSTATESTORE_H
