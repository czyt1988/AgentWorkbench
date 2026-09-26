#include "shell/Notifications.h"

namespace awb::shell {

Notifications::Notifications(QObject *parent)
    : QAbstractListModel(parent)
{
}

int Notifications::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_toasts.size();
}

QVariant Notifications::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_toasts.size())
        return {};
    const Toast &t = m_toasts.at(index.row());
    switch (role) {
    case IdRole:    return t.id;
    case LevelRole: return t.level;
    case TitleRole: return t.title;
    case TextRole:  return t.text;
    }
    return {};
}

QHash<int, QByteArray> Notifications::roleNames() const
{
    return {
        { IdRole, "toastId" },
        { LevelRole, "level" },
        { TitleRole, "title" },
        { TextRole, "text" },
    };
}

void Notifications::notify(const QString &level, const QString &title,
                           const QString &text)
{
    if (text.isEmpty() && title.isEmpty())
        return;
    const int row = m_toasts.size();
    beginInsertRows(QModelIndex(), row, row);
    Toast toast;
    toast.id = QString::number(m_nextId++);
    toast.level = level;
    toast.title = title;
    toast.text = text;
    m_toasts.append(toast);
    endInsertRows();
}

void Notifications::dismiss(const QString &id)
{
    for (int i = 0; i < m_toasts.size(); ++i) {
        if (m_toasts.at(i).id == id) {
            beginRemoveRows(QModelIndex(), i, i);
            m_toasts.removeAt(i);
            endRemoveRows();
            return;
        }
    }
}

int Notifications::durationFor(const QString &level) const
{
    if (level == QLatin1String("warning"))
        return 5000;
    if (level == QLatin1String("error"))
        return 8000;
    return 3000; // info / success
}

} // namespace awb::shell
