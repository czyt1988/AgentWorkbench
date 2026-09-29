#include "agentcatalog/AgentModel.h"

#include <QSet>
#include <QVariantMap>
#include <utility>

namespace awb::agentcatalog {

/**
 * @brief 构造空模型
 *
 * 定义与状态都由 setDefinitions() 与各状态槽填入。
 *
 * @param parent QObject 父项
 */
AgentModel::AgentModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

/**
 * @brief 取行数
 *
 * @param parent 父索引；列表模型无层级，有效时按 QAbstractListModel 惯例返回 0
 * @return 定义条数
 */
int AgentModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_definitions.size();
}

/**
 * @brief 取某行某 role 的值
 *
 * 定义类 role 读 AgentDefinition，运行期 role 读按 id 存的 AgentState，
 * 两者在这里合并成单一视图。
 *
 * @param index 行索引；无效或越界返回空 QVariant
 * @param role  Roles 之一
 * @return 对应字段的值
 */
QVariant AgentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_definitions.size()) {
        return {};
    }
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

/**
 * @brief 取 role 名表
 *
 * @return role id -> QML 侧的 role 名
 */
QHash<int, QByteArray> AgentModel::roleNames() const
{
    // 与 0.3.0 逐字节兼容：名字与顺序都不能动。
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

/**
 * @brief 整体替换定义列表
 *
 * 模型整体 reset（视图重建全部行）。换过后仍存在的 id 保留其运行期状态，
 * 被移除 id 的状态随之丢弃——替换定义绝不能把运行中的卡片清空。
 *
 * @param definitions 新的定义列表
 */
void AgentModel::setDefinitions(const QList<AgentDefinition> &definitions)
{
    beginResetModel();
    m_definitions = definitions;
    endResetModel();
    // 状态按 id 存活：换定义不许清空运行中的卡片（被移除 id 的状态
    // 在下面丢弃）。
    QSet<QString> keep;
    for (const AgentDefinition &d : std::as_const(m_definitions)) {
        keep.insert(d.id);
    }
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (!keep.contains(it.key())) {
            it = m_states.erase(it);
        }
        else {
            ++it;
        }
    }
}

/**
 * @brief 按 id 原位替换定义
 *
 * 只刷新该行，不 reset 整个模型；运行期状态按 id 另存，不受影响。
 *
 * @param definition 新定义，按其 id 定位行
 * @return 替换成功返回 true；id 不在模型中返回 false
 */
bool AgentModel::replaceDefinition(const AgentDefinition &definition)
{
    const int row = indexOf(definition.id);
    if (row < 0) {
        return false;
    }
    m_definitions[row] = definition;
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx); // 不带 roles = 视为全部 role 都变了
    return true;
}

/**
 * @brief 在指定行插入定义
 *
 * @param row        目标行；< 0 或超出末尾时改为追加
 * @param definition 新定义
 */
void AgentModel::insertAgent(int row, const AgentDefinition &definition)
{
    if (row < 0 || row > m_definitions.size()) {
        row = m_definitions.size();
    }
    beginInsertRows(QModelIndex(), row, row);
    m_definitions.insert(row, definition);
    endInsertRows();
}

/**
 * @brief 删除指定 id 的 agent
 *
 * 行删除经 beginRemoveRows/endRemoveRows 通知视图，该 id 的运行期状态一并
 * 清掉，不留悬空条目。
 *
 * @param id agent id
 * @return 删除成功返回 true；id 不在模型中返回 false
 */
bool AgentModel::removeAgentById(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_definitions.removeAt(row);
    endRemoveRows();
    m_states.remove(id);
    return true;
}

/**
 * @brief 查 id 所在的行号
 *
 * @param id agent id
 * @return 行号；不存在返回 -1
 */
int AgentModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_definitions.size(); ++i) {
        if (m_definitions.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 取 id 的定义与运行期状态的合并 map
 *
 * 键名沿用 0.3.0 的表单字段名；tokenFile 只在这里暴露（卡片 role 不含
 * 它），consoleOutput 不进表单。编辑对话框直接拿它当输入。
 *
 * @param id agent id
 * @return 合并后的 map；id 不在模型中返回空 map
 */
QVariantMap AgentModel::agent(const QString &id) const
{
    QVariantMap m;
    const int row = indexOf(id);
    if (row < 0) {
        return m;
    }
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

// --- 运行期状态 setter ---------------------------------------------------
// 每个 setter 只改该 id 状态的一个字段并按对应 role 发 dataChanged；
// 守卫与 0.3.0 相同（id 未知或值未变：不发信号）。

#define AWB_STATE_SETTER(field, value, role)                                 \
    do {                                                                     \
        const int row = indexOf(id);                                         \
        if (row < 0) {                                                       \
            return;                                                          \
        }                                                                    \
        AgentState &s = m_states[id];                                        \
        if (s.field == (value)) {                                            \
            return;                                                          \
        }                                                                    \
        s.field = (value);                                                   \
        const QModelIndex idx = index(row, 0);                               \
        Q_EMIT dataChanged(idx, idx, { role });                              \
    } while (false)

/**
 * @brief 记录 id 的运行状态（健康检查结果），发 RunningRole 的 dataChanged
 */
void AgentModel::setRunning(const QString &id, bool running)
{
    AWB_STATE_SETTER(running, running, RunningRole);
}

/**
 * @brief 标记 id 的 launch 已发起，发 LaunchingRole 的 dataChanged
 */
void AgentModel::setLaunching(const QString &id, bool launching)
{
    AWB_STATE_SETTER(launching, launching, LaunchingRole);
}

/**
 * @brief 记录 id 的安装判定（versionCommand 检测），发 InstalledRole 的 dataChanged
 */
void AgentModel::setInstalled(const QString &id, bool installed)
{
    AWB_STATE_SETTER(installed, installed, InstalledRole);
}

/**
 * @brief 记录 id 的版本号，发 VersionRole 的 dataChanged
 */
void AgentModel::setVersion(const QString &id, const QString &version)
{
    AWB_STATE_SETTER(version, version, VersionRole);
}

/**
 * @brief 标记 id 的安装进行中，发 InstallingRole 的 dataChanged
 */
void AgentModel::setInstalling(const QString &id, bool installing)
{
    AWB_STATE_SETTER(installing, installing, InstallingRole);
}

/**
 * @brief 记录 id 的 setup 完成，发 SetupDoneRole 的 dataChanged
 */
void AgentModel::setSetupDone(const QString &id, bool done)
{
    AWB_STATE_SETTER(setupDone, done, SetupDoneRole);
}

/**
 * @brief 标记 id 的 setup 进行中，发 SetuppingRole 的 dataChanged
 */
void AgentModel::setSetupping(const QString &id, bool setupping)
{
    AWB_STATE_SETTER(setupping, setupping, SetuppingRole);
}

/**
 * @brief 标记 id 的版本查询进行中，发 CheckingVersionRole 的 dataChanged
 */
void AgentModel::setCheckingVersion(const QString &id, bool checking)
{
    AWB_STATE_SETTER(checkingVersion, checking, CheckingVersionRole);
}

/**
 * @brief 记录 id 的控制台实时输出，发 ConsoleOutputRole 的 dataChanged
 */
void AgentModel::setConsoleOutput(const QString &id, const QString &text)
{
    AWB_STATE_SETTER(consoleOutput, text, ConsoleOutputRole);
}

} // namespace awb::agentcatalog
