#include "shell/NavigationModel.h"

#include <QDebug>
#include <QVariantMap>

#include <algorithm>

namespace awb::shell {

// NavigationModel 持有 shell 的全部页面描述符，m_pages 始终维持显示顺序
// （节 main < extensions < system，节内按 order 字段，稳定排序保证同分
// 保持注册顺序）。注册/注销经模型重置或行删除广播给 QML；当前页只存 id，
// currentPage/currentPageId 两个属性都从它派生。
//
// 面向 QML 的快照方法（page()/badges()/keepAlivePages()）返回的是拷贝：
// QML 侧拿到的是普通 map/list，没有后续更新，靠各自的 NOTIFY 信号驱动
// 绑定重新求值。

namespace {

/**
 * @brief 侧栏分节的显示顺序值
 *
 * @param section 分节名（main / extensions / system）
 * @return 排序用的秩：main 0、extensions 1、system 2
 */
int sectionRank(const QString &section)
{
    if (section == QStringLiteral("main")) {
        return 0;
    }
    if (section == QStringLiteral("extensions")) {
        return 1;
    }
    return 2; // system
}
} // namespace

/**
 * @brief 构造页面模型
 *
 * @param parent QObject 父项
 */
NavigationModel::NavigationModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

/**
 * @brief 取列表行数
 *
 * @param parent 父索引；列表模型没有层级，有效时返回 0
 * @return 已注册页面数
 */
int NavigationModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_pages.size();
}

/**
 * @brief 按取值
 *
 * @param index 待查索引；无效或越界时返回空 QVariant
 * @param role  要取的 role（Roles 枚举）
 * @return 该 role 的值；未知 role 返回空 QVariant
 */
QVariant NavigationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_pages.size()) {
        return {};
    }
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

/**
 * @brief 取 role 名表
 *
 * 名字是对 QML delegate 的契约（required property 按 role 名匹配）。
 *
 * @return role id -> QML 侧属性名
 */
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

/**
 * @brief 查 id 在模型中的行号
 *
 * @param id 页面 id
 * @return 行号；未知 id 返回 -1
 */
int NavigationModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pages.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 重排 m_pages 为显示顺序
 *
 * 先按节（sectionRank），节内按 order 字段；stable_sort 保证同分的页
 * 保持注册顺序。调用方负责包 begin/endResetModel。
 */
void NavigationModel::sort()
{
    std::stable_sort(m_pages.begin(), m_pages.end(),
                     [](const PageDescriptor &a, const PageDescriptor &b) {
                         const int ra = sectionRank(a.section);
                         const int rb = sectionRank(b.section);
                         if (ra != rb) {
                             return ra < rb;
                         }
                         return a.order < b.order;
                     });
}

/**
 * @brief 注册一个页面
 *
 * id 为空或已注册时拒绝并记一条警告（不致命）。接受时重置整个模型
 * （插入位置由排序决定，无法表达为单行插入）并广播 pages/badges 两个
 * 属性变化。
 *
 * @param page 页面描述符
 * @return 注册成功返回 true
 */
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
    Q_EMIT pagesChanged();
    Q_EMIT badgesChanged(); // map 多了一个键
    return true;
}

/**
 * @brief 注销一个页面
 *
 * 从模型中移除该页；若它是当前页则同时清空当前选择并广播
 * currentPageChanged。未知 id 不做任何事。
 *
 * @param id 页面 id
 * @return 移除成功返回 true；未知 id 返回 false
 */
bool NavigationModel::unregisterPage(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0) {
        return false;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_pages.removeAt(row);
    endRemoveRows();
    if (m_currentId == id) {
        m_currentId.clear();
        Q_EMIT currentPageChanged();
    }
    Q_EMIT pagesChanged();
    Q_EMIT badgesChanged(); // map 少了一个键
    return true;
}

/**
 * @brief 取一个页面的描述符快照
 *
 * @param id 页面 id
 * @return 描述符的 QVariantMap 拷贝（键与 PageDescriptor 字段同名）；
 *         未知 id 返回空 map
 */
QVariantMap NavigationModel::page(const QString &id) const
{
    QVariantMap map;
    const int row = indexOfId(id);
    if (row < 0) {
        return map;
    }
    const PageDescriptor &p = m_pages.at(row);
    map[QStringLiteral("id")] = p.id;
    map[QStringLiteral("title")] = p.title;
    map[QStringLiteral("iconSource")] = p.iconSource;
    map[QStringLiteral("source")] = p.source;
    map[QStringLiteral("section")] = p.section;
    map[QStringLiteral("order")] = p.order;
    map[QStringLiteral("badgeText")] = p.badgeText;
    map[QStringLiteral("enabled")] = p.enabled;
    map[QStringLiteral("keepAlive")] = p.keepAlive;
    return map;
}

/**
 * @brief 取当前页的描述符快照
 *
 * @return page(m_currentId) 的结果；未选中时为空 map
 * @sa page
 */
QVariantMap NavigationModel::currentPage() const
{
    return page(m_currentId);
}

/**
 * @brief 取徽标快照
 *
 * @return {pageId: badgeText}；badgeText 为空串表示该页无徽标
 */
QVariantMap NavigationModel::badges() const
{
    QVariantMap map;
    for (const PageDescriptor &p : m_pages) {
        map.insert(p.id, p.badgeText);
    }
    return map;
}

/**
 * @brief 取常驻页描述符列表
 *
 * 只含 keepAlive 且 enabled 的页，供 Workspace 的常驻 Repeater 实例化。
 *
 * @return 与 page() 同构的快照，按显示顺序排列
 * @sa page
 */
QVariantList NavigationModel::keepAlivePages() const
{
    // 与 page() 同构的快照，按注册（排序后）顺序返回。跳过 disabled 页：
    // 无法经侧栏到达的页面不该占用一份常驻实例。
    QVariantList pages;
    for (const PageDescriptor &p : m_pages) {
        if (p.keepAlive && p.enabled) {
            pages.append(page(p.id));
        }
    }
    return pages;
}

/**
 * @brief 写一个页面的徽标文本
 *
 * 同值不重复广播；变化时更新 BadgeRole 并驱动 badges 属性重新求值。
 * 未知 id 静默忽略。
 *
 * @param id   页面 id
 * @param text 新徽标文本；空串表示无徽标
 */
void NavigationModel::setBadge(const QString &id, const QString &text)
{
    const int row = indexOfId(id);
    if (row < 0) {
        return;
    }
    if (m_pages[row].badgeText == text) {
        return;
    }
    m_pages[row].badgeText = text;
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx, { BadgeRole });
    // 驱动 `badges` Q_PROPERTY——StatusBar 绑定的是 nav.badges[id]
    Q_EMIT badgesChanged();
}

/**
 * @brief 切换当前页
 *
 * 同值直接返回；未知 id 记警告后忽略——绝不落到一个不存在的页面。
 * 空串表示清空选择。
 *
 * @param id 页面 id；空串清空当前页
 */
void NavigationModel::setCurrentPageId(const QString &id)
{
    if (id == m_currentId) {
        return;
    }
    if (!id.isEmpty() && indexOfId(id) < 0) {
        qWarning().noquote() << QStringLiteral(
            "NavigationModel: unknown page id \"%1\"; ignoring it").arg(id);
        return;
    }
    m_currentId = id;
    Q_EMIT currentPageChanged();
}

/**
 * @brief 按显示顺序返回启用的页 id
 *
 * @return 页 id 列表（跳过 disabled 页）；顺序与 m_pages 一致
 */
QStringList NavigationModel::pageIdsInOrder() const
{
    QStringList ids;
    for (const PageDescriptor &p : m_pages) {
        if (p.enabled) {
            ids.append(p.id);
        }
    }
    return ids;
}

/**
 * @brief 数一节含多少页
 *
 * @param section 分节名（main / extensions / system）
 * @return 该节的页数（含 disabled 页——分隔条按模型行画，不能跳行）
 */
int NavigationModel::countInSection(const QString &section) const
{
    int count = 0;
    for (const PageDescriptor &p : m_pages) {
        if (p.section == section) {
            ++count;
        }
    }
    return count;
}

/**
 * @brief 查一节首行在模型中的行号
 *
 * @param section 分节名
 * @return 行号；该节不存在时返回 -1
 */
int NavigationModel::rowOfFirstInSection(const QString &section) const
{
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pages.at(i).section == section) {
            return i;
        }
    }
    return -1;
}

} // namespace awb::shell
