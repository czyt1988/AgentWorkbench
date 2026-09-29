#include "tools/FileTreeModel.h"

#include <QDebug>
#include <QDir>
#include <QHash>
#include <QSet>

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

/// 刷新走增量对账而不是 reset 模型，原因有两层：
///
/// 1. TreeView 收到 modelReset 会把整棵扁平表推倒重建（所有 delegate 销毁重建，
///    展开状态清空），用户看到的就是「整棵树闪一下再重新展开」；
/// 2. 目录内容通常没变，reset 让没变的行也付一遍重建代价。
///
/// 所以 refresh() 只对**已读盘**的目录逐层与磁盘对账：同名同类型的旧节点原样复用
/// （子树、fetched 状态、视图里的展开状态全都保住），差异压缩成最少的
/// rowsRemoved / rowsInserted 区间发出去。没变化就一个信号都不发。
/// 只有 setRootPath()（换工作区 = 换整棵树的内容）才走 reset。
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
    if (!node || !node->isDir) {
        return 0;
    }
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
    if (!node || column != 0 || row < 0 || row >= static_cast<int>(node->children.size())) {
        return QModelIndex();
    }
    return createIndex(row, column, node->children[static_cast<size_t>(row)].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex &child) const
{
    Node *node = nodeForIndex(child);
    if (!node || !node->parent) {
        return QModelIndex();
    }
    // invisible root 的直接子项的 parent 是无效索引；其余回到父节点的行。
    if (node->parent == m_root.get()) {
        return QModelIndex();
    }
    return createIndex(node->parent->row, 0, node->parent);
}

QVariant FileTreeModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeForIndex(index);
    if (!node) {
        return QVariant();
    }
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
    case IconRole:
        return node->isDir ? m_icons.forFolder(node->name)
                           : m_icons.forFile(node->name);
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
        {IconRole, "iconSource"},
    };
}

bool FileTreeModel::hasChildren(const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    if (!node || !node->isDir) {
        return false;
    }
    // 未读取的目录一律显示展开箭头（读了之后可能是空的，箭头会自然消失）；
    // 这是懒加载树让 TreeView 提前显示箭头的唯一途径。
    if (!node->fetched) {
        return true;
    }
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
    // 整棵 staging 树（含顶层一层）在 reset 之外构造好：reset 之后视图
    // 一次取到完整行数，不会再有 rowsInserted 追加。TreeView 对
    // 「modelReset 之后立刻插入的行」会重复计入，实测（2026-09 冒烟）。
    auto root = makeRootNode(path);
    if (root) {
        populateNode(root.get());
    }

    beginResetModel();
    m_rootPath = root ? root->path : QString();
    m_root = std::move(root);
    endResetModel();
    armWatchers();
    Q_EMIT topLevelCountChanged();
}

/// 与磁盘对账，只对变化的行发信号。
///
/// 已读取的目录无论展开与否都会对账：收起但读过的目录里留着旧行，不更新的话
/// 下次展开就是过期数据。目录集合有增删时重新布防 watcher（新增的目录要等它
/// 被读取才会进监听列表，删除的目录要从列表里摘掉）。
void FileTreeModel::refresh()
{
    if (!m_root) {
        return;
    }

    const int topLevelBefore = topLevelCount();
    if (syncNode(m_root.get())) {
        armWatchers();
        if (topLevelCount() != topLevelBefore) {
            Q_EMIT topLevelCountChanged();
        }
    }
    Q_EMIT refreshed();
}

int FileTreeModel::topLevelCount() const
{
    return m_root ? static_cast<int>(m_root->children.size()) : 0;
}

void FileTreeModel::loadUserIconFile(const QString &path)
{
    m_icons.loadUserFile(path);
}

void FileTreeModel::fetchChildren(const QModelIndex &parent)
{
    fetchNode(parent.isValid() ? nodeForIndex(parent) : m_root.get());
}

// --- private ------------------------------------------------------------------

FileTreeModel::Node *FileTreeModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<Node *>(index.internalPointer());
}

QModelIndex FileTreeModel::indexForNode(Node *node) const
{
    if (!node || !node->parent) {
        return QModelIndex();
    }
    return createIndex(node->row, 0, node);
}

QFileInfoList FileTreeModel::readSortedEntries(const QString &dirPath)
{
    // 排除隐藏项（Unix 的点文件 / Windows 的隐藏属性），其余不过滤：
    // node_modules、.git 这类目录照常出现，开销由懒加载控制。
    QFileInfoList entries = QDir(dirPath).entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);
    // 目录在前 + 文件名大小写不敏感排序；同字母异大小写再按原序保证稳定。
    std::sort(entries.begin(), entries.end(),
              [](const QFileInfo &a, const QFileInfo &b) {
                  if (a.isDir() != b.isDir()) {
                      return a.isDir();
                  }
                  const int cmp = a.fileName().compare(b.fileName(), Qt::CaseInsensitive);
                  if (cmp != 0) {
                      return cmp < 0;
                  }
                  return a.fileName().compare(b.fileName()) < 0;
              });
    return entries;
}

std::unique_ptr<FileTreeModel::Node> FileTreeModel::makeChildNode(
        const QFileInfo &entry, Node *parent)
{
    auto child = std::make_unique<Node>();
    child->name = entry.fileName();
    child->path = entry.absoluteFilePath();
    child->relativePath = parent->relativePath.isEmpty()
                              ? child->name
                              : parent->relativePath + QLatin1Char('/') + child->name;
    child->isDir = entry.isDir();
    child->parent = parent;
    return child;
}

std::unique_ptr<FileTreeModel::Node> FileTreeModel::makeRootNode(const QString &path)
{
    if (path.isEmpty()) {
        return nullptr;
    }
    // 空串经 QDir 也可能被拼成非空（相对当前目录），必须在这里归零，
    // 否则无工作区时会指向进程当前目录。
    auto root = std::make_unique<Node>();
    root->name = QFileInfo(path).fileName();
    root->path = QDir(path).absolutePath();
    root->isDir = true;
    return root;
}

std::vector<std::unique_ptr<FileTreeModel::Node>> FileTreeModel::readChildNodes(
        Node *parent)
{
    const QFileInfoList entries = readSortedEntries(parent->path);
    std::vector<std::unique_ptr<Node>> children;
    children.reserve(entries.size());
    for (const QFileInfo &entry : entries) {
        children.push_back(makeChildNode(entry, parent));
    }
    return children;
}

void FileTreeModel::attachChildren(
        Node *node, std::vector<std::unique_ptr<Node>> children)
{
    node->children = std::move(children);
    node->fetched = true;
    renumberChildren(node);
}

void FileTreeModel::renumberChildren(Node *node)
{
    for (size_t row = 0; row < node->children.size(); ++row) {
        node->children[row]->row = static_cast<int>(row);
    }
}

void FileTreeModel::populateNode(Node *node)
{
    if (!node || !node->isDir || node->fetched) {
        return;
    }
    attachChildren(node, readChildNodes(node));
}

void FileTreeModel::fetchNode(Node *node)
{
    if (!node || !node->isDir || node->fetched) {
        return;
    }

    // 先读盘构造，再 begin/attach/end：构造期不动模型结构，
    // beginInsertRows 声明的行数与 attach 的行数严格一致。
    auto children = readChildNodes(node);
    const int count = static_cast<int>(children.size());
    if (count > 0) {
        beginInsertRows(indexForNode(node), 0, count - 1);
    }
    attachChildren(node, std::move(children));
    if (count > 0) {
        endInsertRows();
    }
    if (node == m_root.get()) {
        Q_EMIT topLevelCountChanged();
    }
    armWatchers();
}

bool FileTreeModel::syncNode(Node *node)
{
    if (!node->isDir || !node->fetched) {
        return false;
    }

    bool changed = syncChildren(node);
    for (const std::unique_ptr<Node> &child : node->children) {
        if (syncNode(child.get())) {
            changed = true;
        }
    }
    return changed;
}

/// 单层对账：把 node 的子项与磁盘列表对齐，差异压缩成最少的行信号。
///
/// 三步走，每步都严格夹在 begin/end 之间：
/// 1. 按名字匹配旧节点，得到目标序列 desired（幸存者用旧指针，新面孔现建）；
/// 2. 删除阶段从后往前逐段 erase，前面的行号在本阶段内始终有效；
/// 3. 插入阶段从前往后逐段 insert——此时 children 只剩幸存者，而两侧都按同一
///    比较器排过序，所以幸存者在 desired 与 children 里必然同序，一个游标就够。
/// 幸存者不动，它们的子树、fetched 状态与视图里的展开状态因此全部保留。
bool FileTreeModel::syncChildren(Node *node)
{
    const QFileInfoList entries = readSortedEntries(node->path);

    QHash<QString, int> oldRowByName;
    oldRowByName.reserve(static_cast<int>(node->children.size()));
    for (size_t row = 0; row < node->children.size(); ++row) {
        oldRowByName.insert(node->children[row]->name, static_cast<int>(row));
    }

    std::vector<Node *> desired;
    std::vector<std::unique_ptr<Node>> fresh;
    std::vector<bool> desiredIsFresh;
    QSet<const Node *> survivors;
    desired.reserve(entries.size());
    desiredIsFresh.reserve(entries.size());

    for (const QFileInfo &entry : entries) {
        Node *reused = nullptr;
        const auto found = oldRowByName.find(entry.fileName());
        if (found != oldRowByName.end()) {
            Node *candidate = node->children[static_cast<size_t>(found.value())].get();
            // 类型变了（文件被换成同名目录之类）按「删一个插一个」处理，
            // 否则会把目录的子行挂到文件上。
            if (candidate->isDir == entry.isDir()) {
                reused = candidate;
            }
            oldRowByName.erase(found);
        }
        if (reused) {
            desired.push_back(reused);
            desiredIsFresh.push_back(false);
            survivors.insert(reused);
            continue;
        }
        auto child = makeChildNode(entry, node);
        desired.push_back(child.get());
        desiredIsFresh.push_back(true);
        fresh.push_back(std::move(child));
    }

    if (fresh.empty() && survivors.size() == node->children.size()) {
        return false;
    }

    const QModelIndex parentIndex = indexForNode(node);

    for (int row = static_cast<int>(node->children.size()) - 1; row >= 0;) {
        if (survivors.contains(node->children[static_cast<size_t>(row)].get())) {
            --row;
            continue;
        }
        const int last = row;
        while (row >= 0
               && !survivors.contains(node->children[static_cast<size_t>(row)].get())) {
            --row;
        }
        const int first = row + 1;
        beginRemoveRows(parentIndex, first, last);
        node->children.erase(node->children.begin() + first,
                             node->children.begin() + last + 1);
        endRemoveRows();
    }

    size_t freshIndex = 0;
    size_t childRow = 0;
    size_t slot = 0;
    while (slot < desired.size()) {
        if (!desiredIsFresh[slot]) {
            ++slot;
            ++childRow;
            continue;
        }
        const size_t runStart = slot;
        while (slot < desired.size() && desiredIsFresh[slot]) {
            ++slot;
        }
        const size_t count = slot - runStart;
        beginInsertRows(parentIndex, static_cast<int>(childRow),
                        static_cast<int>(childRow + count - 1));
        for (size_t offset = 0; offset < count; ++offset) {
            node->children.insert(node->children.begin()
                                          + static_cast<long>(childRow + offset),
                                  std::move(fresh[freshIndex + offset]));
        }
        endInsertRows();
        freshIndex += count;
        childRow += count;
    }

    renumberChildren(node);
    return true;
}

void FileTreeModel::collectWatchedDirs(const Node *node, QStringList *out) const
{
    if (!node->isDir || !node->fetched) {
        return;
    }
    out->append(node->path);
    for (const std::unique_ptr<Node> &child : node->children) {
        collectWatchedDirs(child.get(), out);
    }
}

void FileTreeModel::armWatchers()
{
    // 全量重布防：扫描后目录集合变了（新增/删除），旧列表不可复用。
    const QStringList current = m_watcher.directories();
    if (!current.isEmpty()) {
        m_watcher.removePaths(current);
    }
    if (!m_root) {
        return;
    }

    QStringList watched;
    collectWatchedDirs(m_root.get(), &watched);
    if (watched.isEmpty()) {
        return;
    }
    const QStringList failed = m_watcher.addPaths(watched);
    if (!failed.isEmpty()) {
        qWarning() << "[tools] filesystem watcher failed to watch:" << failed;
    }
}

} // namespace awb::tools
