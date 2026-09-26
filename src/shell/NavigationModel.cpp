#include "shell/NavigationModel.h"

#include <QVariantMap>

#include <algorithm>

namespace awb::shell {

namespace {
// Sidebar sections in display order (02-ui-specification.md §3.1).
int sectionRank(const QString &section)
{
    if (section == QLatin1String("main"))
        return 0;
    if (section == QLatin1String("extensions"))
        return 1;
    return 2; // system
}
} // namespace

NavigationModel::NavigationModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NavigationModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_pages.size();
}

QVariant NavigationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_pages.size())
        return {};
    const PageDescriptor &p = m_pages.at(index.row());
    switch (role) {
    case PageIdRole: return p.id;
    case TitleRole:  return p.title;
    case IconRole:   return p.iconSource;
    case SourceRole: return p.source;
    case SectionRole: return p.section;
    case OrderRole:  return p.order;
    case BadgeRole:  return p.badgeText;
    case EnabledRole: return p.enabled;
    }
    return {};
}

QHash<int, QByteArray> NavigationModel::roleNames() const
{
    return {
        { PageIdRole, "pageId" },
        { TitleRole, "title" },
        { IconRole, "iconSource" },
        { SourceRole, "source" },
        { SectionRole, "section" },
        { OrderRole, "order" },
        { BadgeRole, "badgeText" },
        { EnabledRole, "enabled" },
    };
}

int NavigationModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pages.at(i).id == id)
            return i;
    }
    return -1;
}

void NavigationModel::sort()
{
    std::stable_sort(m_pages.begin(), m_pages.end(),
                     [](const PageDescriptor &a, const PageDescriptor &b) {
                         const int ra = sectionRank(a.section);
                         const int rb = sectionRank(b.section);
                         if (ra != rb)
                             return ra < rb;
                         return a.order < b.order;
                     });
}

bool NavigationModel::registerPage(const PageDescriptor &page)
{
    if (page.id.isEmpty()) {
        qWarning().noquote() << QStringLiteral(
            "NavigationModel: refusing to register a page without an id");
        return false;
    }
    if (indexOfId(page.id) >= 0) {
        qWarning().noquote() << QStringLiteral(
            "NavigationModel: page id \"%1\" is already registered; "
            "refusing the duplicate").arg(page.id);
        return false;
    }

    beginResetModel();
    m_pages.append(page);
    sort();
    endResetModel();
    emit pagesChanged();
    return true;
}

bool NavigationModel::unregisterPage(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    beginRemoveRows(QModelIndex(), row, row);
    m_pages.removeAt(row);
    endRemoveRows();
    if (m_currentId == id) {
        m_currentId.clear();
        emit currentPageChanged();
    }
    emit pagesChanged();
    return true;
}

QVariantMap NavigationModel::page(const QString &id) const
{
    QVariantMap map;
    const int row = indexOfId(id);
    if (row < 0)
        return map;
    const PageDescriptor &p = m_pages.at(row);
    map[QStringLiteral("id")] = p.id;
    map[QStringLiteral("title")] = p.title;
    map[QStringLiteral("iconSource")] = p.iconSource;
    map[QStringLiteral("source")] = p.source;
    map[QStringLiteral("section")] = p.section;
    map[QStringLiteral("order")] = p.order;
    map[QStringLiteral("badgeText")] = p.badgeText;
    map[QStringLiteral("enabled")] = p.enabled;
    return map;
}

QVariantMap NavigationModel::currentPage() const
{
    return page(m_currentId);
}

void NavigationModel::setBadge(const QString &id, const QString &text)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    if (m_pages[row].badgeText == text)
        return;
    m_pages[row].badgeText = text;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, { BadgeRole });
}

void NavigationModel::setCurrentPageId(const QString &id)
{
    if (id == m_currentId)
        return;
    if (!id.isEmpty() && indexOfId(id) < 0) {
        qWarning().noquote() << QStringLiteral(
            "NavigationModel: unknown page id \"%1\"; ignoring it").arg(id);
        return;
    }
    m_currentId = id;
    emit currentPageChanged();
}

QStringList NavigationModel::pageIdsInOrder() const
{
    QStringList ids;
    for (const PageDescriptor &p : m_pages) {
        if (p.enabled)
            ids.append(p.id);
    }
    return ids;
}

int NavigationModel::countInSection(const QString &section) const
{
    int count = 0;
    for (const PageDescriptor &p : m_pages) {
        if (p.section == section)
            ++count;
    }
    return count;
}

int NavigationModel::rowOfFirstInSection(const QString &section) const
{
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pages.at(i).section == section)
            return i;
    }
    return -1;
}

} // namespace awb::shell
