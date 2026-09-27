#ifndef AWB_TOOLS_FILETREEMODEL_H
#define AWB_TOOLS_FILETREEMODEL_H

#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <vector>

namespace awb::tools {

/// Agent Tools 页右侧的文件树模型：对单个工作区目录做懒加载浏览。
///
/// 目录节点在 fetch 前只有箭头没有子行（hasChildren 恒真），展开时才读磁盘；
/// 内建 QFileSystemWatcher 监听已展开的目录，变更经防抖触发 refresh()。
/// 刷新与懒加载如何区分信号、QML 如何配合，见 FileTreeModel.cpp 与 ToolsPage.qml。
class FileTreeModel : public QAbstractItemModel
{
    Q_OBJECT

    /// 上一次 refresh() 恢复出的「刷新前处于展开状态」的目录相对路径。
    /// QML 在 refreshed() 后据此逐层重新展开；只读快照，下次刷新覆盖。
    Q_PROPERTY(QStringList restoredExpandedPaths READ restoredExpandedPaths
               NOTIFY refreshed)
    /// 根层条目数；空状态占位用它判断（rowCount() 不参与 QML 绑定）。
    Q_PROPERTY(int topLevelCount READ topLevelCount NOTIFY topLevelCountChanged)

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
    /// 顶层一层立即读盘，更深层保持懒加载。
    void setRootPath(const QString &path);

    QString rootPath() const { return m_rootPath; }

    /// 重扫整棵树：快照展开状态 → 重建 → 逐个恢复（细节见 .cpp）。
    /// 恢复的路径经 restoredExpandedPaths 暴露，QML 负责实际的重新展开。
    void refresh();

    QStringList restoredExpandedPaths() const { return m_restoredExpandedPaths; }
    int topLevelCount() const;

    /// 相对路径 → 模型索引；沿途逐层补 fetch，供 QML 恢复展开。
    /// 相对路径分隔符固定为 '/'。找不到或路径为空时返回无效索引。
    Q_INVOKABLE QModelIndex indexByPath(const QString &relativePath);

    /// 幂等的 fetch 兜底：目录未读取则读，其余情况静默返回。
    /// TreeView 展开未驱动 fetchMore 时由 QML 调用。
    Q_INVOKABLE void fetchChildren(const QModelIndex &parent);

    /// QML 在 expanded/collapsed 信号里上报目录的展开状态；refresh() 依此快照。
    /// 索引属于已被重建作废的旧节点时静默忽略（视图在 reset 期间可能拿旧行触发）。
    Q_INVOKABLE void setNodeExpanded(const QModelIndex &index, bool expanded);

signals:
    /// 一次 refresh 完成（手动或 watcher 触发）；QML 据此恢复展开状态。
    void refreshed();
    void topLevelCountChanged();

private:
    struct Node;

    Node *nodeForIndex(const QModelIndex &index) const;
    QModelIndex indexForNode(Node *node) const;

    /// 读盘构造 parent 的子节点数组（不挂树、不发信号）。
    std::vector<std::unique_ptr<Node>> readChildNodes(const Node *parent) const;
    /// 把子节点挂到 node 上（parent/row/fetched 落位），不带任何模型信号。
    static void attachChildren(Node *node,
                               std::vector<std::unique_ptr<Node>> children);
    /// 无信号版 fetch：构造期（staging 树）使用。
    void populateNode(Node *node);
    /// 活树上的 fetch：对视图发 rowsInserted。
    void fetchNode(Node *node);
    /// 从 base 沿相对路径下钻，沿途按需 fetch；withSignals 区分活树/构造期。
    Node *findNodeInSubtree(Node *base, const QString &relativePath,
                            bool withSignals);
    Node *nodeForRelativePath(const QString &relativePath);

    void collectExpandedRelativePaths(const Node *node, QStringList *out) const;
    void clearExpandedBelow(Node *node);
    void collectWatchedDirs(const Node *node, QStringList *out) const;
    void armWatchers();
    std::unique_ptr<Node> makeRootNode(const QString &path) const;

    /// 工作区根的绝对路径（正斜杠）；空串表示还没有工作区。
    QString m_rootPath;
    std::unique_ptr<Node> m_root;
    /// 每次重建自增；节点携带创建时的代数，代数不符的索引一律视为作废
    /// （beginResetModel 期间视图可能仍拿旧行调用，旧指针已不可解引用）。
    int m_generation = 0;
    QStringList m_restoredExpandedPaths;
    QFileSystemWatcher m_watcher;
    QTimer m_watcherDebounce;
};

} // namespace awb::tools

#endif // AWB_TOOLS_FILETREEMODEL_H
