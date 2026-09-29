#ifndef AWB_WEB_WEBTABSMODEL_H
#define AWB_WEB_WEBTABSMODEL_H

#include "web/WebTab.h"

#include <QAbstractListModel>
#include <QList>

namespace awb::web {

/// 标签栏背后的标签列表模型（QAbstractListModel）。
///
/// 模型持有标签（appendTab 接管所有权）并负责把 WebTab 属性变化翻译成
/// dataChanged；表面经 facade 的 set* 方法回报状态，属性翻转后由
/// notifyTabChanged() 通知 QML 重读该行。
class WebTabsModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int activeIndex READ activeIndex WRITE setActiveIndex
               NOTIFY activeIndexChanged)
    Q_PROPERTY(QString activeTabId READ activeTabId NOTIFY activeIndexChanged)

public:
    /// 列表模型的 role。名字与顺序是对 QML 的契约，改动前先确认所有页面。
    enum Roles {
        TabIdRole = Qt::UserRole + 1, ///< 标签 id（"tab-<n>"）
        AgentIdRole,                  ///< 所属 agent 的 id
        UrlRole,                      ///< 当前 URL（QUrl）
        TitleRole,                    ///< 显示标题
        IconRole,                     ///< 图标（agent 定义）
        ColorRole,                    ///< agent 颜色（标签按钮底色）
        SurfaceKindRole,              ///< 呈现方式 kind
        StateRole,                    ///< 状态机当前值（loading | ready | …）
        LoadProgressRole,             ///< 加载进度 0..100
        LastErrorRole,                ///< 最近一次错误说明，无错误为空串
        ZoomRole,                     ///< 缩放系数
        TabObjectRole                 ///< WebTab 对象本身，表面把 tab 属性绑到它
    };
    Q_ENUM(Roles)

    explicit WebTabsModel(QObject *parent = nullptr);

    // 顶层行数（标签是平铺列表，无层级）；带有效 parent 时为 0
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 追加一个标签并接管所有权；同行的属性变化自动转成 dataChanged
    void appendTab(WebTab *tab);
    // 按行号移除并删除标签；id 未知时返回 false
    bool removeTab(const QString &id);

    // 按 id 找标签，找不到返回 nullptr
    WebTab *tabById(const QString &id) const;
    // 该 agent 当前打开的标签，一个 agent 最多一个，没有时为 nullptr
    WebTab *tabForAgent(const QString &agentId) const;
    // 按行号取标签，越界返回 nullptr
    WebTab *tabAt(int row) const;
    // 标签所在行号，找不到返回 -1
    int rowOfTab(const QString &id) const;

    // 激活标签的下标（-1 表示没有）；设为有效下标会 touch() 该标签
    int activeIndex() const { return m_activeIndex; }
    void setActiveIndex(int index);
    // 激活标签的 id，无激活标签时为空串
    QString activeTabId() const;

    WebTab *activeTab() const;

    // 某个 WebTab 的属性翻转后，为对应行发一条 dataChanged
    void notifyTabChanged(const QString &id);

Q_SIGNALS:
    /**
     * @brief 激活标签变化时发射
     *
     * activeIndex 的变化与 activeTabId（由同一索引派生）都靠它通知。
     * 设为有效的 index 时顺带 touch() 该标签（LRU 更新）。
     */
    void activeIndexChanged();

private:
    QList<WebTab *> m_tabs;  ///< 标签按行序排列，模型持有所有权
    int m_activeIndex = -1;  ///< 激活行号；-1 表示无激活（空列表）
};

} // namespace awb::web

#endif // AWB_WEB_WEBTABSMODEL_H
