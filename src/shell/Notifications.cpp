#include "shell/Notifications.h"

namespace awb::shell {

// Notifications 是纯 append/remove 队列：计时与暂停逻辑全在 QML 的
// toast 栈里，C++ 侧只维护列表与 dismiss 的回调键。停留时长
// （durationFor）按级别固定，不走设置。

/**
 * @brief 构造 toast 队列
 *
 * @param parent QObject 父项
 */
Notifications::Notifications(QObject *parent)
    : QAbstractListModel(parent)
{
}

/**
 * @brief 取列表行数
 *
 * @param parent 父索引；列表模型没有层级，有效时返回 0
 * @return 队列中的 toast 数（含排队等候的）
 */
int Notifications::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_toasts.size();
}

/**
 * @brief 按 role 取值
 *
 * @param index 待查索引；无效或越界时返回空 QVariant
 * @param role  要取的 role（Roles 枚举）
 * @return 该 role 的值；未知 role 返回空 QVariant
 */
QVariant Notifications::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_toasts.size()) {
        return {};
    }
    const Toast &t = m_toasts.at(index.row());
    switch (role) {
    case IdRole:    return t.id;
    case LevelRole: return t.level;
    case TitleRole: return t.title;
    case TextRole:  return t.text;
    }
    return {};
}

/**
 * @brief 取 role 名表
 *
 * 名字是对 QML delegate 的契约，IdRole 叫 toastId 是为了避免与
 * delegate 根元素的 id 概念混淆。
 *
 * @return role id -> QML 侧属性名
 */
QHash<int, QByteArray> Notifications::roleNames() const
{
    return {
        { IdRole, "toastId" },
        { LevelRole, "level" },
        { TitleRole, "title" },
        { TextRole, "text" },
    };
}

/**
 * @brief 追加一条 toast
 *
 * 标题与正文都为空时直接忽略（空 toast 没有可显示的内容）。追加即入队，
 * 是否立即可见由 QML 栈按 kMaxVisible 决定。
 *
 * @param level 级别：info | success | warning | error
 * @param title 标题
 * @param text  正文
 */
void Notifications::notify(const QString &level, const QString &title,
                           const QString &text)
{
    if (text.isEmpty() && title.isEmpty()) {
        return;
    }
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

/**
 * @brief 按 id 移除一条 toast
 *
 * QML 侧计时器到点时回调。未知 id 静默忽略——重复 dismiss 与迟到回调
 * 都不该出事。
 *
 * @param id notify() 生成的字符串 id
 */
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

/**
 * @brief 级别对应的自动停留时长
 *
 * @param level 级别名
 * @return 毫秒：warning 5000、error 8000，其余（info/success 及未知值）
 *         一律 3000
 */
int Notifications::durationFor(const QString &level) const
{
    if (level == QStringLiteral("warning")) {
        return 5000;
    }
    if (level == QStringLiteral("error")) {
        return 8000;
    }
    return 3000; // info / success
}

} // namespace awb::shell
