#ifndef AWB_SKILLS_SKILLSCANTASK_H
#define AWB_SKILLS_SKILLSCANTASK_H

#include "skillcatalog/SkillDefinition.h"
#include "skillcatalog/SkillRoot.h"

#include <QList>
#include <QStringList>

namespace awb::skillcatalog {

/// 一次扫描的全部输入：值类型快照，不含任何 QObject 指针。
/// SkillScanTask::run() 因此可以整个跑在 worker 线程上。
struct SkillScanParams
{
    QList<SkillRoot> roots;          ///< 待扫的根（RAW 路径，扫描时展开）
    int maxDepth = 6;                ///< 目录遍历深度上限
    bool includePluginCaches = true; ///< 是否计入 plugin 缓存根
};

/// 纯计算的一次扫描：遍历根目录找 SKILL.md、解析 frontmatter、按 plugin
/// 版本去重。
///
/// 不持状态、不发信号、不读 Settings——输入输出都是值类型，调用方
/// （SkillScanner）负责在线程池里执行并把结果搬回 GUI 线程。扫描规则：
///  - 一个含 SKILL.md 的目录就是一个 skill，不再向下递归；
///  - 缺失/不可读的根跳过并记入 stats——一个坏根不会让整次扫描失败；
///  - plugin 缓存里同一 marketplace/plugin 的同一 skill 只保留最高版本。
class SkillScanTask
{
public:
    /// 一次扫描的统计（skill 数、根数、去重量、耗时）。
    struct Stats
    {
        int skillCount = 0;         ///< 扫到的 skill 总数（去重后）
        int rootsScanned = 0;       ///< 实际扫过（存在且可读）的根数
        int rootsSkipped = 0;       ///< 跳过的根（禁用/被过滤/不可读）
        int duplicatesDropped = 0;  ///< plugin 版本去重丢弃的条数
        qint64 elapsedMs = 0;       ///< 本次扫描总耗时
        QStringList skippedRoots;   ///< 跳过的根的 label（页面警示行用）
    };

    /// 一次扫描的输出：定义列表 + 统计。
    struct Result
    {
        QList<SkillDefinition> definitions;
        Stats stats;
    };

    /// 同步执行整次扫描。线程安全：只读 params、只写局部状态与返回值。
    static Result run(const SkillScanParams &params);
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLSCANTASK_H
