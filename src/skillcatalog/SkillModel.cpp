#include "skillcatalog/SkillModel.h"

#include <QLocale> // 为将来接入 collator 预留（当前排序走 localeAwareCompare）

#include <algorithm>
#include <utility>

namespace awb::skillcatalog {

// SkillModel 的过滤全部在内存里做：m_all 是唯一主数据，m_visible 是过滤
// 排序后的投影，任何条件变化都重建投影（modelReset），代价远低于重扫
// 磁盘。role 名与顺序是 QML delegate 的契约，见 roleNames()。

/**
 * @brief 构造模型
 *
 * @param parent QObject 父项
 */
SkillModel::SkillModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

/**
 * @brief 取可见行数
 *
 * @param parent 父索引；列表模型没有子级，有效时返回 0
 * @return m_visible 的大小
 */
int SkillModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_visible.size();
}

/**
 * @brief 按 role 取可见行里某个 skill 的字段
 *
 * @param index 行索引
 * @param role Roles 里的 role id
 * @return 字段值；索引无效、行越界或 role 未知时返回空 QVariant
 */
QVariant SkillModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) {
        return {};
    }
    const SkillDefinition &skill = m_visible.at(index.row());
    switch (role) {
    case SkillIdRole: return skill.skillFilePath;
    case NameRole: return skill.name;
    case DescriptionRole: return skill.description;
    case DirPathRole: return skill.dirPath;
    case SkillFileRole: return skill.skillFilePath;
    case RootIdRole: return skill.rootId;
    case RootLabelRole: return skill.rootLabel;
    case KindRole: return skill.kind;
    case PluginIdRole: return skill.pluginId;
    case PluginVersionRole: return skill.pluginVersion;
    case LastModifiedRole: return skill.lastModified;
    case SizeBytesRole: return skill.sizeBytes;
    case ExtrasRole: return skill.extras;
    }
    return {};
}

/**
 * @brief 取 role 名表
 *
 * 名字与顺序是对 QML delegate 的稳定契约，改动前先确认所有页面。
 *
 * @return role id -> QML 侧的属性名
 */
QHash<int, QByteArray> SkillModel::roleNames() const
{
    return {
        { SkillIdRole, "skillId" },
        { NameRole, "name" },
        { DescriptionRole, "description" },
        { DirPathRole, "dirPath" },
        { SkillFileRole, "skillFilePath" },
        { RootIdRole, "rootId" },
        { RootLabelRole, "rootLabel" },
        { KindRole, "kind" },
        { PluginIdRole, "pluginId" },
        { PluginVersionRole, "pluginVersion" },
        { LastModifiedRole, "lastModified" },
        { SizeBytesRole, "sizeBytes" },
        { ExtrasRole, "extras" },
    };
}

/**
 * @brief 替换主列表并按当前条件重新过滤
 *
 * @param skills 最新扫描结果（或缓存恢复的旧结果）
 */
void SkillModel::setSkills(const QList<SkillDefinition> &skills)
{
    // 扫描结果与现状一致（启动后台扫描 vs 缓存恢复的典型情形）时直接
    // 短路：一次 modelReset 会让 QML 销毁并重建整页卡片（151 个 skill
    // 实测约 2 s 的 GUI 冻结），而内容没有任何变化。只有真变化才走
    // refilter + reset 的重建路径。
    if (m_all == skills) {
        return;
    }
    m_all = skills;
    refilter();
    Q_EMIT countChanged();
}

/**
 * @brief 设置搜索文本
 *
 * 值未变化时短路，不发信号。纯空白文本在 refilter 里 trim 后等同空串
 * （不过滤）。
 *
 * @param text 新的搜索文本
 */
void SkillModel::setSearchText(const QString &text)
{
    if (m_searchText == text) {
        return;
    }
    m_searchText = text;
    refilter();
    Q_EMIT filterChanged();
    Q_EMIT countChanged();
}

/**
 * @brief 设置选中的 kind 集合
 *
 * 空列表 = 「全部」。值未变化时短路，不发信号。
 *
 * @param kinds 新选中的 kind 列表
 */
void SkillModel::setActiveKinds(const QStringList &kinds)
{
    if (m_activeKinds == kinds) {
        return;
    }
    m_activeKinds = kinds;
    refilter();
    Q_EMIT filterChanged();
    Q_EMIT countChanged();
}

/**
 * @brief 设置排序模式
 *
 * 值未变化时短路；排序变化只发 filterChanged（数量不变）。
 *
 * @param mode name | modified | kind；未知值按 name 处理
 */
void SkillModel::setSortMode(const QString &mode)
{
    if (m_sortMode == mode) {
        return;
    }
    m_sortMode = mode;
    refilter();
    Q_EMIT filterChanged();
}

/**
 * @brief 按当前的过滤与排序重算可见列表
 *
 * 搜索文本对 name / description / dirPath 做大小写不敏感的包含匹配；
 * kind 过滤空集 = 全部。重算结果整体替换 m_visible，经
 * beginResetModel / endResetModel 通知视图销毁重建 delegate。
 */
void SkillModel::refilter()
{
    QList<SkillDefinition> filtered;
    filtered.reserve(m_all.size());

    const QString needle = m_searchText.trimmed().toLower();
    const QSet<QString> kinds = QSet<QString>(m_activeKinds.begin(),
                                              m_activeKinds.end());

    for (const SkillDefinition &skill : std::as_const(m_all)) {
        if (!kinds.isEmpty() && !kinds.contains(skill.kind)) {
            continue;
        }
        if (!needle.isEmpty()) {
            const bool matches =
                skill.name.toLower().contains(needle)
                || skill.description.toLower().contains(needle)
                || skill.dirPath.toLower().contains(needle);
            if (!matches) {
                continue;
            }
        }
        filtered.append(skill);
    }

    // 排序：name（locale 感知）、最近修改在前、或先按来源 kind 分组再按
    // name 排。
    if (m_sortMode == QStringLiteral("modified")) {
        std::sort(filtered.begin(), filtered.end(),
                  [](const SkillDefinition &a, const SkillDefinition &b) {
                      return a.lastModified > b.lastModified;
                  });
    } else if (m_sortMode == QStringLiteral("kind")) {
        std::sort(filtered.begin(), filtered.end(),
                  [](const SkillDefinition &a, const SkillDefinition &b) {
                      if (a.kind != b.kind) {
                          return a.kind < b.kind;
                      }
                      return QString::localeAwareCompare(a.name, b.name) < 0;
                  });
    } else {
        std::sort(filtered.begin(), filtered.end(),
                  [](const SkillDefinition &a, const SkillDefinition &b) {
                      return QString::localeAwareCompare(a.name, b.name) < 0;
                  });
    }

    beginResetModel();
    m_visible = filtered;
    endResetModel();
}

} // namespace awb::skillcatalog
