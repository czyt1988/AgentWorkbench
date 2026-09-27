#include "skills/SkillModel.h"

#include <QLocale> // (kept for future collator use)

#include <algorithm>

namespace awb::skills {

SkillModel::SkillModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SkillModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_visible.size();
}

QVariant SkillModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return {};
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

void SkillModel::setSkills(const QList<SkillDefinition> &skills)
{
    m_all = skills;
    refilter();
    emit countChanged();
}

void SkillModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    refilter();
    emit filterChanged();
    emit countChanged();
}

void SkillModel::setActiveKinds(const QStringList &kinds)
{
    if (m_activeKinds == kinds)
        return;
    m_activeKinds = kinds;
    refilter();
    emit filterChanged();
    emit countChanged();
}

void SkillModel::setSortMode(const QString &mode)
{
    if (m_sortMode == mode)
        return;
    m_sortMode = mode;
    refilter();
    emit filterChanged();
}

void SkillModel::refilter()
{
    QList<SkillDefinition> filtered;
    filtered.reserve(m_all.size());

    const QString needle = m_searchText.trimmed().toLower();
    const QSet<QString> kinds = QSet<QString>(m_activeKinds.begin(),
                                              m_activeKinds.end());

    for (const SkillDefinition &skill : m_all) {
        if (!kinds.isEmpty() && !kinds.contains(skill.kind))
            continue;
        if (!needle.isEmpty()) {
            const bool matches =
                skill.name.toLower().contains(needle)
                || skill.description.toLower().contains(needle)
                || skill.dirPath.toLower().contains(needle);
            if (!matches)
                continue;
        }
        filtered.append(skill);
    }

    // Sorting: name (locale-aware), most recently modified first,
    // or grouped by source kind then name.
    if (m_sortMode == QLatin1String("modified")) {
        std::sort(filtered.begin(), filtered.end(),
                  [](const SkillDefinition &a, const SkillDefinition &b) {
                      return a.lastModified > b.lastModified;
                  });
    } else if (m_sortMode == QLatin1String("kind")) {
        std::sort(filtered.begin(), filtered.end(),
                  [](const SkillDefinition &a, const SkillDefinition &b) {
                      if (a.kind != b.kind)
                          return a.kind < b.kind;
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

} // namespace awb::skills
