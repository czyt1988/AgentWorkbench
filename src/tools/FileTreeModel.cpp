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
    beginResetModel();
    m_rootPath = QDir(path).absolutePath();
    // 空串经 QDir 也可能被拼成非空（相对当前目录），这里强制归零。
    if (path.isEmpty())
        m_rootPath.clear();
    m_root.reset();
    rebuildRoot();
    endResetModel();
    if (m_root)
        fetchNode(m_root.get());
    armWatchers();
}

void FileTreeModel::refresh()
{
    if (!m_root)
        return;

    // 快照要在 reset 之前收集：reset 之后旧节点连同展开历史一起消失。
    // 先序收集保证父路径总在子路径前面，恢复时逐层下钻即可。
    QStringList fetched;
    collectFetchedRelativePaths(m_root.get(), &fetched);

    beginResetModel();
    m_root.reset();
    rebuildRoot();
    endResetModel();
    fetchNode(m_root.get());
    for (const QString &relativePath : fetched) {
        Node *node = nodeForRelativePath(relativePath);
        // nodeForRelativePath 只补读沿途中间层；目标目录本身要在这里补上，
        // 否则恢复出的索引 rowCount 为 0，展开状态等于丢了。
        if (node && node->isDir)
            fetchNode(node);
    }
    armWatchers();
    emit refreshed();
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

// --- private ------------------------------------------------------------------

void FileTreeModel::rebuildRoot()
{
    if (m_rootPath.isEmpty())
        return;
    auto root = std::make_unique<Node>();
    root->name = QFileInfo(m_rootPath).fileName();
    root->path = m_rootPath;
    root->isDir = true;
    m_root = std::move(root);
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

void FileTreeModel::fetchNode(Node *node)
{
    if (!node || !node->isDir || node->fetched)
        return;

    // 排除隐藏项（Unix 的点文件 / Windows 的隐藏属性），其余不过滤：
    // node_modules、.git 这类目录照常出现，开销由懒加载控制。
    QFileInfoList entries = QDir(node->path).entryInfoList(
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

    const int count = static_cast<int>(entries.size());
    if (count > 0)
        beginInsertRows(indexForNode(node), 0, count - 1);
    node->children.reserve(entries.size());
    int row = 0;
    for (const QFileInfo &entry : entries) {
        auto child = std::make_unique<Node>();
        child->name = entry.fileName();
        child->path = entry.absoluteFilePath();
        child->relativePath = node->relativePath.isEmpty()
                                  ? child->name
                                  : node->relativePath + QLatin1Char('/') + child->name;
        child->isDir = entry.isDir();
        child->parent = node;
        child->row = row;
        node->children.push_back(std::move(child));
        ++row;
    }
    node->fetched = true;
    if (count > 0)
        endInsertRows();
    armWatchers();
}

FileTreeModel::Node *FileTreeModel::nodeForRelativePath(const QString &relativePath)
{
    if (!m_root || relativePath.isEmpty())
        return m_root.get();

    Node *current = m_root.get();
    const QStringList segments = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        // 沿途逐层补 fetch：恢复深层路径时中间层必须已读取。
        if (!current->fetched)
            fetchNode(current);
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

void FileTreeModel::collectFetchedRelativePaths(const Node *node, QStringList *out) const
{
    if (node == m_root.get()) {
        for (const std::unique_ptr<Node> &child : node->children)
            collectFetchedRelativePaths(child.get(), out);
        return;
    }
    if (!node->isDir || !node->fetched)
        return;
    out->append(node->relativePath);
    for (const std::unique_ptr<Node> &child : node->children)
        collectFetchedRelativePaths(child.get(), out);
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
