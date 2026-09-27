#include "agentcatalog/AgentStateStore.h"

#include "core/JsonStore.h"

#include <QFile>
#include <QJsonObject>

namespace awb::agents {

AgentStateStore::AgentStateStore(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
}

QString AgentStateStore::stateFilePath() const
{
    return m_dataRoot + QStringLiteral("/agent_state.json");
}

void AgentStateStore::load()
{
    m_setupDone.clear();
    const QJsonObject root = core::JsonStore::readFile(stateFilePath());
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.value().isObject()
            && it.value().toObject()
                   .value(QStringLiteral("setupDone")).toBool())
            m_setupDone.insert(it.key(), true);
    }
}

bool AgentStateStore::isSetupDone(const QString &id) const
{
    return m_setupDone.value(id, false);
}

bool AgentStateStore::markSetupDone(const QString &id)
{
    QJsonObject root = core::JsonStore::readFile(stateFilePath());
    QJsonObject agentState = root.value(id).toObject();
    agentState[QStringLiteral("setupDone")] = true;
    root[id] = agentState;

    if (!core::JsonStore::writeFile(stateFilePath(), root).ok)
        return false;
    m_setupDone.insert(id, true);
    return true;
}

bool AgentStateStore::reset(const QString &id)
{
    // A missing file means nothing has been recorded — nothing to clear
    // (the caller stays silent, as 0.3.0 did).
    if (!QFile::exists(stateFilePath()))
        return false;

    QJsonObject root = core::JsonStore::readFile(stateFilePath());
    root.remove(id); // no-op when the id was never recorded
    if (!core::JsonStore::writeFile(stateFilePath(), root).ok)
        return false;
    m_setupDone.remove(id);
    return true;
}

} // namespace awb::agents
