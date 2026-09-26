#include "web/WebTabsModel.h"

namespace awb::web {

WebTabsModel::WebTabsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int WebTabsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_tabs.size();
}

QVariant WebTabsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tabs.size())
        return {};
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
    // The tab object itself — surfaces bind their `tab` property to it.
    case TabObjectRole:
        return QVariant::fromValue<QObject *>(
            static_cast<QObject *>(const_cast<WebTab *>(tab)));
    }
    return {};
}

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

bool WebTabsModel::removeTab(const QString &id)
{
    const int row = rowOfTab(id);
    if (row < 0)
        return false;
    beginRemoveRows(QModelIndex(), row, row);
    WebTab *tab = m_tabs.takeAt(row);
    endRemoveRows();
    if (m_activeIndex >= m_tabs.size())
        m_activeIndex = m_tabs.size() - 1;
    emit activeIndexChanged();
    tab->deleteLater();
    return true;
}

WebTab *WebTabsModel::tabById(const QString &id) const
{
    const int row = rowOfTab(id);
    return row < 0 ? nullptr : m_tabs.at(row);
}

WebTab *WebTabsModel::tabForAgent(const QString &agentId) const
{
    for (WebTab *tab : m_tabs) {
        if (tab->agentId() == agentId)
            return tab;
    }
    return nullptr;
}

WebTab *WebTabsModel::tabAt(int row) const
{
    return (row >= 0 && row < m_tabs.size()) ? m_tabs.at(row) : nullptr;
}

int WebTabsModel::rowOfTab(const QString &id) const
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs.at(i)->id() == id)
            return i;
    }
    return -1;
}

void WebTabsModel::setActiveIndex(int index)
{
    if (index < -1 || index >= m_tabs.size())
        return;
    if (m_activeIndex == index)
        return;
    m_activeIndex = index;
    if (index >= 0)
        m_tabs.at(index)->touch();
    emit activeIndexChanged();
}

QString WebTabsModel::activeTabId() const
{
    return m_activeIndex >= 0 && m_activeIndex < m_tabs.size()
        ? m_tabs.at(m_activeIndex)->id() : QString();
}

WebTab *WebTabsModel::activeTab() const
{
    return tabAt(m_activeIndex);
}

void WebTabsModel::notifyTabChanged(const QString &id)
{
    const int row = rowOfTab(id);
    if (row < 0)
        return;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx);
}

} // namespace awb::web
