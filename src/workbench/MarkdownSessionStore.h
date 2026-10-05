#ifndef AWB_WORKBENCH_MARKDOWNSESSIONSTORE_H
#define AWB_WORKBENCH_MARKDOWNSESSIONSTORE_H

#include <QList>
#include <QString>
#include <QStringList>

namespace awb::workbench {

/// 一次「关窗时的现场」：tab 组、激活位、预设与界面开合、最近文档。
///
/// @struct MarkdownSessionState
struct MarkdownSessionState {
    struct Tab {
        QString path;    ///< 磁盘绝对路径（文件 tab；草稿 tab 为空）
        QString draftId; ///< tools.json 里的草稿 id（草稿 tab；文件 tab 为空）
        QString mode;    ///< 该 tab 的三态（preview/split/source）
    };

    QList<Tab> tabs;                 ///< tab 组（打开顺序；草稿在前、文件在后）
    int activeIndex = -1;            ///< 激活 tab 下标；-1 = 无
    QString preset = QStringLiteral("prompt");  ///< 上次的界面预设
    bool promptLeftOpen = true;      ///< 提示词预设的左栏开合
    bool documentLeftOpen = false;   ///< 文档预设的左栏开合
    bool documentRibbonCollapsed = true;  ///< 文档预设的 Ribbon 折叠位
    QStringList recentDocuments;     ///< 最近文档 MRU（新在前，上限 20）
};

/// `markdown_session.json` 的读写（workbench 域：会话是窗口级布局状态，
/// 跨 tools（草稿 id）与 markdown（文件路径）两个域，只有 L3 能同时认识）。
///
/// 语义（见计划的 E3 与数据规划）：tab 结构变化立即写；**正常退出不删除**
/// ——会话的意义就是跨启动找回。损坏的 JSON 整体作废重来（布局状态不值得
/// 抢救），记一条警告。隐私口径：只有本地路径，无正文、不加密、不清理。
class MarkdownSessionStore
{
public:
    /// 最近文档 MRU 的上限（存储侧；启动页只显示前几条）。
    static constexpr int kMaxRecentDocuments = 20;

    explicit MarkdownSessionStore(const QString &dataRoot);

    /// 读会话；文件缺失或损坏一律得到默认态（并记一条警告）。
    MarkdownSessionState load() const;

    /// 原子写会话（core::JsonStore：临时文件 + 替换）。
    bool save(const MarkdownSessionState &state) const;

    /// 会话文件的绝对路径（<dataRoot>/markdown_session.json）。
    QString filePath() const;

    /// 把一个路径触到 MRU 队首（去重、截断到上限）；返回新队列。
    static QStringList touchRecent(const QStringList &recent,
                                   const QString &path);

private:
    QString m_dataRoot;   ///< 数据根；filePath() 由它拼出
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_MARKDOWNSESSIONSTORE_H
