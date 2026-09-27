#ifndef AWB_TOOLS_FILETREEMODEL_H
#define AWB_TOOLS_FILETREEMODEL_H

#include <QAbstractItemModel>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <vector>

namespace awb::tools {

/// Agent Tools 页右侧的文件树模型：对单个工作区目录做懒加载浏览。
///
/// 目录节点在 fetch 前只有箭头没有子行（hasChildren 恒真），展开时才读磁盘；
/// 内建 QFileSystemWatcher 监听已读取的目录，变更经防抖触发 refresh()。
/// refresh() 是**增量**的：逐目录与磁盘对账，只对真正变化的行发
/// rowsInserted / rowsRemoved，不 reset 模型，视图因此既不闪也不丢展开状态。
/// 对账算法与信号顺序见 FileTreeModel.cpp。
class FileTreeModel : public QAbstractItemModel
{
    Q_OBJECT

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
    /// 换根是整棵树换内容，走 model reset；顶层一层立即读盘，更深层保持懒加载。
    void setRootPath(const QString &path);

    QString rootPath() const { return m_rootPath; }

    /// 与磁盘对账：只改变化的行，展开状态与未变的节点原样保留。
    void refresh();

    int topLevelCount() const;

    /// 幂等的 fetch 兜底：目录未读取则读，其余情况静默返回。
    /// TreeView 展开未驱动 fetchMore 时由 QML 调用。
    Q_INVOKABLE void fetchChildren(const QModelIndex &parent);

signals:
    /// 一次 refresh 完成（手动或 watcher 触发）；无论有无变化都会发。
    void refreshed();
    /// 根层条目数变化（工作区切换、顶层增删）。
    void topLevelCountChanged();

private:
    struct Node;

    Node *nodeForIndex(const QModelIndex &index) const;
    QModelIndex indexForNode(Node *node) const;

    /// 读盘并排序（目录优先、名字大小写不敏感）；只列目录项，不建节点。
    static QFileInfoList readSortedEntries(const QString &dirPath);
    static std::unique_ptr<Node> makeChildNode(const QFileInfo &entry, Node *parent);
    static std::unique_ptr<Node> makeRootNode(const QString &path);
    /// 读盘构造 parent 的子节点数组（不挂树、不发信号）。
    static std::vector<std::unique_ptr<Node>> readChildNodes(Node *parent);
    /// 把子节点挂到 node 上（parent/row/fetched 落位），不带任何模型信号。
    static void attachChildren(Node *node,
                               std::vector<std::unique_ptr<Node>> children);
    /// 结构变化后重排行号；parent 指针由建节点时落位。
    static void renumberChildren(Node *node);
    /// 无信号版 fetch：构造期（staging 树）使用。
    void populateNode(Node *node);
    /// 活树上的 fetch：对视图发 rowsInserted。
    void fetchNode(Node *node);

    /// 对账一个已读取的目录并递归其已读取的子目录；返回整棵子树是否有变化。
    bool syncNode(Node *node);
    /// 单层对账：发最小的 remove/insert 行信号；返回本层是否有变化。
    bool syncChildren(Node *node);

    void collectWatchedDirs(const Node *node, QStringList *out) const;
    void armWatchers();

    /// 工作区根的绝对路径（正斜杠）；空串表示还没有工作区。
    QString m_rootPath;
    std::unique_ptr<Node> m_root;
    QFileSystemWatcher m_watcher;
    QTimer m_watcherDebounce;
};

} // namespace awb::tools

#endif // AWB_TOOLS_FILETREEMODEL_H
