#include "tools/FileTreeModel.h"

#include <QDebug>
#include <QDir>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace awb::tools {

// 刷新走增量对账而不是 reset 模型，原因有两层：
//
// 1. TreeView 收到 modelReset 会把整棵扁平表推倒重建（所有 delegate 销毁重建，
//    展开状态清空），用户看到的就是「整棵树闪一下再重新展开」；
// 2. 目录内容通常没变，reset 让没变的行也付一遍重建代价。
//
// 所以 refresh() 只对**已读盘**的目录逐层与磁盘对账：同名同类型的旧节点原样复用
// （子树、fetched 状态、视图里的展开状态全都保住），差异压缩成最少的
// rowsRemoved / rowsInserted 区间发出去。没变化就一个信号都不发。
// 只有 setRootPath()（换工作区 = 换整棵树的内容）才走 reset。

namespace {

/**
 * @brief watcher 事件的防抖间隔（毫秒）
 *
 * 编辑器场景下树不深，300 ms 足够合并连续写入——构建目录里一秒内
 * 几十次增删也只触发一次重扫。
 */
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

/**
 * @brief 构造文件树模型
 *
 * 装好 watcher → 防抖定时器 → refresh() 的转发链。
 *
 * @param parent QObject 父项
 */
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

/**
 * @brief 析构文件树模型
 *
 * 节点树由 unique_ptr 自上而下自动递归释放，无手工清理。
 */
FileTreeModel::~FileTreeModel() = default;

// --- QAbstractItemModel -----------------------------------------------------

/**
 * @brief 取 parent 的子行数
 *
 * @param parent 目录索引；无效索引指 invisible root
 * @return 目录的子节点数；文件节点或空树返回 0
 */
int FileTreeModel::rowCount(const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    if (!node || !node->isDir) {
        return 0;
    }
    return static_cast<int>(node->children.size());
}

/**
 * @brief 列数（恒为 1，树视图的列契约）
 *
 * @param parent 未使用
 * @return 1
 */
int FileTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

/**
 * @brief 取 parent 下第 row 行、第 column 列的索引
 *
 * @param row 行号（从 0 起）
 * @param column 列号；非 0 一律返回无效索引
 * @param parent 目录索引；无效索引指 invisible root
 * @return 指向对应子节点的索引；越界或列号非 0 时返回无效索引
 */
QModelIndex FileTreeModel::index(int row, int column,
                                 const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    if (!node || column != 0 || row < 0 || row >= static_cast<int>(node->children.size())) {
        return QModelIndex();
    }
    return createIndex(row, column, node->children[static_cast<size_t>(row)].get());
}

/**
 * @brief 取 child 的父索引
 *
 * invisible root 的直接子项（顶层条目）返回无效索引，其余回到父节点
 * 所在行。
 *
 * @param child 子项索引
 * @return 父节点索引；顶层条目与无效索引返回无效索引
 */
QModelIndex FileTreeModel::parent(const QModelIndex &child) const
{
    Node *node = nodeForIndex(child);
    if (!node || !node->parent) {
        return QModelIndex();
    }
    if (node->parent == m_root.get()) {
        return QModelIndex();
    }
    return createIndex(node->parent->row, 0, node->parent);
}

/**
 * @brief 按 role 派发节点数据
 *
 * @param index 节点索引
 * @param role 请求的 role（Qt::DisplayRole 等同 NameRole）
 * @return 对应数据；索引无效或 role 不认识时返回无效 QVariant
 */
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

/**
 * @brief role 名表：role 号 → QML 端的属性名
 *
 * 名字与顺序是对 QML delegate 的契约（required property 按名字匹配），
 * 改动前先确认 ToolsPage.qml。
 *
 * @return role 号到 role 名的映射
 */
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

/**
 * @brief parent 是否有子行
 *
 * 未读取的目录一律返回 true（视图据此显示展开箭头，读了之后若是空的，
 * 箭头随行数自然消失）；这是懒加载树让视图提前显示箭头的唯一途径。
 *
 * @param parent 目录索引；无效索引指 invisible root
 * @return 目录且（未读或确有子项）时返回 true；文件返回 false
 */
bool FileTreeModel::hasChildren(const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    if (!node || !node->isDir) {
        return false;
    }
    if (!node->fetched) {
        return true;
    }
    return !node->children.empty();
}

/**
 * @brief 目录尚未读盘时是否还可 fetch
 *
 * @param parent 目录索引；无效索引指 invisible root
 * @return 目录且未读取时返回 true
 */
bool FileTreeModel::canFetchMore(const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    return node && node->isDir && !node->fetched;
}

/**
 * @brief 首次读取目录内容（视图展开时驱动）
 *
 * @param parent 目录索引；无效索引指 invisible root
 */
void FileTreeModel::fetchMore(const QModelIndex &parent)
{
    fetchNode(parent.isValid() ? nodeForIndex(parent) : m_root.get());
}

// --- public API ---------------------------------------------------------------

/**
 * @brief 换工作区根：整棵树换内容，走 model reset
 *
 * 顶层一层在 reset 之外先读好盘，reset 之后视图一次取到完整行数——
 * 不再有 reset 后追加的 rowsInserted。TreeView 对「modelReset 之后
 * 立刻插入的行」会重复计入（2026-09 冒烟实测）。换根后重布防 watcher。
 *
 * @param path 工作区目录的绝对路径；空串清成无工作区的空树
 */
void FileTreeModel::setRootPath(const QString &path)
{
    // 整棵 staging 树（含顶层一层）在 reset 之外构造好。
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

/**
 * @brief 与磁盘对账，只对变化的行发信号
 *
 * 已读取的目录无论展开与否都会对账：收起但读过的目录里留着旧行，
 * 不更新的话下次展开就是过期数据。目录集合有增删时重新布防 watcher
 * （新增的目录要等它被读取才会进监听列表，删除的目录要从列表里摘掉）。
 * 结束时无论有无变化都发 refreshed()。
 */
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

/**
 * @brief 根层条目数（Q_PROPERTY 的 READ 侧）
 *
 * @return 顶层子节点数；空树返回 0
 */
int FileTreeModel::topLevelCount() const
{
    return m_root ? static_cast<int>(m_root->children.size()) : 0;
}

/**
 * @brief 叠加用户图标配置并转发给图标表
 *
 * 只影响之后新取的 IconRole 值：不会为已渲染的行补发 dataChanged，
 * 因此要在 setRootPath() 之前调用（门面 ToolsFacade 保证这个顺序）。
 *
 * @param path 用户配置文件路径（约定是 <dataRoot>/file_icons.json）
 */
void FileTreeModel::loadUserIconFile(const QString &path)
{
    m_icons.loadUserFile(path);
}

/**
 * @brief 幂等的 fetch 兜底（QML 调用）
 *
 * 视图没有驱动 fetchMore 时由 QML 直接调用；目录未读取则读，其余
 * 情况静默返回。
 *
 * @param parent 目录索引；无效索引指 invisible root
 */
void FileTreeModel::fetchChildren(const QModelIndex &parent)
{
    fetchNode(parent.isValid() ? nodeForIndex(parent) : m_root.get());
}

// --- private ------------------------------------------------------------------

/**
 * @brief 索引 → 节点
 *
 * @param index 模型索引
 * @return internalPointer 里的 Node *；无效索引返回 nullptr
 */
FileTreeModel::Node *FileTreeModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<Node *>(index.internalPointer());
}

/**
 * @brief 节点 → 索引
 *
 * @param node 树节点
 * @return 节点所在行的索引；节点为空或为 invisible root 时返回无效索引
 */
QModelIndex FileTreeModel::indexForNode(Node *node) const
{
    if (!node || !node->parent) {
        return QModelIndex();
    }
    return createIndex(node->row, 0, node);
}

/**
 * @brief 列目录并按固定顺序排序
 *
 * 排除隐藏项（Unix 的点文件 / Windows 的隐藏属性），其余不过滤：
 * node_modules、.git 这类目录照常出现，开销由懒加载控制。目录在前、
 * 文件名大小写不敏感排序；同字母异大小写再按原序保证稳定。
 *
 * @param dirPath 目录的绝对路径
 * @return 排好序的目录项清单；目录不存在时为空
 */
QFileInfoList FileTreeModel::readSortedEntries(const QString &dirPath)
{
    QFileInfoList entries = QDir(dirPath).entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);
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

/**
 * @brief 为目录项建子节点
 *
 * @param entry 目录项（文件或目录）
 * @param parent 父节点，行号由 renumberChildren() 统一落位
 * @return 建好的节点（未挂树）
 */
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

/**
 * @brief 建工作区根节点
 *
 * 空串在这里归零返回 nullptr：空串经 QDir 也可能被拼成非空（相对进程
 * 当前目录），不归零的话无工作区时树会指向当前目录。
 *
 * @param path 工作区目录的绝对路径
 * @return 根节点（isDir 恒真、未挂子树）；空串返回 nullptr
 */
std::unique_ptr<FileTreeModel::Node> FileTreeModel::makeRootNode(const QString &path)
{
    if (path.isEmpty()) {
        return nullptr;
    }
    auto root = std::make_unique<Node>();
    root->name = QFileInfo(path).fileName();
    root->path = QDir(path).absolutePath();
    root->isDir = true;
    return root;
}

/**
 * @brief 读盘构造 parent 的子节点数组
 *
 * 不挂树、不发信号：调用方决定是在 staging 树里静默挂接
 * （attachChildren）还是走 begin/end 信号。
 *
 * @param parent 目录节点
 * @return 按排序序排好的子节点数组
 */
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

/**
 * @brief 把子节点数组挂到 node 上并落位行号
 *
 * parent 指针在建节点时已写好；不带任何模型信号，信号外壳由调用方
 * （fetchNode 的 begin/end 或 staging 构造）负责。
 *
 * @param node 目录节点
 * @param children 子节点数组（移动进来）
 */
void FileTreeModel::attachChildren(
        Node *node, std::vector<std::unique_ptr<Node>> children)
{
    node->children = std::move(children);
    node->fetched = true;
    renumberChildren(node);
}

/**
 * @brief 按数组序重排 node 各子节点的行号
 *
 * 结构变化（挂接、对账）后必须调一次，行号是 index()/parent() 的依据。
 *
 * @param node 目录节点
 */
void FileTreeModel::renumberChildren(Node *node)
{
    for (size_t row = 0; row < node->children.size(); ++row) {
        node->children[row]->row = static_cast<int>(row);
    }
}

/**
 * @brief 无信号版 fetch：构造期（staging 树）使用
 *
 * @param node 目录节点；空指针、文件节点或已读取时静默返回
 */
void FileTreeModel::populateNode(Node *node)
{
    if (!node || !node->isDir || node->fetched) {
        return;
    }
    attachChildren(node, readChildNodes(node));
}

/**
 * @brief 活树上的 fetch：读目录内容并对视图发 rowsInserted
 *
 * 先读盘构造、再 begin/attach/end：构造期不动模型结构，beginInsertRows
 * 声明的行数与 attach 的行数严格一致。fetch 到的是根节点时发
 * topLevelCountChanged，随后重布防 watcher。
 *
 * @param node 目录节点；空指针、文件节点或已读取时静默返回
 */
void FileTreeModel::fetchNode(Node *node)
{
    if (!node || !node->isDir || node->fetched) {
        return;
    }

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

/**
 * @brief 对账一个已读取的目录并递归其已读取的子目录
 *
 * 未读取的目录不碰（懒加载的行还没有投影给视图）。目录集合有增删时
 * 由调用方（refresh）重布防 watcher。
 *
 * @param node 目录节点
 * @return 整棵子树是否有任何变化（决定是否重布防 watcher）
 */
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

/**
 * @brief 单层对账：把 node 的子项与磁盘列表对齐，差异压缩成最少的行信号
 *
 * 三步走，每步都严格夹在 begin/end 之间：
 * 1. 按名字匹配旧节点，得到目标序列 desired（幸存者用旧指针，新面孔现建）；
 * 2. 删除阶段从后往前逐段 erase，前面的行号在本阶段内始终有效；
 * 3. 插入阶段从前往后逐段 insert——此时 children 只剩幸存者，而两侧都按同一
 *    比较器排过序，所以幸存者在 desired 与 children 里必然同序，一个游标就够。
 * 幸存者不动，它们的子树、fetched 状态与视图里的展开状态因此全部保留。
 *
 * @param node 已读取的目录节点
 * @return 本层是否有变化
 */
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

/**
 * @brief 收集整棵树里已读目录的绝对路径
 *
 * armWatchers() 布防监听用的清单；未读取的目录没有行，不值得监听。
 *
 * @param node 起始节点（通常是根）
 * @param out 追加目标
 */
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

/**
 * @brief 全量重布防文件系统监听
 *
 * 每次结构变化后旧列表不可复用（目录集合有增删），先全部摘掉再按
 * 「已读目录」清单重新加。个别目录加不上只记警告，不影响其余。
 */
void FileTreeModel::armWatchers()
{
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
