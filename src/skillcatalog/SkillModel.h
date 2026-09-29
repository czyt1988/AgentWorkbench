#ifndef AWB_SKILLS_SKILLMODEL_H
#define AWB_SKILLS_SKILLMODEL_H

#include "skillcatalog/SkillDefinition.h"

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QStringList>

namespace awb::skillcatalog {

/// skill 列表模型：客户端侧的搜索过滤、来源 facet 多选与排序。
///
/// 完整扫描结果保留在主数据侧（m_all），改过滤条件只做内存重算，
/// 永远不需要重新扫描。
class SkillModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText
               NOTIFY filterChanged)
    // 选中的 kind 集合；空 = 「全部」（facet 是多选、「全部」与其互斥——
    // 页面为「全部」传空列表）。
    Q_PROPERTY(QStringList activeKinds READ activeKinds WRITE setActiveKinds
               NOTIFY filterChanged)
    // name | modified | kind
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode
               NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)

public:
    /// 列表模型的 role。名字与顺序是对 QML 的契约，改动前先确认所有页面。
    enum Roles {
        SkillIdRole = Qt::UserRole + 1, ///< skill 的稳定标识（SKILL.md 绝对路径）
        NameRole,                       ///< 显示名
        DescriptionRole,                ///< 描述
        DirPathRole,                    ///< skill 目录的绝对路径
        SkillFileRole,                  ///< SKILL.md 的绝对路径
        RootIdRole,                     ///< 所属扫描根 id
        RootLabelRole,                  ///< 所属扫描根的显示名
        KindRole,                       ///< 来源类别（agents/claude/codex/plugin/project/custom）
        PluginIdRole,                   ///< 插件 skill 的来源插件 id
        PluginVersionRole,              ///< 插件 skill 的来源插件版本
        LastModifiedRole,               ///< SKILL.md 的修改时刻
        SizeBytesRole,                  ///< SKILL.md 的字节数
        ExtrasRole                      ///< 其余 frontmatter 标量的映射
    };
    Q_ENUM(Roles)

    explicit SkillModel(QObject *parent = nullptr);

    // 可见行数；parent 有效时返回 0（列表模型没有子级）
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    // 按 role 取可见行里某个 skill 的字段；越界或未知 role 返回空 QVariant
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    // role 名表；名字是对 QML delegate 的稳定契约
    QHash<int, QByteArray> roleNames() const override;

    // 替换主列表（过滤条件保持生效）；内容与现状一致时短路跳过 reset
    void setSkills(const QList<SkillDefinition> &skills);
    // 完整扫描结果（过滤前的主数据）
    const QList<SkillDefinition> &allSkills() const { return m_all; }

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QStringList activeKinds() const { return m_activeKinds; }
    void setActiveKinds(const QStringList &kinds);
    QString sortMode() const { return m_sortMode; }
    void setSortMode(const QString &mode);

    // 过滤前的总数
    int totalCount() const { return m_all.size(); }

    // 过滤后的数量，作 i18n 感知的 "%n skill(s) found" 参数
    Q_INVOKABLE int visibleCount() const { return rowCount(); }

Q_SIGNALS:
    /**
     * @brief 任一过滤条件（searchText / activeKinds / sortMode）变化时发射
     */
    void filterChanged();

    /**
     * @brief 可见数量或总数量变化时发射
     */
    void countChanged();

private:
    // 按当前的过滤与排序把 m_all 重算进 m_visible，并 reset 模型
    void refilter();

    QList<SkillDefinition> m_all;      ///< 完整扫描结果（主数据）
    QList<SkillDefinition> m_visible;  ///< 过滤排序后实际展示的子集
    QString m_searchText;              ///< 搜索文本；空 = 不过滤
    QStringList m_activeKinds;         ///< 选中的 kind；空 = 全部
    QString m_sortMode = QStringLiteral("name"); ///< name | modified | kind
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLMODEL_H
