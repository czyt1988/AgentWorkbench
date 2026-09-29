#ifndef AWB_SHELL_NOTIFICATIONS_H
#define AWB_SHELL_NOTIFICATIONS_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace awb::shell {

/// 非阻塞 toast 队列：notify(level, title, text) 追加一行，QML 栈只显示
/// 前三条，各自的可暂停计时器到点后经 dismiss(id) 回收——暂停的计时器
/// 因此不会提前丢掉 toast。
class Notifications : public QAbstractListModel
{
    Q_OBJECT

public:
    /// 列表模型的 role。名字是对 QML delegate 的契约，改动前先确认
    /// ToastStack.qml。
    enum Roles {
        IdRole = Qt::UserRole + 1, ///< toast 的字符串 id（dismiss 的回调键）
        LevelRole,                 ///< 级别：info | success | warning | error
        TitleRole,                 ///< 标题
        TextRole                   ///< 正文
    };
    Q_ENUM(Roles)

    explicit Notifications(QObject *parent = nullptr);

    // QAbstractListModel 实现：行数、按 role 取值、role 名表
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 追加一条 toast；level ∈ info | success | warning | error
    Q_INVOKABLE void notify(const QString &level, const QString &title,
                            const QString &text);
    // 按 id 移除一条 toast（QML 计时器到点时回调）；未知 id 忽略
    Q_INVOKABLE void dismiss(const QString &id);

    // 级别对应的自动停留时长：info/success 3 s、warning 5 s、error 8 s
    Q_INVOKABLE int durationFor(const QString &level) const;

    // 同时可见的行数（QML 经 toasts.maxVisible() 读）
    Q_INVOKABLE int maxVisible() const { return kMaxVisible; }

    // 同时可见的行数；其余排队等候
    static constexpr int kMaxVisible = 3;

private:
    /// 队列里的一条 toast。
    struct Toast
    {
        QString id;    ///< dismiss 用的回调键
        QString level; ///< info | success | warning | error
        QString title; ///< 标题
        QString text;  ///< 正文
    };

    QList<Toast> m_toasts;  ///< 全部 toast，含 kMaxVisible 之外排队的
    int m_nextId = 1;       ///< 下一个 toast 的数字 id，只增不减
};

} // namespace awb::shell

#endif // AWB_SHELL_NOTIFICATIONS_H
