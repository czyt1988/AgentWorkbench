#include "agentcatalog/AgentRepository.h"

#include "core/IconResolver.h"
#include "core/JsonStore.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace awb::agentcatalog {

namespace {

/**
 * @brief 一条定义的持久化形态
 *
 * 哪些字段进 agents.json 由这里唯一决定。
 *
 * @param a agent 定义
 * @return 可写入 agents.json 的 JSON 对象
 */
QJsonObject definitionObject(const AgentDefinition &a)
{
    QJsonObject o;
    o[QStringLiteral("id")] = a.id;
    o[QStringLiteral("name")] = a.name;
    o[QStringLiteral("command")] = a.command;
    o[QStringLiteral("webUrl")] = a.webUrl;
    o[QStringLiteral("configDir")] = a.configDir;
    o[QStringLiteral("icon")] = a.icon;
    o[QStringLiteral("color")] = a.color;
    o[QStringLiteral("cardColor")] = a.cardColor;
    o[QStringLiteral("installCommand")] = a.installCommand;
    o[QStringLiteral("updateCommand")] = a.updateCommand;
    o[QStringLiteral("versionCommand")] = a.versionCommand;
    o[QStringLiteral("setupCommand")] = a.setupCommand;
    o[QStringLiteral("tokenFile")] = a.tokenFile;
    return o;
}

/**
 * @brief 把定义列表转成 JSON 数组
 *
 * @param agents 定义列表
 * @return 逐条经 definitionObject() 转换的数组
 */
QJsonArray definitionsArray(const QList<AgentDefinition> &agents)
{
    QJsonArray arr;
    for (const AgentDefinition &a : agents) {
        arr.append(definitionObject(a));
    }
    return arr;
}

} // namespace

/**
 * @brief 构造仓库
 *
 * @param dataRoot 数据根目录；agents.json 从它派生，构造时不读文件
 */
AgentRepository::AgentRepository(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
}

/**
 * @brief 取配置文件路径
 *
 * @return <dataRoot>/agents.json
 */
QString AgentRepository::configFilePath() const
{
    return m_dataRoot + QStringLiteral("/agents.json");
}

/**
 * @brief 读入 agents.json 并完成内置同步与配色
 *
 * 文件缺失或读不出时从空列表起步。内置 agent 每次启动都按随包默认重新
 * 生成——磁盘文件真正决定的只有「哪些内置被删过」与「用户叠加了哪些自建
 * agent」。给没配色的自建 agent 补一个色板颜色，让卡片能渲染；同步或
 * 配色产生了任何变化都立即落盘，磁盘内容因此始终与界面所见一致。
 */
void AgentRepository::load()
{
    m_definitions.clear();
    m_removedIds.clear();

    QByteArray data;
    {
        QFile file(configFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            data = file.readAll();
        }
    }
    if (!data.isEmpty()) {
        m_definitions = parse(data);

        // 0.4.0 起根级 "title" 不再驱动窗口标题——它改住在 settings.json
        // 里了。给还留着它的用户提个醒，但只在 settings 文件已存在时提：
        // 之前没地方可以迁移。
        const QString legacyTitle = QJsonDocument::fromJson(data)
                                        .object()
                                        .value(QStringLiteral("title"))
                                        .toString();
        if (!legacyTitle.isEmpty()
            && QFile::exists(m_dataRoot + QStringLiteral("/settings.json"))) {
            qInfo().noquote() << QStringLiteral(
                "agents.json: the root \"title\" field is ignored; set the "
                "window title in Settings (settings.json window.title) "
                "instead.");
        }
    }

    // 内置 agent 一律来自随包默认，磁盘文件只决定哪些被用户删过，
    // 以及用户在其上叠加了哪些自建 agent。
    const QList<AgentDefinition> synced =
        withBuiltinDefaults(m_definitions, m_removedIds);
    const bool changed = definitionsArray(synced) != definitionsArray(m_definitions);
    m_definitions = synced;

    // 给用户添加时没填颜色的 agent 补一个，卡片才有得渲；两类变化都落盘。
    const bool colorsAssigned = assignPaletteColors();
    if (changed || colorsAssigned) {
        save();
    }
}

/**
 * @brief 把当前定义与删除记录写回 agents.json
 *
 * 与随包默认完全一致（无自建、无删除）时逐字节写入内置文件，保持
 * <dataRoot>/agents.json 与 config/default_agents.json 可 diff——开发默认
 * 启动器列表时靠它比对。其余情况写 { "agents": [...], "removed": [...] }，
 * 经 core::JsonStore 原子写、缩进一致。
 *
 * @return 落盘成功返回 true
 */
bool AgentRepository::save()
{
    const QString path = configFilePath();

    const QList<AgentDefinition> defaults = loadDefaults();
    if (m_removedIds.isEmpty()
        && definitionsArray(m_definitions) == definitionsArray(defaults)) {
        QFile bundled(QStringLiteral(":/config/default_agents.json"));
        if (bundled.open(QIODevice::ReadOnly)) {
            return core::JsonStore::writeBytes(path, bundled.readAll()).ok;
        }
    }

    QJsonObject root;
    root[QStringLiteral("agents")] = definitionsArray(m_definitions);
    if (!m_removedIds.isEmpty()) {
        QJsonArray removed;
        for (const QString &id : std::as_const(m_removedIds)) {
            removed.append(id);
        }
        root[QStringLiteral("removed")] = removed;
    }

    return core::JsonStore::writeFile(path, root).ok;
}

/**
 * @brief 恢复默认 agent 列表
 *
 * 清空删除记录，把随包内置列表叠到 current 之上：用户自建 agent 保留
 * 各自定义与顺序，被删过的内置 agent 全部回来。
 *
 * @param current 恢复前的定义列表（用户自建条目从这里保留）
 * @return 落盘成功返回 true
 */
bool AgentRepository::restoreDefaults(const QList<AgentDefinition> &current)
{
    m_removedIds.clear();
    m_definitions = withBuiltinDefaults(current, QStringList());
    return save();
}

/**
 * @brief 判断 id 是否为随包默认 agent
 *
 * @param id agent id
 * @return 属于 default_agents.json 时返回 true
 */
bool AgentRepository::isDefaultAgent(const QString &id) const
{
    return defaultAgentIds().contains(id);
}

/**
 * @brief 从 JSON 字节解析定义列表
 *
 * 根级 "removed" 数组顺带收进 m_removedIds；每条定义的 icon 经
 * resolveIcon() 归一（应用级回退在这里生效）。
 *
 * @param data agents.json 的原始字节
 * @return 解析出的定义列表；格式异常时缺字段按空串处理，不报错
 */
QList<AgentDefinition> AgentRepository::parse(const QByteArray &data)
{
    QList<AgentDefinition> result;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();
    const QJsonArray removed = root.value(QStringLiteral("removed")).toArray();
    for (const QJsonValue &v : removed) {
        m_removedIds.append(v.toString());
    }
    const QJsonArray arr = root.value(QStringLiteral("agents")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        AgentDefinition a;
        a.id = o.value(QStringLiteral("id")).toString();
        a.name = o.value(QStringLiteral("name")).toString();
        a.command = o.value(QStringLiteral("command")).toString();
        a.webUrl = o.value(QStringLiteral("webUrl")).toString();
        a.configDir = o.value(QStringLiteral("configDir")).toString();
        a.icon = o.value(QStringLiteral("icon")).toString();
        a.color = o.value(QStringLiteral("color")).toString();
        a.cardColor = o.value(QStringLiteral("cardColor")).toString();
        a.installCommand = o.value(QStringLiteral("installCommand")).toString();
        a.updateCommand = o.value(QStringLiteral("updateCommand")).toString();
        a.versionCommand = o.value(QStringLiteral("versionCommand")).toString();
        a.setupCommand = o.value(QStringLiteral("setupCommand")).toString();
        a.tokenFile = o.value(QStringLiteral("tokenFile")).toString();
        a.icon = resolveIcon(a.icon);
        result.append(a);
    }
    return result;
}

/**
 * @brief 把随包内置定义叠到现有列表之上
 *
 * @param current     现有定义（用户自建条目从这里保留）
 * @param removedIds  保持删除状态的内置 agent id
 * @return 内置（按随包顺序、跳过被删的）在前、用户自建（原顺序）在后的列表
 */
QList<AgentDefinition> AgentRepository::withBuiltinDefaults(
    const QList<AgentDefinition> &current, const QStringList &removedIds)
{
    const QList<AgentDefinition> defaults = loadDefaults();

    QList<AgentDefinition> result;
    result.reserve(defaults.size() + current.size());
    for (const AgentDefinition &def : defaults) {
        // 设置页里删过的——保持删除。
        if (removedIds.contains(def.id)) {
            continue;
        }
        result.append(def);
    }

    // 用户在其上叠加的自建 agent，保留各自定义与顺序。
    for (const AgentDefinition &a : current) {
        const bool builtin = std::any_of(
            defaults.cbegin(), defaults.cend(),
            [&](const AgentDefinition &def) { return def.id == a.id; });
        if (!builtin) {
            result.append(a);
        }
    }
    return result;
}

// --- 色板颜色分配 -----------------------------------------------------------

/**
 * @brief 给 color 为空的 agent 补色板颜色
 *
 * @return 有颜色被分配时返回 true（调用方据此落盘）
 */
bool AgentRepository::assignPaletteColors()
{
    bool changed = false;
    for (int i = 0; i < m_definitions.size(); ++i) {
        if (m_definitions[i].color.isEmpty()) {
            m_definitions[i].color = paletteColorFor(i);
            changed = true;
        }
    }
    return changed;
}

/**
 * @brief 取某位置应配的颜色
 *
 * @param index agent 在列表中的位置
 * @return 主题色板（注入时）或内置 Mocha 色板按位置循环取的颜色
 */
QString AgentRepository::paletteColorFor(int index) const
{
    // 优先当前主题的 agentPalette；没注入时回退内置 Mocha 色板。
    if (m_agentPalette.isEmpty()) {
        return paletteColorAt(index);
    }
    const int size = m_agentPalette.size();
    return m_agentPalette.at(((index % size) + size) % size);
}

/**
 * @brief 内置 Mocha 色板按位置取色
 *
 * Catppuccin Mocha 的亮色系，在深色卡片底（#313244）上可读性好；
 * S3 起由当前主题的 agentPalette 顶替。
 *
 * @param index 位置（可为负，模运算保证落在色板内）
 * @return 色板循环取出的颜色
 */
QString AgentRepository::paletteColorAt(int index)
{
    static const QStringList palette = {
        QStringLiteral("#f38ba8"), // Red
        QStringLiteral("#fab387"), // Peach
        QStringLiteral("#f9e2af"), // Yellow
        QStringLiteral("#a6e3a1"), // Green
        QStringLiteral("#94e2d5"), // Teal
        QStringLiteral("#89b4fa"), // Blue
        QStringLiteral("#cba6f7"), // Mauve
        QStringLiteral("#f5c2e7"), // Pink
    };
    return palette.at(((index % palette.size()) + palette.size()) % palette.size());
}

// --- 图标解析 ----------------------------------------------------------------

/**
 * @brief 解析用于显示的图标串
 *
 * 应用级回退图标在这里给；core::IconResolver 从不写死应用资源路径。
 *
 * @param raw 定义里的原始 icon 值
 * @return 归一后的图标 URL；无法解析时返回默认图标
 */
QString AgentRepository::resolveIcon(const QString &raw)
{
    return core::IconResolver::resolve(raw,
                                       QStringLiteral("qrc:/icons/default.svg"));
}

// --- 随包默认配置 -------------------------------------------------------------

/**
 * @brief 读随包默认 agent 定义
 *
 * @return default_agents.json 解析出的定义列表；资源打不开返回空列表
 */
QList<AgentDefinition> AgentRepository::loadDefaults()
{
    QFile def(QStringLiteral(":/config/default_agents.json"));
    if (!def.open(QIODevice::ReadOnly)) {
        return {};
    }
    return AgentRepository(QString()).parse(def.readAll());
}

/**
 * @brief 取随包默认 agent 的 id 列表
 *
 * @return 依次对应 loadDefaults() 顺序的 id
 */
QStringList AgentRepository::defaultAgentIds()
{
    QStringList ids;
    const QList<AgentDefinition> defaults = loadDefaults();
    for (const AgentDefinition &a : defaults) {
        ids.append(a.id);
    }
    return ids;
}

/**
 * @brief 显示名转配置 id
 *
 * 小写、去标点、空白与下划线折叠成连字符；清掉首尾连字符后若为空
 * 则回退 "agent"。
 *
 * @param name agent 显示名
 * @return 可作 id 的 slug，如 "Kimi Code" -> "kimi-code"
 */
QString AgentRepository::slugFromName(const QString &name)
{
    QString s = name.toLower().trimmed();
    s.remove(QRegularExpression(QStringLiteral("[^a-z0-9\\s_-]")));
    s.replace(QRegularExpression(QStringLiteral("[\\s_]+")), QStringLiteral("-"));
    s.replace(QRegularExpression(QStringLiteral("-+")), QStringLiteral("-"));
    while (s.startsWith(QLatin1Char('-'))) {
        s.remove(0, 1);
    }
    while (s.endsWith(QLatin1Char('-'))) {
        s.chop(1);
    }
    if (s.isEmpty()) {
        s = QStringLiteral("agent");
    }
    return s;
}

} // namespace awb::agentcatalog
