#ifndef AWB_WEB_WEBTABSMODEL_H
#define AWB_WEB_WEBTABSMODEL_H

#include "web/WebTab.h"

#include <QAbstractListModel>
#include <QList>

namespace awb::web {

// The tab list model behind the tab bar.
// Surfaces report their progress/title back through the mutators below.
class WebTabsModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int activeIndex READ activeIndex WRITE setActiveIndex
               NOTIFY activeIndexChanged)
    Q_PROPERTY(QString activeTabId READ activeTabId NOTIFY activeIndexChanged)

public:
    enum Roles {
        TabIdRole = Qt::UserRole + 1,
        AgentIdRole,
        UrlRole,
        TitleRole,
        IconRole,
        ColorRole,
        SurfaceKindRole,
        StateRole,
        LoadProgressRole,
        LastErrorRole,
        ZoomRole,
        TabObjectRole
    };
    Q_ENUM(Roles)

    explicit WebTabsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Takes ownership of the tab.
    void appendTab(WebTab *tab);
    // Removes (and deletes) the tab; false when unknown.
    bool removeTab(const QString &id);

    WebTab *tabById(const QString &id) const;
    WebTab *tabForAgent(const QString &agentId) const;
    WebTab *tabAt(int row) const;
    int rowOfTab(const QString &id) const;

    int activeIndex() const { return m_activeIndex; }
    void setActiveIndex(int index);
    QString activeTabId() const;

    WebTab *activeTab() const;

    // Emit dataChanged for one tab (after a WebTab property flipped).
    void notifyTabChanged(const QString &id);

signals:
    void activeIndexChanged();

private:
    QList<WebTab *> m_tabs;
    int m_activeIndex = -1;
};

} // namespace awb::web

#endif // AWB_WEB_WEBTABSMODEL_H
