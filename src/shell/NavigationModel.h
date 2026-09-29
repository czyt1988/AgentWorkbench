#ifndef AWB_SHELL_NAVIGATIONMODEL_H
#define AWB_SHELL_NAVIGATIONMODEL_H

#include "shell/PageDescriptor.h"

#include <QAbstractListModel>
#include <QList>
#include <QVariantList>
#include <QVariantMap>

namespace awb::shell {

/// 页面注册表 + 侧栏与工作区宿主背后的列表模型。
///
/// 重复 id 一律记警告后拒绝——shell 绝不猜测调用方指的是哪个页面。
class NavigationModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString currentPageId READ currentPageId WRITE setCurrentPageId
               NOTIFY currentPageChanged)
    // 用属性而不只是 Q_INVOKABLE：Workspace 里 nav.currentPage.source 这类
    // 绑定才会在页面变化时重新求值——裸 invokable 在绑定里读出来是函数引用
    Q_PROPERTY(QVariantMap currentPage READ currentPage NOTIFY currentPageChanged)
    // {pageId: badgeText} 快照。NOTIFY 化是为了 StatusBar 能绑定
    // nav.badges["agents"]——page(id) 这个 invokable 没有 notify 信号，
    // 徽标变化时该绑定不会重新求值
    Q_PROPERTY(QVariantMap badges READ badges NOTIFY badgesChanged)
    // 描述符列表（与 page() 同构），只含 keepAlive 且 enabled 的页，
    // 供 Workspace 的常驻 Repeater 实例化。是 Q_PROPERTY 而非 Q_INVOKABLE：
    // 注册/注销（pagesChanged）时绑定需要自动重新求值。
    Q_PROPERTY(QVariantList keepAlivePages READ keepAlivePages NOTIFY pagesChanged)

public:
    /// 列表模型的 role。名字是对 QML delegate 的契约（required property
    /// 按 role 名匹配），改动前先确认所有页面。
    enum Roles {
        PageIdRole = Qt::UserRole + 1, ///< 页面 id（注册键）
        TitleRole,                     ///< 显示标题（已翻译的文本）
        IconRole,                      ///< 图标的 qrc URL
        SourceRole,                    ///< 页面 QML 的 source URL
        SectionRole,                   ///< 侧栏分节（main/extensions/system）
        OrderRole,                     ///< 节内排序键
        BadgeRole,                     ///< 侧栏徽标文本（空串 = 无徽标）
        EnabledRole                    ///< false 时侧栏不显示该页
    };
    Q_ENUM(Roles)

    explicit NavigationModel(QObject *parent = nullptr);

    // QAbstractListModel 实现：行数、按 role 取值、role 名表
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 注册一个页面；id 为空或已注册时拒绝（记警告，不致命）
    bool registerPage(const PageDescriptor &page);
    bool unregisterPage(const QString &id);

    // 一个 id 的描述符快照；未知 id 返回空 map
    Q_INVOKABLE QVariantMap page(const QString &id) const;
    // 当前页的描述符（工作区 Loader 的 source）；未选中时为空 map
    Q_INVOKABLE QVariantMap currentPage() const;

    void setBadge(const QString &id, const QString &text);

    // badges Q_PROPERTY 的快照（见上）
    QVariantMap badges() const;

    // keepAlivePages Q_PROPERTY 的快照（见上）
    QVariantList keepAlivePages() const;

    QString currentPageId() const { return m_currentId; }
    // Q_INVOKABLE 是硬要求：裸 Q_PROPERTY WRITE 不在 meta-object 方法表里，
    // QML 调 nav.setCurrentPageId(...) 会抛 "…is not a function"，侧栏与
    // Ctrl+N 切页曾因此静默失效。属性赋值（nav.currentPageId = x）仍可用
    Q_INVOKABLE void setCurrentPageId(const QString &id);

    // 按显示顺序（分节 + order 字段）返回页 id，供侧栏 Ctrl+1…9 快捷键
    // 与 Repeater 使用
    Q_INVOKABLE QStringList pageIdsInOrder() const;

    // 一节含多少页 / 该节首行在模型中的行号——侧栏按它们画分节分隔条
    Q_INVOKABLE int countInSection(const QString &section) const;
    Q_INVOKABLE int rowOfFirstInSection(const QString &section) const;

Q_SIGNALS:
    /**
     * @brief 当前页变化（含切换与清空）时发射
     */
    void currentPageChanged();

    /**
     * @brief 页面注册或注销时发射
     */
    void pagesChanged();

    /**
     * @brief 任一徽标变化时发射（含注册/注销带来的键增删）
     */
    void badgesChanged();

private:
    // id 在 m_pages 中的行号；未知 id 返回 -1
    int indexOfId(const QString &id) const;
    // 重排 m_pages：先按节（main < extensions < system），节内按 order
    void sort();

    QList<PageDescriptor> m_pages;  ///< 全部已注册页，始终维持显示顺序
    QString m_currentId;            ///< 当前页 id；空串 = 未选中
};

} // namespace awb::shell

#endif // AWB_SHELL_NAVIGATIONMODEL_H
