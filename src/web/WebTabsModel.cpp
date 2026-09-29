#include "web/WebTabsModel.h"

namespace awb::web {

/**
 * @brief 构造标签模型
 *
 * @param parent QObject 父项
 */
WebTabsModel::WebTabsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

/**
 * @brief 取顶层行数
 *
 * @param parent 父索引；标签是平铺列表，无层级，有效时恒为 0
 * @return 当前行数
 */
int WebTabsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_tabs.size();
}

/**
 * @brief 取某行某 role 的数据
 *
 * @param index 行索引；无效或越界时返回无效 QVariant
 * @param role Roles 里的值
 * @return 对应属性；TabObjectRole 返回 WebTab 的 QObject 指针
 */
QVariant WebTabsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tabs.size()) {
        return {};
    }
    const WebTab *tab = m_tabs.at(index.row());
    switch (role) {
    case TabIdRole: return tab->id();
    case AgentIdRole: return tab->agentId();
    case UrlRole: return tab->url();
    case TitleRole: return tab->title();
    case IconRole: return tab->iconSource();
    case ColorRole: return tab->color();
    case SurfaceKindRole: return tab->surfaceKind();
    case StateRole: return tab->state();
    case LoadProgressRole: return tab->loadProgress();
    case LastErrorRole: return tab->lastError();
    case ZoomRole: return tab->zoom();
    // 标签对象本身：表面把自己的 tab 属性绑到它，Q_PROPERTY 读取因此
    // 保持响应式（delegate 的 required property 无法直接持有对象）。
    case TabObjectRole:
        return QVariant::fromValue<QObject *>(
            static_cast<QObject *>(const_cast<WebTab *>(tab)));
    }
    return {};
}

/**
 * @brief 取 role 名表
 *
 * @return role 键 -> QML 侧的 role 名，名字与顺序是对 QML 的稳定契约
 */
QHash<int, QByteArray> WebTabsModel::roleNames() const
{
    return {
        { TabIdRole, "tabId" },
        { AgentIdRole, "agentId" },
        { UrlRole, "url" },
        { TitleRole, "title" },
        { IconRole, "iconSource" },
        { ColorRole, "color" },
        { SurfaceKindRole, "surfaceKind" },
        { StateRole, "state" },
        { LoadProgressRole, "loadProgress" },
        { LastErrorRole, "lastError" },
        { ZoomRole, "zoom" },
        { TabObjectRole, "tabObject" },
    };
}

/**
 * @brief 追加一个标签并接管所有权
 *
 * 同时把该标签的 state/title/loadProgress/url/lastError/zoom 变化接到
 * notifyTabChanged()——表面经 facade 回报的一切属性翻转都转成该行的
 * dataChanged，QML 的行内绑定因此保持新鲜。
 *
 * @param tab 待追加的标签，所有权移交本模型
 */
void WebTabsModel::appendTab(WebTab *tab)
{
    const int row = m_tabs.size();
    beginInsertRows(QModelIndex(), row, row);
    m_tabs.append(tab);
    endInsertRows();

    connect(tab, &WebTab::stateChanged, this, [this, tab]() {
        notifyTabChanged(tab->id());
    });
    connect(tab, &WebTab::titleChanged, this, [this, tab]() {
        notifyTabChanged(tab->id());
    });
    connect(tab, &WebTab::loadProgressChanged, this, [this, tab]() {
        notifyTabChanged(tab->id());
    });
    connect(tab, &WebTab::urlChanged, this, [this, tab]() {
        notifyTabChanged(tab->id());
    });
    connect(tab, &WebTab::lastErrorChanged, this, [this, tab]() {
        notifyTabChanged(tab->id());
    });
    connect(tab, &WebTab::zoomChanged, this, [this, tab]() {
        notifyTabChanged(tab->id());
    });
}

/**
 * @brief 按行号移除并删除一个标签
 *
 * 移除后维护激活下标：删的是激活行之前的行则前移一格，激活标签保持指向
 * 同一个对象；激活标签本身被删、或删完下标越界需要夹取时，发射
 * activeIndexChanged()。
 *
 * @param id 标签 id
 * @return 移除成功返回 true；id 未知时返回 false（模型不变）
 */
bool WebTabsModel::removeTab(const QString &id)
{
    const int row = rowOfTab(id);
    if (row < 0) {
        return false;
    }
    beginRemoveRows(QModelIndex(), row, row);
    WebTab *tab = m_tabs.takeAt(row);
    endRemoveRows();
    // 激活下标必须始终指向同一个标签对象：删除它之前的行会把后面的行
    // 整体前移一格；只有激活标签本身被删、或下标落到末尾之外时才需要
    // 夹取。删的恰是激活行时下标不动，但指向的已是后一个标签——同样
    // 是激活标签变化。
    const int previousActive = m_activeIndex;
    bool activeChanged = false;
    if (row < m_activeIndex) {
        --m_activeIndex;
    }
    else if (row == m_activeIndex) {
        activeChanged = true;
    }
    if (m_activeIndex >= m_tabs.size()) {
        m_activeIndex = m_tabs.size() - 1;
        activeChanged = true;
    }
    if (m_activeIndex != previousActive) {
        activeChanged = true;
    }
    if (activeChanged) {
        Q_EMIT activeIndexChanged();
    }
    tab->deleteLater();
    return true;
}

/**
 * @brief 按 id 查找标签
 *
 * @param id 标签 id
 * @return 对应的标签；不存在时返回 nullptr
 */
WebTab *WebTabsModel::tabById(const QString &id) const
{
    const int row = rowOfTab(id);
    return row < 0 ? nullptr : m_tabs.at(row);
}

/**
 * @brief 查找该 agent 当前打开的标签
 *
 * @param agentId agent id
 * @return 该 agent 的标签；未打开时返回 nullptr
 */
WebTab *WebTabsModel::tabForAgent(const QString &agentId) const
{
    for (WebTab *tab : m_tabs) {
        if (tab->agentId() == agentId) {
            return tab;
        }
    }
    return nullptr;
}

/**
 * @brief 按行号取标签
 *
 * @param row 行号
 * @return 该行的标签；越界（含空列表）返回 nullptr
 */
WebTab *WebTabsModel::tabAt(int row) const
{
    return (row >= 0 && row < m_tabs.size()) ? m_tabs.at(row) : nullptr;
}

/**
 * @brief 查找标签所在行号
 *
 * @param id 标签 id
 * @return 行号；不存在时返回 -1
 */
int WebTabsModel::rowOfTab(const QString &id) const
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs.at(i)->id() == id) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 设置激活行号
 *
 * 设为有效的下标时 touch() 该标签（LRU 释放策略把它算作刚用过）。
 * 越界或与当前相同则忽略，不发信号。
 *
 * @param index 行号；合法区间是 [-1, rowCount)，-1 表示清空激活
 */
void WebTabsModel::setActiveIndex(int index)
{
    if (index < -1 || index >= m_tabs.size()) {
        return;
    }
    if (m_activeIndex == index) {
        return;
    }
    m_activeIndex = index;
    if (index >= 0) {
        m_tabs.at(index)->touch();
    }
    Q_EMIT activeIndexChanged();
}

/**
 * @brief 取激活标签的 id
 *
 * @return 激活标签的 id；无激活标签（空列表或 -1）时返回空串
 */
QString WebTabsModel::activeTabId() const
{
    return m_activeIndex >= 0 && m_activeIndex < m_tabs.size()
        ? m_tabs.at(m_activeIndex)->id() : QString();
}

/**
 * @brief 取激活标签
 *
 * @return 激活的 WebTab；无激活标签时返回 nullptr
 * @sa setActiveIndex
 */
WebTab *WebTabsModel::activeTab() const
{
    return tabAt(m_activeIndex);
}

/**
 * @brief 为一个标签的行发 dataChanged
 *
 * WebTab 的某个属性翻转后调用，让 QML 重读该行的全部 role（不指定
 * role 列表，整行刷新）。标签不在模型里时无效果。
 *
 * @param id 标签 id
 */
void WebTabsModel::notifyTabChanged(const QString &id)
{
    const int row = rowOfTab(id);
    if (row < 0) {
        return;
    }
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx);
}

} // namespace awb::web
