#ifndef AWB_TOOLS_TOOLSSTORE_H
#define AWB_TOOLS_TOOLSSTORE_H

#include <QString>
#include <QStringList>

namespace awb::tools {

/// tools.json 的全部状态：工作区列表（MRU 序）、当前工作区、提示词草稿。
///
/// 启动时 load() 一次，之后每次变更立即原子写回（core::JsonStore）；
/// 列表语义（MRU 换序、上限淘汰）与键位见 ToolsStore.cpp。
class ToolsStore
{
public:
    /// 工作区记忆上限；第 21 个把队尾（最久未用）挤出去。
    static constexpr int kMaxWorkspaces = 20;

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

    // 新增（或把已有项移到队首）并设为当前；返回落盘是否成功
    bool addWorkspace(const QString &path);

    // 移除一项；被移除的是当前工作区时，current 顺延为队首剩余项（可为空）
    bool removeWorkspace(const QString &path);

    // 切换当前工作区并把该项移到队首；path 必须已在列表中（空串 = 清空 current）
    bool setCurrentWorkspace(const QString &path);

    // 覆盖草稿并立即写盘
    bool setDraft(const QString &text);

    // tools.json 的绝对路径
    QString storeFilePath() const;

private:
    // 把当前全部状态原子写回 tools.json
    bool save();

    QString m_dataRoot;    ///< 数据根；storeFilePath() 由它拼出
    QStringList m_workspaces; ///< 工作区列表，MRU 在前
    QString m_current;     ///< 当前工作区的绝对路径；空串表示无
    QString m_draft;       ///< 提示词草稿全文
};

} // namespace awb::tools

#endif // AWB_TOOLS_TOOLSSTORE_H
