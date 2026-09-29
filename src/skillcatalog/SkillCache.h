#ifndef AWB_SKILLS_SKILLCACHE_H
#define AWB_SKILLS_SKILLCACHE_H

#include "core/OpResult.h"
#include "skillcatalog/SkillDefinition.h"
#include "skillcatalog/SkillScanTask.h"

#include <QDateTime>
#include <QList>

namespace awb::skillcatalog {

/// 上一次扫描结果的 JSON 缓存（<dataRoot>/skills_cache.json，路径经
/// core::Paths 取得）。启动时 load() 先行恢复，页面不经扫描即可渲染；
/// 每次扫描完成后由 worker 线程 save() 覆盖。
///
/// 缓存只服务「启动到首次扫描完成之间的首屏渲染」，不做任何失效判断：
/// 启动总是发一次真扫描，roots 变更、目录变化都会在扫描落地后自然修正。
class SkillCache
{
public:
    /// 一次成功读回的缓存内容。
    struct Snapshot
    {
        QList<SkillDefinition> definitions;
        SkillScanTask::Stats stats;
        QDateTime cachedAt; ///< 写入时刻；invalid = 没有可用缓存

        bool isValid() const { return cachedAt.isValid(); }
    };

    /// 读缓存。文件缺失、损坏或格式版本不认识都返回 invalid Snapshot，
    /// 不发警告——「首次启动」与「缓存过期」都是正常路径，扫描随后自愈。
    /// 小文件毫秒级，允许在 GUI 线程同步调用。
    static Snapshot load();

    /// 原子写缓存（QSaveFile）。静态纯函数、线程安全：扫描 worker 在
    /// 自己的线程里调用，GUI 线程不为此付出任何 IO 时间。
    static core::OpResult save(const QList<SkillDefinition> &definitions,
                               const SkillScanTask::Stats &stats);

    /// 缓存文件路径。
    static QString filePath();
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLCACHE_H
