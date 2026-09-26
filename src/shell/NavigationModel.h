#ifndef AWB_SHELL_NAVIGATIONMODEL_H
#define AWB_SHELL_NAVIGATIONMODEL_H

#include "shell/PageDescriptor.h"

#include <QAbstractListModel>
#include <QList>

namespace awb::shell {

// Page registry + list model behind the sidebar and the workspace host
// (01-architecture.md §4.7). Duplicate ids are rejected with a warning —
// the shell never guesses which page the caller meant.
class NavigationModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString currentPageId READ currentPageId WRITE setCurrentPageId
               NOTIFY currentPageChanged)

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

    QString currentPageId() const { return m_currentId; }
    void setCurrentPageId(const QString &id);

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

private:
    int indexOfId(const QString &id) const;
    void sort();

    QList<PageDescriptor> m_pages;
    QString m_currentId;
};

} // namespace awb::shell

#endif // AWB_SHELL_NAVIGATIONMODEL_H
