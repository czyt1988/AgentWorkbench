#include "tools/FileTreeFlatModel.h"

#include "tools/FileTreeModel.h"

namespace awb::tools {

/**
 * @brief 构造扁平投影模型
 *
 * @param parent QObject 父项
 */
FileTreeFlatModel::FileTreeFlatModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

/**
 * @brief 设定树源并接好事实信号
 *
 * 同一个模型重复设定时只重接信号；换 nullptr 断开旧源并清空投影。
 * 接好后立即投影一次。
 *
 * @param model 树源；可为 nullptr（投影清空）
 */
void FileTreeFlatModel::setSourceModel(FileTreeModel *model)
{
    if (m_source == model) {
        return;
    }
    if (m_source) {
        m_source->disconnect(this);
    }
    m_source = model;
    if (model) {
        // 只听换根与「一次 refresh 结束」两个事实信号；refresh 过程中的
        // 逐目录 rows* 增删是源模型给树视图的记账，这里不消费。
        connect(model, &QAbstractItemModel::modelReset, this, [this]() {
            // 换根 = 另一棵树：旧的相对路径失去意义，展开状态清零。
            m_expandedPaths.clear();
            rebuild();
        });
        connect(model, &FileTreeModel::refreshed, this,
                &FileTreeFlatModel::rebuild);
    }
    rebuild();
}

/**
 * @brief 取投影行数
 *
 * @param parent 列表模型无层级，有效索引恒返回 0
 * @return 当前投影的行数
 */
int FileTreeFlatModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

/**
 * @brief 按 role 派发投影行数据
 *
 * 深度/展开是扁平模型自己的状态；文本与图标 role 转发到源模型的对应
 * 节点（role 枚举数值不同，映射见函数内的 case 表）。
 *
 * @param index 投影行索引
 * @param role 请求的 role
 * @return 对应数据；行越界时返回无效 QVariant
 */
QVariant FileTreeFlatModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const Row &row = m_rows.at(static_cast<size_t>(index.row()));
    switch (role) {
    case DepthRole:
        return row.depth;
    case ExpandedRole:
        return row.expanded;
    case HasChildrenRole:
        // 未读目录恒真（懒加载箭头的前提），读过的目录空则假——与源模型
        // hasChildren 的语义一致。
        return m_source->hasChildren(row.source);
    // 文本与图标 role 显式映射到源模型的 role 枚举：两份枚举同值到
    // IsDirRole 为止（源在 IconRole 前还有 SuffixRole），数值不能直传。
    case NameRole:
        return m_source->data(row.source, FileTreeModel::NameRole);
    case PathRole:
        return m_source->data(row.source, FileTreeModel::PathRole);
    case RelativePathRole:
        return m_source->data(row.source, FileTreeModel::RelativePathRole);
    case IsDirRole:
        return m_source->data(row.source, FileTreeModel::IsDirRole);
    case IconRole:
        return m_source->data(row.source, FileTreeModel::IconRole);
    default:
        return m_source->data(row.source, role);
    }
}

/**
 * @brief role 名表：role 号 → QML 端的属性名
 *
 * 名字与 FileTreeModel 的同名 role 保持一致（多出 depth/expanded/
 * hasChildren 三个扁平专属 role），QML delegate 的 required property
 * 因此不用改。
 *
 * @return role 号到 role 名的映射
 */
QHash<int, QByteArray> FileTreeFlatModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "display"},
        {NameRole, "name"},
        {PathRole, "path"},
        {RelativePathRole, "relativePath"},
        {IsDirRole, "isDir"},
        {IconRole, "iconSource"},
        {DepthRole, "depth"},
        {ExpandedRole, "expanded"},
        {HasChildrenRole, "hasChildren"},
    };
}

/**
 * @brief 展开或收起投影里的一个目录行
 *
 * 展开路径：先 fetchChildren 兜底（未读目录立刻有子行），再把子树按
 * 当前展开键递归投影成行、一次 rowsInserted 插到该行之后；空目录没有
 * 行可插，只翻展开态（箭头由 hasChildren 的 dataChanged 自然消失）。
 * 收起路径是递归的：子树内所有目录的展开键一并清掉，再展开父级时不会
 * 「记得」深层状态。
 *
 * @param row 投影行号；越界或非目录行静默返回
 */
void FileTreeFlatModel::toggleExpanded(int row)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const QModelIndex source = m_rows.at(static_cast<size_t>(row)).source;
    const int depth = m_rows.at(static_cast<size_t>(row)).depth;
    const bool wasExpanded =
        m_rows.at(static_cast<size_t>(row)).expanded;
    const bool isDir =
        m_source->data(source, FileTreeModel::IsDirRole).toBool();
    const QString relPath =
        m_source->data(source, FileTreeModel::RelativePathRole).toString();
    if (!isDir || relPath.isEmpty()) {
        return;
    }

    if (!wasExpanded) {
        m_expandedPaths.insert(relPath);
        // 兜底 fetch 与展开投影同帧完成；源模型发的 rowsInserted 无人监听。
        m_source->fetchChildren(source);
        std::vector<Row> subtree;
        const int childCount = m_source->rowCount(source);
        for (int i = 0; i < childCount; ++i) {
            Row child;
            child.source = m_source->index(i, 0, source);
            child.depth = depth + 1;
            appendExpanded(subtree, child);
        }
        const int first = row + 1;
        const int last = row + static_cast<int>(subtree.size());
        // 空目录 fetch 后没有子行：没有行可插，只翻展开态（箭头由
        // hasChildren 的 dataChanged 自然消失）。
        if (!subtree.empty()) {
            beginInsertRows(QModelIndex(), first, last);
            m_rows.insert(m_rows.begin() + first, subtree.begin(),
                          subtree.end());
            endInsertRows();
        }
        // insert 可能重排 m_rows 的存储，展开位一律用下标回写。
        m_rows.at(static_cast<size_t>(row)).expanded = true;
        Q_EMIT dataChanged(index(row, 0), index(row, 0),
                         {ExpandedRole, HasChildrenRole});
    } else {
        // 收起是递归的：子树内所有目录的展开键一并清掉，再展开父级时
        // 不会「记得」深层状态。
        int last = row + 1;
        while (last < static_cast<int>(m_rows.size())
               && m_rows.at(static_cast<size_t>(last)).depth > depth) {
            const Row &descendant = m_rows.at(static_cast<size_t>(last));
            if (descendant.expanded) {
                m_expandedPaths.remove(
                    m_source->data(descendant.source,
                                   FileTreeModel::RelativePathRole)
                        .toString());
            }
            ++last;
        }
        if (last > row + 1) {
            beginRemoveRows(QModelIndex(), row + 1, last - 1);
            m_rows.erase(m_rows.begin() + row + 1, m_rows.begin() + last);
            endRemoveRows();
        }
        m_expandedPaths.remove(relPath);
        m_rows.at(static_cast<size_t>(row)).expanded = false;
        Q_EMIT dataChanged(index(row, 0), index(row, 0), {ExpandedRole});
    }
    Q_EMIT visibleCountChanged();
}

/**
 * @brief 从源模型重新投影整张扁平表
 *
 * 换根（modelReset）与一次 refresh 结束（refreshed）时由信号触发；
 * 走 model reset，收起的目录不投影。结束时发 visibleCountChanged()。
 */
void FileTreeFlatModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    if (m_source) {
        const int topCount = m_source->rowCount();
        for (int i = 0; i < topCount; ++i) {
            Row row;
            row.source = m_source->index(i, 0);
            row.depth = 0;
            appendExpanded(m_rows, row);
        }
    }
    endResetModel();
    Q_EMIT visibleCountChanged();
}

/**
 * @brief 追加一行，命中展开键时递归追加整棵子树
 *
 * 展开键跨 refresh 保留；目录可能还没读过盘，投影前先 fetchChildren
 * 兜底。
 *
 * @param out 投影行的追加目标
 * @param row 待追加的行（depth 已按父级算好）
 */
void FileTreeFlatModel::appendExpanded(std::vector<Row> &out, const Row &row)
{
    out.push_back(row);
    const QString relPath =
        m_source->data(row.source, FileTreeModel::RelativePathRole).toString();
    const bool isDir =
        m_source->data(row.source, FileTreeModel::IsDirRole).toBool();
    if (!isDir || !m_expandedPaths.contains(relPath)) {
        return;
    }
    // 展开键跨 refresh 保留；目录可能还没读过盘，投影前先兜底 fetch。
    m_source->fetchChildren(row.source);
    out.back().expanded = true;
    const int childCount = m_source->rowCount(row.source);
    for (int i = 0; i < childCount; ++i) {
        Row child;
        child.source = m_source->index(i, 0, row.source);
        child.depth = row.depth + 1;
        appendExpanded(out, child);
    }
}

} // namespace awb::tools
