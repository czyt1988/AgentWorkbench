#ifndef AWB_TOOLS_FILETREEFLATMODEL_H
#define AWB_TOOLS_FILETREEFLATMODEL_H

#include <QAbstractListModel>
#include <QSet>
#include <QString>

#include <vector>

namespace awb::tools {

class FileTreeModel;

/// FileTreeModel 的扁平列表视图：把树按「展开中的节点压平、收起的不投影」
/// 的规则投影成 ListView 可消费的线性行序，展开状态由本类持有。
///
/// 为什么需要它：Qt 6.3 的 QML TreeView 在 Qt 5.15 不存在，本类把同一棵
/// 树投影成 QAbstractListModel，让 ToolsPage 的文件树在两个版本下共用同
/// 一份 delegate 与交互。只监听源模型的两个事实信号——modelReset（换根，
/// 展开状态清零）与 refreshed（一次 refresh 收尾，展开状态按路径保留）；
/// refresh 过程中的逐目录 rows* 增删不消费，避免一个 burst 重建多次。
/// role 名与 FileTreeModel 的契约一致（name/path/relativePath/isDir/
/// iconSource），QML delegate 的 required property 不用改。
class FileTreeFlatModel : public QAbstractListModel
{
    Q_OBJECT

    /// 当前投影里的行数；空状态占位用它判断（rowCount 不参与 QML 绑定）。
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY visibleCountChanged)

public:
    /// role 名是对 QML 的契约，与 FileTreeModel::Roles 同名同义。
    enum Roles {
        NameRole = Qt::UserRole + 1, ///< 显示名（不含路径）
        PathRole,                    ///< 绝对路径（正斜杠）
        RelativePathRole,            ///< 相对工作区根的路径（正斜杠）
        IsDirRole,                   ///< 是否目录
        IconRole,                    ///< 图标 URL（按名字/后缀查表，见 FileIcons）
        DepthRole,                   ///< 缩进层级（顶层为 0）
        ExpandedRole,                ///< 目录当前是否展开（文件恒 false）
        HasChildrenRole              ///< 是否有可展开的子项（未读目录恒真，见源模型）
    };
    Q_ENUM(Roles)

    explicit FileTreeFlatModel(QObject *parent = nullptr);

    /// 设定树源；接好信号后立即投影一次。
    void setSourceModel(FileTreeModel *model);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int visibleCount() const { return static_cast<int>(m_rows.size()); }

    /// 展开/收起 row（须为目录行）。展开时先 fetchChildren 兜底再投影子
    /// 行；收起是递归的——子目录的展开状态一并清掉。
    Q_INVOKABLE void toggleExpanded(int row);

    /// 重新投影：源模型根变化与刷新时由信号触发，也可手动调用。
    void rebuild();

signals:
    void visibleCountChanged();

private:
    void connectSource(FileTreeModel *model);

    /// 投影行：源模型索引 + 快照的层级/展开状态。
    struct Row {
        QModelIndex source;
        int depth = 0;
        bool expanded = false;
    };

    /// 追加 row 并在其展开键命中时递归追加子树（fetch 兜底在内）。
    void appendExpanded(std::vector<Row> &out, const Row &row);

    FileTreeModel *m_source = nullptr;
    std::vector<Row> m_rows;
    /// 已展开目录的路径集合（相对工作区根），跨源模型 refresh 保留。
    QSet<QString> m_expandedPaths;
};

} // namespace awb::tools

#endif // AWB_TOOLS_FILETREEFLATMODEL_H
