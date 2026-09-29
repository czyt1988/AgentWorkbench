#ifndef AWB_SKILLS_SKILLMODEL_H
#define AWB_SKILLS_SKILLMODEL_H

#include "skillcatalog/SkillDefinition.h"

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QStringList>

namespace awb::skillcatalog {

// The skill list with client-side filtering, source facets and sorting.
// The full scan result is kept master-side so
// changing a filter never needs a rescan.
class SkillModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText
               NOTIFY filterChanged)
    // Selected kinds; empty = "All" (facets are multi-select with
    // "All" mutually exclusive — the page passes an empty set for All).
    Q_PROPERTY(QStringList activeKinds READ activeKinds WRITE setActiveKinds
               NOTIFY filterChanged)
    // name | modified | kind
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode
               NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)

public:
    enum Roles {
        SkillIdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        DirPathRole,
        SkillFileRole,
        RootIdRole,
        RootLabelRole,
        KindRole,
        PluginIdRole,
        PluginVersionRole,
        LastModifiedRole,
        SizeBytesRole,
        ExtrasRole
    };
    Q_ENUM(Roles)

    explicit SkillModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replace the master list (keeps filters applied).
    void setSkills(const QList<SkillDefinition> &skills);
    const QList<SkillDefinition> &allSkills() const { return m_all; }

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QStringList activeKinds() const { return m_activeKinds; }
    void setActiveKinds(const QStringList &kinds);
    QString sortMode() const { return m_sortMode; }
    void setSortMode(const QString &mode);

    int totalCount() const { return m_all.size(); }

    // The i18n-aware "%n skill(s) found" argument.
    Q_INVOKABLE int visibleCount() const { return rowCount(); }

Q_SIGNALS:
    void filterChanged();
    void countChanged();

private:
    void refilter();

    QList<SkillDefinition> m_all;
    QList<SkillDefinition> m_visible;
    QString m_searchText;
    QStringList m_activeKinds;
    QString m_sortMode = QStringLiteral("name");
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLMODEL_H
