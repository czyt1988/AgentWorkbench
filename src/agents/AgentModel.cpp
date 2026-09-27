#include "agents/AgentModel.h"

#include <QSet>
#include <QVariantMap>

namespace awb::agents {

AgentModel::AgentModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int AgentModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_definitions.size();
}

QVariant AgentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_definitions.size())
        return {};
    const AgentDefinition &d = m_definitions.at(index.row());
    const AgentState s = m_states.value(d.id);

    switch (role) {
    case IdRole:        return d.id;
    case NameRole:      return d.name;
    case CommandRole:   return d.command;
    case WebUrlRole:    return d.webUrl;
    case ConfigDirRole: return d.configDir;
    case IconRole:      return d.icon;
    case ColorRole:     return d.color;
    case CardColorRole: return d.cardColor;
    case RunningRole:   return s.running;
    case LaunchingRole: return s.launching;
    case InstallCommandRole: return d.installCommand;
    case UpdateCommandRole:  return d.updateCommand;
    case VersionCommandRole: return d.versionCommand;
    case SetupCommandRole:   return d.setupCommand;
    case InstalledRole:  return s.installed;
    case VersionRole:    return s.version;
    case InstallingRole: return s.installing;
    case SetupDoneRole:  return s.setupDone;
    case SetuppingRole:  return s.setupping;
    case CheckingVersionRole: return s.checkingVersion;
    case ConsoleOutputRole:   return s.consoleOutput;
    }
    return {};
}

QHash<int, QByteArray> AgentModel::roleNames() const
{
    // Byte-compatible with 0.3.0: names AND order.
    return {
        { IdRole,        "agentId" },
        { NameRole,      "name" },
        { CommandRole,   "command" },
        { WebUrlRole,    "webUrl" },
        { ConfigDirRole, "configDir" },
        { IconRole,      "icon" },
        { ColorRole,     "color" },
        { CardColorRole, "cardColor" },
        { RunningRole,   "running" },
        { LaunchingRole, "launching" },
        { InstallCommandRole, "installCommand" },
        { UpdateCommandRole,  "updateCommand" },
        { VersionCommandRole, "versionCommand" },
        { SetupCommandRole,   "setupCommand" },
        { InstalledRole,  "installed" },
        { VersionRole,     "version" },
        { InstallingRole,  "installing" },
        { SetupDoneRole,   "setupDone" },
        { SetuppingRole,   "setupping" },
        { CheckingVersionRole, "checkingVersion" },
        { ConsoleOutputRole, "consoleOutput" }
    };
}

void AgentModel::setDefinitions(const QList<AgentDefinition> &definitions)
{
    beginResetModel();
    m_definitions = definitions;
    endResetModel();
    // Runtime state survives by id: swapping definitions must never blank a
    // running card (states of removed ids are dropped below).
    QSet<QString> keep;
    for (const AgentDefinition &d : m_definitions)
        keep.insert(d.id);
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (!keep.contains(it.key()))
            it = m_states.erase(it);
        else
            ++it;
    }
}

bool AgentModel::replaceDefinition(const AgentDefinition &definition)
{
    const int row = indexOf(definition.id);
    if (row < 0)
        return false;
    m_definitions[row] = definition;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx); // no roles = all roles
    return true;
}

void AgentModel::insertAgent(int row, const AgentDefinition &definition)
{
    if (row < 0 || row > m_definitions.size())
        row = m_definitions.size();
    beginInsertRows(QModelIndex(), row, row);
    m_definitions.insert(row, definition);
    endInsertRows();
}

bool AgentModel::removeAgentById(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0)
        return false;
    beginRemoveRows(QModelIndex(), row, row);
    m_definitions.removeAt(row);
    endRemoveRows();
    m_states.remove(id);
    return true;
}

int AgentModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_definitions.size(); ++i) {
        if (m_definitions.at(i).id == id)
            return i;
    }
    return -1;
}

QVariantMap AgentModel::agent(const QString &id) const
{
    QVariantMap m;
    const int row = indexOf(id);
    if (row < 0)
        return m;
    const AgentDefinition &d = m_definitions.at(row);
    const AgentState s = m_states.value(id);
    m[QStringLiteral("id")] = d.id;
    m[QStringLiteral("name")] = d.name;
    m[QStringLiteral("command")] = d.command;
    m[QStringLiteral("webUrl")] = d.webUrl;
    m[QStringLiteral("configDir")] = d.configDir;
    m[QStringLiteral("icon")] = d.icon;
    m[QStringLiteral("color")] = d.color;
    m[QStringLiteral("cardColor")] = d.cardColor;
    m[QStringLiteral("running")] = s.running;
    m[QStringLiteral("launching")] = s.launching;
    m[QStringLiteral("installCommand")] = d.installCommand;
    m[QStringLiteral("updateCommand")] = d.updateCommand;
    m[QStringLiteral("versionCommand")] = d.versionCommand;
    m[QStringLiteral("setupCommand")] = d.setupCommand;
    m[QStringLiteral("tokenFile")] = d.tokenFile;
    m[QStringLiteral("installed")] = s.installed;
    m[QStringLiteral("version")] = s.version;
    m[QStringLiteral("installing")] = s.installing;
    m[QStringLiteral("setupDone")] = s.setupDone;
    m[QStringLiteral("setupping")] = s.setupping;
    m[QStringLiteral("checkingVersion")] = s.checkingVersion;
    return m;
}

// --- Runtime state setters --------------------------------------------------
// Each one flips a single field of the id's state and emits dataChanged for
// its role — same guards as 0.3.0 (unknown id or unchanged value: no signal).

#define AWB_STATE_SETTER(field, value, role)                                 \
    do {                                                                     \
        const int row = indexOf(id);                                         \
        if (row < 0)                                                         \
            return;                                                          \
        AgentState &s = m_states[id];                                        \
        if (s.field == (value))                                              \
            return;                                                          \
        s.field = (value);                                                   \
        const QModelIndex idx = index(row, 0);                               \
        emit dataChanged(idx, idx, { role });                                \
    } while (false)

void AgentModel::setRunning(const QString &id, bool running)
{
    AWB_STATE_SETTER(running, running, RunningRole);
}

void AgentModel::setLaunching(const QString &id, bool launching)
{
    AWB_STATE_SETTER(launching, launching, LaunchingRole);
}

void AgentModel::setInstalled(const QString &id, bool installed)
{
    AWB_STATE_SETTER(installed, installed, InstalledRole);
}

void AgentModel::setVersion(const QString &id, const QString &version)
{
    AWB_STATE_SETTER(version, version, VersionRole);
}

void AgentModel::setInstalling(const QString &id, bool installing)
{
    AWB_STATE_SETTER(installing, installing, InstallingRole);
}

void AgentModel::setSetupDone(const QString &id, bool done)
{
    AWB_STATE_SETTER(setupDone, done, SetupDoneRole);
}

void AgentModel::setSetupping(const QString &id, bool setupping)
{
    AWB_STATE_SETTER(setupping, setupping, SetuppingRole);
}

void AgentModel::setCheckingVersion(const QString &id, bool checking)
{
    AWB_STATE_SETTER(checkingVersion, checking, CheckingVersionRole);
}

void AgentModel::setConsoleOutput(const QString &id, const QString &text)
{
    AWB_STATE_SETTER(consoleOutput, text, ConsoleOutputRole);
}

} // namespace awb::agents
