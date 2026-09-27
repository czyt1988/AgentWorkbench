#ifndef AWB_TOOLS_FILETREEMODEL_H
#define AWB_TOOLS_FILETREEMODEL_H

#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <QTimer>

#include <memory>

namespace awb::tools {

/// Agent Tools 页右侧的文件树模型：对单个工作区目录做懒加载浏览。
///
/// 目录节点在 fetch 前只有箭头没有子行（hasChildren 恒真），展开时才读磁盘；
/// 内建 QFileSystemWatcher 监听已读取过的目录，变更经防抖触发 refresh()。
/// 刷新如何保住已展开状态、QML 如何配合，见 FileTreeModel.cpp 与 ToolsPage.qml。
class FileTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    /// role 名与顺序是对 QML 的契约，改动前先确认 ToolsPage.qml。
    enum Roles {
        NameRole = Qt::UserRole + 1, ///< 显示名（不含路径）
        PathRole,                    ///< 绝对路径（正斜杠）
        RelativePathRole,            ///< 相对工作区根的路径（正斜杠，无 ./ 前缀）
        IsDirRole,                   ///< 是否目录
        SuffixRole                   ///< 文件后缀（小写、无点；目录为空串）
    };
    Q_ENUM(Roles)

    explicit FileTreeModel(QObject *parent = nullptr);
    ~FileTreeModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    /// 设定树的内容根（工作区目录的绝对路径）；空串 = 无工作区的空树。
    /// 顶层一层会立即读盘，更深层保持懒加载。
    void setRootPath(const QString &path);

    QString rootPath() const { return m_rootPath; }

    /// 重扫整棵树，尽量恢复刷新前已读取过的节点（细节见 .cpp）。
    void refresh();

    /// 相对路径 → 模型索引；沿途逐层补 fetch，供 QML 在 refresh 后恢复展开。
    /// 相对路径分隔符固定为 '/'。找不到或路径为空时返回无效索引。
    Q_INVOKABLE QModelIndex indexByPath(const QString &relativePath);

    /// 幂等的 fetch 兜底：目录未读取则读，其余情况静默返回。
    /// TreeView 展开未驱动 fetchMore 时由 QML 调用。
    Q_INVOKABLE void fetchChildren(const QModelIndex &parent);

signals:
    /// 一次 refresh 完成（手动或 watcher 触发）；QML 据此恢复展开状态。
    void refreshed();

private:
    struct Node;

    Node *nodeForIndex(const QModelIndex &index) const;
    QModelIndex indexForNode(Node *node) const;
    void fetchNode(Node *node);
    Node *nodeForRelativePath(const QString &relativePath);
    void collectFetchedRelativePaths(const Node *node, QStringList *out) const;
    void collectWatchedDirs(const Node *node, QStringList *out) const;
    void armWatchers();
    void rebuildRoot();

    /// 工作区根的绝对路径（正斜杠）；空串表示还没有工作区。
    QString m_rootPath;
    std::unique_ptr<Node> m_root;
    QFileSystemWatcher m_watcher;
    QTimer m_watcherDebounce;
};

} // namespace awb::tools

#endif // AWB_TOOLS_FILETREEMODEL_H
