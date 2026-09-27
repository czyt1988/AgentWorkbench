#include "tools/FileTreeModel.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace awb::tools {

/// watcher 事件防抖：编辑器场景下树不深，300 ms 足够合并连续写入
/// （构建目录里一秒内几十次增删也只触发一次重扫）。
namespace {
constexpr int kWatcherDebounceMs = 300;
} // namespace

/// 树节点。invisible root 也用 Node 表达（relativePath 为空串，永不对外暴露）。
struct FileTreeModel::Node
{
    QString name;          ///< 显示名
    QString path;          ///< 绝对路径（正斜杠）
    QString relativePath;  ///< 相对工作区根（正斜杠，根为空串）
    bool isDir = false;
    bool fetched = false;  ///< 目录的子项是否已读盘
    bool expandedInUi = false; ///< QML 上报的展开状态（refresh 快照的依据）
    int generation = 0;    ///< 创建时的重建代数，用于识别作废索引
    Node *parent = nullptr;
    int row = 0;
    std::vector<std::unique_ptr<Node>> children; ///< fetch 后才有内容
};

FileTreeModel::FileTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_watcherDebounce.setSingleShot(true);
    m_watcherDebounce.setInterval(kWatcherDebounceMs);
    // 只防抖 directoryChanged：fileChanged 需求（监听单文件改名）本模型没有。
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            &m_watcherDebounce, qOverload<>(&QTimer::start));
    connect(&m_watcherDebounce, &QTimer::timeout, this, [this]() { refresh(); });
}

FileTreeModel::~FileTreeModel() = default;

// --- QAbstractItemModel -----------------------------------------------------

int FileTreeModel::rowCount(const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    if (!node || !node->isDir)
        return 0;
    return static_cast<int>(node->children.size());
}

int FileTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QModelIndex FileTreeModel::index(int row, int column,
                                 const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    if (!node || column != 0 || row < 0 || row >= static_cast<int>(node->children.size()))
        return QModelIndex();
    return createIndex(row, column, node->children[static_cast<size_t>(row)].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex &child) const
{
    Node *node = nodeForIndex(child);
    if (!node || !node->parent)
        return QModelIndex();
    // invisible root 的直接子项的 parent 是无效索引；其余回到父节点的行。
    if (node->parent == m_root.get())
        return QModelIndex();
    return createIndex(node->parent->row, 0, node->parent);
}

QVariant FileTreeModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeForIndex(index);
    if (!node)
        return QVariant();
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return node->name;
    case PathRole:
        return node->path;
    case RelativePathRole:
        return node->relativePath;
    case IsDirRole:
        return node->isDir;
    case SuffixRole:
        return node->isDir ? QString() : QFileInfo(node->name).suffix().toLower();
    }
    return QVariant();
}

QHash<int, QByteArray> FileTreeModel::roleNames() const
{
    // 显式带上 display：覆盖 roleNames 后 TreeViewDelegate 的默认文本仍可用。
    return {
        {Qt::DisplayRole, "display"},
        {NameRole, "name"},
        {PathRole, "path"},
        {RelativePathRole, "relativePath"},
        {IsDirRole, "isDir"},
        {SuffixRole, "suffix"},
    };
}

bool FileTreeModel::hasChildren(const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    if (!node || !node->isDir)
        return false;
    // 未读取的目录一律显示展开箭头（读了之后可能是空的，箭头会自然消失）；
    // 这是懒加载树让 TreeView 提前显示箭头的唯一途径。
    if (!node->fetched)
        return true;
    return !node->children.empty();
}

bool FileTreeModel::canFetchMore(const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    return node && node->isDir && !node->fetched;
}

void FileTreeModel::fetchMore(const QModelIndex &parent)
{
    fetchNode(parent.isValid() ? nodeForIndex(parent) : m_root.get());
}

// --- public API ---------------------------------------------------------------

void FileTreeModel::setRootPath(const QString &path)
{
    ++m_generation;

    // 整棵 staging 树（含顶层一层）在 reset 之外构造好：reset 之后视图
    // 一次取到完整行数，不会再有 rowsInserted 追加。TreeView 对
    // 「modelReset 之后立刻插入的行」会重复计入，实测（2026-09 冒烟）。
    auto root = makeRootNode(path);
    if (root)
        populateNode(root.get());

    beginResetModel();
    m_rootPath = root ? root->path : QString();
    m_root = std::move(root);
    m_restoredExpandedPaths.clear();
    endResetModel();
    armWatchers();
    emit topLevelCountChanged();
}

void FileTreeModel::refresh()
{
    if (!m_root)
        return;

    // 快照要在构造新树之前收集：旧节点是展开状态的唯一来源。
    // 先序收集保证父路径总在子路径前面，恢复时逐层下钻即可。
    QStringList expanded;
    collectExpandedRelativePaths(m_root.get(), &expanded);

    ++m_generation;
    auto newRoot = makeRootNode(m_rootPath);
    if (!newRoot)
        return;
    populateNode(newRoot.get());

    // 恢复阶段全部在 staging 树上完成（无信号 fetch）；目录已消失
    // （被删/改名）时静默跳过。
    m_restoredExpandedPaths.clear();
    for (const QString &relativePath : expanded) {
        Node *node = findNodeInSubtree(newRoot.get(), relativePath, false);
        if (!node || !node->isDir)
            continue;
        populateNode(node);
        node->expandedInUi = true;
        m_restoredExpandedPaths.append(relativePath);
    }

    beginResetModel();
    m_root = std::move(newRoot);
    endResetModel();
    armWatchers();
    emit refreshed();
}

int FileTreeModel::topLevelCount() const
{
    return m_root ? static_cast<int>(m_root->children.size()) : 0;
}

QModelIndex FileTreeModel::indexByPath(const QString &relativePath)
{
    Node *node = nodeForRelativePath(relativePath);
    if (!node || node == m_root.get())
        return QModelIndex();
    return indexForNode(node);
}

void FileTreeModel::fetchChildren(const QModelIndex &parent)
{
    fetchNode(parent.isValid() ? nodeForIndex(parent) : m_root.get());
}

void FileTreeModel::setNodeExpanded(const QModelIndex &index, bool expanded)
{
    Node *node = nodeForIndex(index);
    if (!node || !node->isDir || node->generation != m_generation)
        return;
    node->expandedInUi = expanded;
    // 收起时子树的展开标记一并清掉：子目录本就不可见，留着会让下一次
    // refresh 把「已收起的子树」整条恢复出来。
    if (!expanded)
        clearExpandedBelow(node);
}

// --- private ------------------------------------------------------------------

std::unique_ptr<FileTreeModel::Node> FileTreeModel::makeRootNode(
        const QString &path) const
{
    if (path.isEmpty())
        return nullptr;
    // 空串经 QDir 也可能被拼成非空（相对当前目录），必须在这里归零，
    // 否则无工作区时会指向进程当前目录。
    auto root = std::make_unique<Node>();
    root->name = QFileInfo(path).fileName();
    root->path = QDir(path).absolutePath();
    root->isDir = true;
    root->generation = m_generation;
    return root;
}

FileTreeModel::Node *FileTreeModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    return static_cast<Node *>(index.internalPointer());
}

QModelIndex FileTreeModel::indexForNode(Node *node) const
{
    if (!node || !node->parent)
        return QModelIndex();
    return createIndex(node->row, 0, node);
}

std::vector<std::unique_ptr<FileTreeModel::Node>> FileTreeModel::readChildNodes(
        const Node *parent) const
{
    // 排除隐藏项（Unix 的点文件 / Windows 的隐藏属性），其余不过滤：
    // node_modules、.git 这类目录照常出现，开销由懒加载控制。
    QFileInfoList entries = QDir(parent->path).entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);
    // 目录在前 + 文件名大小写不敏感排序；同字母异大小写再按原序保证稳定。
    std::sort(entries.begin(), entries.end(),
              [](const QFileInfo &a, const QFileInfo &b) {
                  if (a.isDir() != b.isDir())
                      return a.isDir();
                  const int cmp = a.fileName().compare(b.fileName(), Qt::CaseInsensitive);
                  if (cmp != 0)
                      return cmp < 0;
                  return a.fileName().compare(b.fileName()) < 0;
              });

    std::vector<std::unique_ptr<Node>> children;
    children.reserve(entries.size());
    int row = 0;
    for (const QFileInfo &entry : entries) {
        auto child = std::make_unique<Node>();
        child->name = entry.fileName();
        child->path = entry.absoluteFilePath();
        child->relativePath = parent->relativePath.isEmpty()
                                  ? child->name
                                  : parent->relativePath + QLatin1Char('/') + child->name;
        child->isDir = entry.isDir();
        child->generation = m_generation;
        child->parent = const_cast<Node *>(parent);
        child->row = row;
        children.push_back(std::move(child));
        ++row;
    }
    return children;
}

void FileTreeModel::attachChildren(
        Node *node, std::vector<std::unique_ptr<Node>> children)
{
    node->children = std::move(children);
    node->fetched = true;
}

void FileTreeModel::populateNode(Node *node)
{
    if (!node || !node->isDir || node->fetched)
        return;
    attachChildren(node, readChildNodes(node));
}

void FileTreeModel::fetchNode(Node *node)
{
    if (!node || !node->isDir || node->fetched)
        return;

    // 先读盘构造，再 begin/attach/end：构造期不动模型结构，
    // beginInsertRows 声明的行数与 attach 的行数严格一致。
    auto children = readChildNodes(node);
    const int count = static_cast<int>(children.size());
    if (count > 0)
        beginInsertRows(indexForNode(node), 0, count - 1);
    attachChildren(node, std::move(children));
    if (count > 0)
        endInsertRows();
    if (node == m_root.get())
        emit topLevelCountChanged();
    armWatchers();
}

FileTreeModel::Node *FileTreeModel::findNodeInSubtree(Node *base,
                                                      const QString &relativePath,
                                                      bool withSignals)
{
    Node *current = base;
    const QStringList segments =
            relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        // 沿途逐层补 fetch：恢复深层路径时中间层必须已读取。
        if (!current->fetched) {
            if (withSignals)
                fetchNode(current);
            else
                populateNode(current);
        }
        Node *match = nullptr;
        for (std::unique_ptr<Node> &child : current->children) {
            // Windows 大小写不敏感；磁盘大小写可能与记忆的不一致。
            if (child->name == segment
                    || child->name.compare(segment, Qt::CaseInsensitive) == 0) {
                match = child.get();
                break;
            }
        }
        if (!match)
            return nullptr;
        current = match;
    }
    return current;
}

FileTreeModel::Node *FileTreeModel::nodeForRelativePath(
        const QString &relativePath)
{
    if (!m_root || relativePath.isEmpty())
        return m_root.get();
    return findNodeInSubtree(m_root.get(), relativePath, true);
}

void FileTreeModel::collectExpandedRelativePaths(const Node *node, QStringList *out) const
{
    if (node != m_root.get()) {
        if (!node->isDir || !node->expandedInUi)
            return;
        out->append(node->relativePath);
    }
    for (const std::unique_ptr<Node> &child : node->children)
        collectExpandedRelativePaths(child.get(), out);
}

void FileTreeModel::clearExpandedBelow(Node *node)
{
    for (std::unique_ptr<Node> &child : node->children) {
        child->expandedInUi = false;
        clearExpandedBelow(child.get());
    }
}

void FileTreeModel::collectWatchedDirs(const Node *node, QStringList *out) const
{
    if (!node->isDir || !node->fetched)
        return;
    out->append(node->path);
    for (const std::unique_ptr<Node> &child : node->children)
        collectWatchedDirs(child.get(), out);
}

void FileTreeModel::armWatchers()
{
    // 全量重布防：扫描后目录集合变了（新增/删除），旧列表不可复用。
    const QStringList current = m_watcher.directories();
    if (!current.isEmpty())
        m_watcher.removePaths(current);
    if (!m_root)
        return;

    QStringList watched;
    collectWatchedDirs(m_root.get(), &watched);
    if (watched.isEmpty())
        return;
    const QStringList failed = m_watcher.addPaths(watched);
    if (!failed.isEmpty())
        qWarning() << "[tools] filesystem watcher failed to watch:" << failed;
}

} // namespace awb::tools
