#ifndef AWB_TOOLS_TOOLSSTORE_H
#define AWB_TOOLS_TOOLSSTORE_H

#include <QList>
#include <QString>
#include <QStringList>

#include <QFile>

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

/// 一条提示词草稿（tools.json 的 `drafts` 数组项，M4）：一份可编辑的
/// 提示词正文 + 它自己的标题与工作区绑定。草稿没有磁盘文件——它的
/// 存在本身就是 tools.json 里的这条记录；渲染镜像（预览会话）按需创建。
struct DraftEntry {
    QString id;         ///< 稳定标识（生成一次永不变；旧 draft 键迁移固定 "default"）
    QString title;      ///< 显示标题；空时由首行推导，再空给 "Untitled"
    QString body;       ///< 正文（Markdown）
    QString workspace;  ///< 绑定的工作区绝对路径；空串 = 未绑定（不要求仍在 workspaces 里）
    QString savePath;   ///< 上次保存到的 Markdown 文件；空串 = 从未保存
    qint64 createdAt = 0;  ///< 创建时间（ms 纪元）
    qint64 updatedAt = 0;  ///< 最后编辑时间（排序与淘汰用）
};

/// 一条历史快照（tools.json 的 `history` 数组项，M4）：某草稿在某时点
/// 的一版全文。快照管「跨会话的版本级回退」，与编辑区按键级的撤销栈
/// 分工互补。
struct DraftSnapshot {
    QString draftId;    ///< 属于哪条草稿
    QString body;       ///< 那一版的全文
    QString reason;     ///< 触发原因：copy / clear / replace / switch / manual / rollback
    qint64 createdAt = 0;  ///< 时间（ms 纪元）
    int charCount = 0;  ///< 字数（面板显示用，避免每次都数）
};

/// 一条快照的历史约束（M4）：触发原因的合法取值，磁盘格式的一部分。
namespace DraftSnapshotReason {
inline constexpr auto kCopy = "copy";           ///< 复制到剪贴板前
inline constexpr auto kClear = "clear";         ///< 清空 / 替换全文前
inline constexpr auto kReplace = "replace";     ///< 模板/恢复覆盖前
inline constexpr auto kSwitch = "switch";       ///< 切走草稿时有改动
inline constexpr auto kManual = "manual";       ///< 用户手动存一版
inline constexpr auto kRollback = "rollback";   ///< 回滚前自动存当前版
} // namespace DraftSnapshotReason

/// 一条收藏（tools.json 的 `favorites` 数组项）：**成稿**提示词，可直接
/// 插入使用——与模板（骨架、要填变量）是两种东西。
struct FavoriteEntry {
    QString title;        ///< 显示标题
    QString body;         ///< 全文（Markdown）
    QStringList tags;     ///< 可选标签
    qint64 createdAt = 0; ///< 创建时间（ms 纪元）
};

/// 编辑器偏好（tools.json 的 `editor` 段，M3 新增，缺键全取默认）：
/// 查找替换的两个开关 + 上下文预算的窗口基数。
struct EditorSettings {
    // B2 查找替换的持久项
    bool findCaseSensitive = false;   ///< 区分大小写
    bool findWholeWords = false;      ///< 全词匹配
    // C4 预算条的持久项（保守常量 32k，决策 D-9 小决策 2：不探测真实窗口）
    int contextWindowTokens = 32000;  ///< 上下文窗口估算基数（>= 1000）
};

/// tools.json 的全部状态：工作区列表（MRU 序）、当前工作区、提示词草稿、
/// Ribbon 界面偏好、编辑器偏好、体检规则开关。
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
    /// 草稿上限（规划 05 建议区间 20–50 的下限）；满了再建要先关一条，
    /// 不静默淘汰——草稿是用户资产。
    static constexpr int kMaxDrafts = 20;
    /// 每条草稿保留的快照上限（新在前，超出淘汰最旧）。
    static constexpr int kMaxSnapshotsPerDraft = 30;

    // 构造时只记数据根，磁盘状态要等 load()
    explicit ToolsStore(const QString &dataRoot);

    // 读取 tools.json；文件缺失或损坏一律得到空列表、空 current、空草稿
    void load();

    // 工作区列表（MRU 在前，最多 kMaxWorkspaces 项）
    QStringList workspaces() const { return m_workspaces; }
    // 当前工作区的绝对路径；无当前工作区时为空串
    QString currentWorkspace() const { return m_current; }
    // 提示词草稿全文（**当前草稿**的镜像；多草稿读写走 drafts()/draftBody()
    // 族。保留它是因为旧版程序只认识这个键——写侧持续同步，降级读不丢内容）
    QString draft() const { return m_draft; }
    // Ribbon 界面偏好（缺键取默认值）
    RibbonSettings ribbon() const { return m_ribbon; }

    // --- 草稿集（M4） ------------------------------------------------------
    // 草稿清单（tools.json 的存储顺序 = 创建顺序；显示排序由调用方做）
    QList<DraftEntry> drafts() const { return m_drafts; }
    // 当前草稿 id；指向不存在项时 load() 已回退修正
    QString activeDraftId() const { return m_activeDraftId; }
    // 按 id 取一条草稿；不存在返回空 id 的条目
    DraftEntry draftById(const QString &id) const;
    // 新建一条草稿并设为当前：标题空时给 "Untitled N"，工作区默认绑
    // givenWorkspace（空 = 不绑）。返回新草稿 id；超过 kMaxDrafts 拒绝
    // （返回空串，*rejected 不为空时给出可展示原因）。
    QString createDraft(const QString &title, const QString &body,
                        const QString &workspace, QString *rejected);
    // 整条覆盖（标题/正文/工作区绑定）并立即写盘；id 不存在时 false。
    // updatedAt 顺带刷新。
    bool updateDraft(const QString &id, const QString &title,
                     const QString &workspace);
    // 草稿正文回写（编辑防抖的落点）：更新 body + updatedAt 并立即写盘。
    bool setDraftBody(const QString &id, const QString &body);
    // 重命名（只改标题；KISS——改名不改 id，tab 恢复按 id 不受影响）。
    bool renameDraft(const QString &id, const QString &title);
    // 记下草稿上次保存到的 Markdown 文件（空串 = 清掉记录）。
    bool setDraftSavePath(const QString &id, const QString &path);
    // 删除一条草稿（连同它的快照与恢复副本记录）。删的是当前草稿时
    // activeDraftId 顺延为队首剩余项（空则空串）。返回落盘是否成功。
    bool removeDraft(const QString &id);
    // 设当前草稿（id 必须存在）；同时把 `draft` 镜像键同步为新当前。
    bool setActiveDraft(const QString &id);
    // 生成一个未占用的草稿 id（"d" + 十六进制序号）。
    QString allocateDraftId() const;

    // --- 历史快照（M4） ----------------------------------------------------
    // 某条草稿的快照（新在前，最多 kMaxSnapshotsPerDraft 条）；空草稿给空表
    QList<DraftSnapshot> snapshots(const QString &draftId) const;
    // 存一版快照（新在队首；上限淘汰最旧）。reason 取
    // DraftSnapshotReason::k* 常量。返回落盘是否成功。
    bool addSnapshot(const QString &draftId, const QString &body,
                     const QString &reason);
    // 删除一版快照（draftId + createdAt 精确定位）。
    bool removeSnapshot(const QString &draftId, qint64 createdAt);

    // --- 崩溃恢复副本（M4，D6） ---------------------------------------------
    // tools_recovery.json 的形状：{draftId, body, updatedAt}。编辑期降频写
    //（比 500 ms 草稿防抖更密、仍是停顿触发），正常退出删除。
    struct RecoveryInfo {
        bool present = false;   ///< 副本是否存在（上次未正常退出）
        QString draftId;        ///< 它属于哪条草稿
        QString body;           ///< 那一版正文
        qint64 updatedAt = 0;   ///< 时间
    };
    // 读恢复副本；不存在得到 present=false。
    RecoveryInfo recovery() const;
    // 落一份恢复副本（原子写，同目录）。draftId 为空时删除副本。
    bool writeRecovery(const QString &draftId, const QString &body);
    // 正常退出时调用：删除恢复副本文件。
    bool clearRecovery();

    // 编辑器偏好（M3：查找选项与预算基数；缺键取默认值）
    EditorSettings editor() const { return m_editor; }
    // 覆盖编辑器偏好并立即写盘
    bool setEditor(const EditorSettings &editor);

    // 体检规则开关：**只存已关闭的规则 id**（缺键 = 全开）。大多数用户
    // 一条都不关，空数组是常态；「默认全开 + 记例外」让未来新增的规则
    // 自动是开的，不需要迁移。
    QStringList disabledCheckIds() const { return m_disabledChecks; }
    // 整组覆盖体检规则的关闭集合并立即写盘
    bool setDisabledCheckIds(const QStringList &ids);

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

    // 覆盖草稿并立即写盘（**当前草稿**的镜像写侧：更新它的 body 并同步
    // `draft` 键；没有草稿时静默建一条 "default"——与读侧容错对偶）
    bool setDraft(const QString &text);

    // 覆盖 Ribbon 界面偏好并立即写盘
    bool setRibbon(const RibbonSettings &ribbon);

    // tools.json 的绝对路径
    QString storeFilePath() const;
    // tools_recovery.json 的绝对路径（恢复副本专用，见 RecoveryInfo）
    QString recoveryFilePath() const;

private:
    // 把当前全部状态原子写回 tools.json
    bool save();
    // `draft` 镜像键的同步：写当前草稿正文（无草稿时清空）
    void syncDraftMirror();
    // 快照列表按上限裁剪（每草稿 kMaxSnapshotsPerDraft 条，淘汰最旧）
    void trimSnapshots();

    QString m_dataRoot;    ///< 数据根；storeFilePath() 由它拼出
    QStringList m_workspaces; ///< 工作区列表，MRU 在前
    QString m_current;     ///< 当前工作区的绝对路径；空串表示无
    QString m_draft;       ///< 当前草稿正文的镜像（旧版兼容键，见 draft()）
    RibbonSettings m_ribbon; ///< Ribbon 界面偏好
    EditorSettings m_editor;   ///< 编辑器偏好（查找选项、预算基数）
    QStringList m_disabledChecks;  ///< 已关闭的体检规则 id（其余全开）
    QList<FavoriteEntry> m_favorites;  ///< 收藏夹，新在前
    QList<DraftEntry> m_drafts;       ///< 草稿集（存储顺序 = 创建顺序）
    QString m_activeDraftId;          ///< 当前草稿 id；空 = 无草稿
    QList<DraftSnapshot> m_snapshots; ///< 全部快照（新在前；按草稿分组过滤读）
    int m_docSeq = 0;                 ///< allocateDraftId 的水位（内存即可：
                                      ///< 重新分配只要求「不撞既有 id」）
};

} // namespace awb::tools

#endif // AWB_TOOLS_TOOLSSTORE_H
