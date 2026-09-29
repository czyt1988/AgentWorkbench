#include "agentcatalog/AgentStateStore.h"

#include "core/JsonStore.h"

#include <QFile>
#include <QJsonObject>

namespace awb::agentcatalog {

/**
 * @brief 构造状态存储
 *
 * @param dataRoot 数据根目录；stateFilePath() 由它派生，不在此处读文件
 */
AgentStateStore::AgentStateStore(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
}

/**
 * @brief 取状态文件路径
 *
 * @return <dataRoot>/agent_state.json
 */
QString AgentStateStore::stateFilePath() const
{
    return m_dataRoot + QStringLiteral("/agent_state.json");
}

/**
 * @brief 读入全部 setup 记录
 *
 * 文件里的形态是 { "<agentId>": { "setupDone": true }, … }；只收
 * setupDone 为 true 的条目，其余（含损坏文件的整体缺失）都不进入内存，
 * 之后一律按「未做过 setup」处理。
 */
void AgentStateStore::load()
{
    m_setupDone.clear();
    const QJsonObject root = core::JsonStore::readFile(stateFilePath());
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.value().isObject()
            && it.value().toObject()
                   .value(QStringLiteral("setupDone")).toBool()) {
            m_setupDone.insert(it.key(), true);
        }
    }
}

/**
 * @brief 查询某 agent 是否已完成 setup
 *
 * @param id agent id
 * @return 已记录完成返回 true；未记录返回 false
 */
bool AgentStateStore::isSetupDone(const QString &id) const
{
    return m_setupDone.value(id, false);
}

/**
 * @brief 记录某 agent 的 setup 已完成
 *
 * 落盘成功后才更新内存副本，保证两者不脱节。
 *
 * @param id agent id
 * @return 落盘成功返回 true；文件写不进去返回 false（内存不变，
 *         下次启动 setup 会重跑，由调用方提示用户）
 */
bool AgentStateStore::markSetupDone(const QString &id)
{
    QJsonObject root = core::JsonStore::readFile(stateFilePath());
    QJsonObject agentState = root.value(id).toObject();
    agentState[QStringLiteral("setupDone")] = true;
    root[id] = agentState;

    if (!core::JsonStore::writeFile(stateFilePath(), root).ok) {
        return false;
    }
    m_setupDone.insert(id, true);
    return true;
}

/**
 * @brief 抹掉某 agent 的 setup 记录
 *
 * @param id agent id；从未记录过时对文件是 no-op
 * @return 文件不存在或写入失败返回 false；成功返回 true
 */
bool AgentStateStore::reset(const QString &id)
{
    // 文件不存在 = 从未记录过任何东西，没有可清的（调用方保持静默，
    // 与 0.3.0 的行为一致）。
    if (!QFile::exists(stateFilePath())) {
        return false;
    }

    QJsonObject root = core::JsonStore::readFile(stateFilePath());
    root.remove(id); // id 从未记录时是 no-op
    if (!core::JsonStore::writeFile(stateFilePath(), root).ok) {
        return false;
    }
    m_setupDone.remove(id);
    return true;
}

} // namespace awb::agentcatalog
