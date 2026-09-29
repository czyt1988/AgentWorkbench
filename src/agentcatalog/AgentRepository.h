#ifndef AWB_AGENTS_AGENTREPOSITORY_H
#define AWB_AGENTS_AGENTREPOSITORY_H

#include "agentcatalog/AgentDefinition.h"

#include <QString>
#include <QStringList>

namespace awb::agentcatalog {

/// agents.json 的读写与内置 agent 的同步语义（沿用 0.3.0）。
///
/// 数据根在构造时注入——模块内部没有全局状态。
class AgentRepository
{
public:
    explicit AgentRepository(const QString &dataRoot);

    // 读 agents.json，把随包默认重新套用到内置 agent 上，保留 removed
    // 列表，给没配色的用户 agent 分配色板颜色，并把由此产生的变化落盘
    void load();

    // 把当前定义与删除记录写回 <dataRoot>/agents.json
    //（经 core::JsonStore 原子写）
    bool save();

    // 当前的定义列表
    QList<AgentDefinition> definitions() const { return m_definitions; }
    void setDefinitions(const QList<AgentDefinition> &definitions)
    {
        m_definitions = definitions;
    }

    // 用户在设置页删除的内置 agent id；持久化为根级 "removed" 数组，
    // 删除状态因此跨重启保持
    QStringList removedIds() const { return m_removedIds; }
    void setRemovedIds(const QStringList &ids) { m_removedIds = ids; }

    // 清掉删除记录，把随包内置列表重新叠到 current 之上（用户自建
    // agent 保留各自定义与顺序），结果落盘
    bool restoreDefaults(const QList<AgentDefinition> &current);

    // id 是否属于随包的 default_agents.json
    bool isDefaultAgent(const QString &id) const;

    // 磁盘上 agents.json 的路径（错误提示里展示用）
    QString configFilePath() const;

    // 构造时注入的数据根
    QString dataRoot() const { return m_dataRoot; }

    // 随包默认 agent 定义（:/config/default_agents.json）
    static QList<AgentDefinition> loadDefaults();

    // 随包默认 agent 的 id 列表
    static QStringList defaultAgentIds();

    // 显示名转配置 id："Kimi Code" -> "kimi-code"
    static QString slugFromName(const QString &name);

    // 自动配色用的色板，由当前主题的 agentPalette 注入；为空时用
    // 内置 Mocha 色板
    void setAgentPalette(const QStringList &palette) { m_agentPalette = palette; }

    // 内置 Catppuccin Mocha 色板按位置取色（未注入主题色板时的回退）
    static QString paletteColorAt(int index);

    // 新建 agent 按位置取的色：优先注入的主题色板，回退内置 Mocha 色板
    QString paletteColorFor(int index) const;

    // 解析用于显示的图标串；应用级回退图标在这里给，core::IconResolver
    // 从不写死应用资源路径
    static QString resolveIcon(const QString &raw);

private:
    // 内置同步：内置 agent 一律来自随包默认、按随包顺序（跳过
    // removedIds），其后是用户自建 agent，保持其既有顺序
    static QList<AgentDefinition> withBuiltinDefaults(
        const QList<AgentDefinition> &current, const QStringList &removedIds);

    // 从 JSON 字节解析定义列表（顺带收集根级 removed id）
    QList<AgentDefinition> parse(const QByteArray &data);

    // 给 color 仍为空的 agent 分配色板颜色；有颜色被分配时返回 true
    //（调用方据此落盘）
    bool assignPaletteColors();

    QString m_dataRoot;                    ///< 数据根，agents.json 从它派生
    QList<AgentDefinition> m_definitions;  ///< 当前定义（内置在前、用户自建在后）
    QStringList m_removedIds;              ///< 被删除的内置 agent id
    QStringList m_agentPalette;            ///< 注入的主题色板，空 = 用内置 Mocha
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTREPOSITORY_H
