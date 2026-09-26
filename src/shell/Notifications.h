#ifndef AWB_SHELL_NOTIFICATIONS_H
#define AWB_SHELL_NOTIFICATIONS_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace awb::shell {

// The non-blocking toast queue (01-architecture.md §4.7,
// 02-ui-specification.md §12): `notify(level, title, text)` appends a row,
// the QML stack shows the first three and dismisses them when their
// (hover-pausable) timer elapses — dismissal comes back through
// dismiss(id), so paused timers never drop a toast early.
class Notifications : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        LevelRole,
        TitleRole,
        TextRole
    };
    Q_ENUM(Roles)

    explicit Notifications(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // level ∈ info | success | warning | error.
    Q_INVOKABLE void notify(const QString &level, const QString &title,
                            const QString &text);
    Q_INVOKABLE void dismiss(const QString &id);

    // Automatic display time for a level (02 §12): info/success 3s,
    // warning 5s, error 8s.
    Q_INVOKABLE int durationFor(const QString &level) const;

    // Rows shown at once (QML reads it via toasts.maxVisible()).
    Q_INVOKABLE int maxVisible() const { return kMaxVisible; }

    // Rows visible at once; the rest queue up behind them (02 §12).
    static constexpr int kMaxVisible = 3;

private:
    struct Toast
    {
        QString id;
        QString level;
        QString title;
        QString text;
    };

    QList<Toast> m_toasts;
    int m_nextId = 1;
};

} // namespace awb::shell

#endif // AWB_SHELL_NOTIFICATIONS_H
