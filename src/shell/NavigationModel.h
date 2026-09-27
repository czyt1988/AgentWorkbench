#ifndef AWB_SHELL_NAVIGATIONMODEL_H
#define AWB_SHELL_NAVIGATIONMODEL_H

#include "shell/PageDescriptor.h"

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>

namespace awb::shell {

// Page registry + list model behind the sidebar and the workspace host
// Duplicate ids are rejected with a warning —
// the shell never guesses which page the caller meant.
class NavigationModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString currentPageId READ currentPageId WRITE setCurrentPageId
               NOTIFY currentPageChanged)
    // Property (not just the invokable) so QML bindings like
    // Workspace's `nav.currentPage.source` evaluate AND re-evaluate when
    // the page changes — a bare invokable reads as a function reference.
    Q_PROPERTY(QVariantMap currentPage READ currentPage NOTIFY currentPageChanged)
    // {pageId: badgeText} for every page. NOTifiable so StatusBar can bind
    // `nav.badges["agents"]` — the `page(id)` invokable has no notify
    // signal, so a badge change never re-evaluated that binding.
    Q_PROPERTY(QVariantMap badges READ badges NOTIFY badgesChanged)

public:
    enum Roles {
        PageIdRole = Qt::UserRole + 1,
        TitleRole,
        IconRole,
        SourceRole,
        SectionRole,
        OrderRole,
        BadgeRole,
        EnabledRole
    };
    Q_ENUM(Roles)

    explicit NavigationModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // False when the id is already registered (logged, not fatal).
    bool registerPage(const PageDescriptor &page);
    bool unregisterPage(const QString &id);

    // Descriptor for one id; invalid id when unknown.
    Q_INVOKABLE QVariantMap page(const QString &id) const;
    // The current page's descriptor (source for the workspace Loader);
    // empty map when nothing is selected.
    Q_INVOKABLE QVariantMap currentPage() const;

    void setBadge(const QString &id, const QString &text);

    // Snapshot for the `badges` Q_PROPERTY (see above).
    QVariantMap badges() const;

    QString currentPageId() const { return m_currentId; }
    // Q_INVOKABLE: a bare Q_PROPERTY WRITE is not in the meta-object method
    // table — QML calling nav.setCurrentPageId(...) threw
    // "…is not a function" and sidebar/Ctrl+N page switching silently did
    // nothing. Property assignment (nav.currentPageId = x) still works too.
    Q_INVOKABLE void setCurrentPageId(const QString &id);

    // Pages in display order (section grouping + order field), for the
    // sidebar's Ctrl+1…9 shortcuts and repeaters.
    Q_INVOKABLE QStringList pageIdsInOrder() const;

    // How many pages a sidebar section holds / which model row starts it —
    // the sidebar draws its section dividers from these.
    Q_INVOKABLE int countInSection(const QString &section) const;
    Q_INVOKABLE int rowOfFirstInSection(const QString &section) const;

signals:
    void currentPageChanged();
    void pagesChanged();
    void badgesChanged();

private:
    int indexOfId(const QString &id) const;
    void sort();

    QList<PageDescriptor> m_pages;
    QString m_currentId;
};

} // namespace awb::shell

#endif // AWB_SHELL_NAVIGATIONMODEL_H
