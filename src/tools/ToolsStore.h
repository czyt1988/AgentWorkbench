#ifndef AWB_TOOLS_TOOLSSTORE_H
#define AWB_TOOLS_TOOLSSTORE_H

#include <QList>
#include <QString>
#include <QStringList>

namespace awb::tools {

/// Ribbon 的界面偏好（tools.json 的 `ribbon` 段）。四个小值都是窗口级
/// 偏好：折叠位、样式档位、是否跟随应用主题、上次停留的标签页。样式取值
/// 为 "single" / "two-row" / "three-row"，缺省单行。
struct RibbonSettings {
    bool collapsed = false;        ///< Ribbon 折叠位（minimumMode）
    QString style = QStringLiteral("single"); ///< 样式档位
    bool followAppTheme = true;    ///< Ribbon 主题跟随应用主题
    int lastTabIndex = 0;          ///< 上次停留的标签页索引（从 0 起）
};

/// 一条收藏（tools.json 的 `favorites` 数组项）：**成稿**提示词，可直接
/// 插入使用——与模板（骨架、要填变量）是两种东西。
struct FavoriteEntry {
    QString title;        ///< 显示标题
    QString body;         ///< 全文（Markdown）
    QStringList tags;     ///< 可选标签
    qint64 createdAt = 0; ///< 创建时间（ms 纪元）
};

/// tools.json 的全部状态：工作区列表（MRU 序）、当前工作区、提示词草稿、
/// Ribbon 界面偏好。
///
/// 启动时 load() 一次，之后每次变更立即原子写回（core::JsonStore）；
/// 列表语义（MRU 换序、上限淘汰）与键位见 ToolsStore.cpp。
class ToolsStore
{
public:
    /// 工作区记忆上限；第 21 个把队尾（最久未用）挤出去。
    static constexpr int kMaxWorkspaces = 20;
    /// 收藏上限；超出淘汰最旧（新在前，队尾最旧）。
    static constexpr int kMaxFavorites = 50;

    // 构造时只记数据根，磁盘状态要等 load()
    explicit ToolsStore(const QString &dataRoot);

    // 读取 tools.json；文件缺失或损坏一律得到空列表、空 current、空草稿
    void load();

    // 工作区列表（MRU 在前，最多 kMaxWorkspaces 项）
    QStringList workspaces() const { return m_workspaces; }
    // 当前工作区的绝对路径；无当前工作区时为空串
    QString currentWorkspace() const { return m_current; }
    // 提示词草稿全文
    QString draft() const { return m_draft; }
    // Ribbon 界面偏好（缺键取默认值）
    RibbonSettings ribbon() const { return m_ribbon; }

    // 收藏夹（新在前；缺键为空列表）
    QList<FavoriteEntry> favorites() const { return m_favorites; }
    // 新增一条收藏（同标题 + 同全文 = 幂等更新）；超出上限淘汰最旧。
    bool addFavorite(const QString &title, const QString &body,
                     const QStringList &tags);
    // 按标题 + 全文精确匹配移除一条收藏。
    bool removeFavorite(const QString &title, const QString &body);

    // 新增（或把已有项移到队首）并设为当前；返回落盘是否成功
    bool addWorkspace(const QString &path);

    // 移除一项；被移除的是当前工作区时，current 顺延为队首剩余项（可为空）
    bool removeWorkspace(const QString &path);

    // 切换当前工作区并把该项移到队首；path 必须已在列表中（空串 = 清空 current）
    bool setCurrentWorkspace(const QString &path);

    // 覆盖草稿并立即写盘
    bool setDraft(const QString &text);

    // 覆盖 Ribbon 界面偏好并立即写盘
    bool setRibbon(const RibbonSettings &ribbon);

    // tools.json 的绝对路径
    QString storeFilePath() const;

private:
    // 把当前全部状态原子写回 tools.json
    bool save();

    QString m_dataRoot;    ///< 数据根；storeFilePath() 由它拼出
    QStringList m_workspaces; ///< 工作区列表，MRU 在前
    QString m_current;     ///< 当前工作区的绝对路径；空串表示无
    QString m_draft;       ///< 提示词草稿全文
    RibbonSettings m_ribbon; ///< Ribbon 界面偏好
    QList<FavoriteEntry> m_favorites;  ///< 收藏夹，新在前
};

} // namespace awb::tools

#endif // AWB_TOOLS_TOOLSSTORE_H
