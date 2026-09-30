#ifndef AWB_WORKBENCH_ENVIRONMENTCACHE_H
#define AWB_WORKBENCH_ENVIRONMENTCACHE_H

#include "core/OpResult.h"
#include "workbench/EnvironmentProbe.h"

#include <QDateTime>

namespace awb::workbench {

/// 上一次运行时探测结果的 JSON 缓存（<dataRoot>/environment_cache.json，
/// 路径经 core::Paths 取得）。启动时 load() 先行恢复，界面不必等探测就
/// 能显示版本与路径；每轮探测落地后，服务只在结论**有变化**时才 save()。
///
/// 缓存不做失效判断，也不相信它：启动与手动重测总会真探测一轮，装了、
/// 升级了、卸载了都会在探测落地后自然修正——「卸载了要反馈不存在」正是
/// 靠这一轮真探测兜住的，缓存只负责首屏。
class EnvironmentCache
{
public:
    /// 一次成功读回的缓存内容。
    struct Snapshot
    {
        RuntimeState python; ///< 上次的 Python 结论
        RuntimeState node;   ///< 上次的 Node.js 结论
        QDateTime cachedAt;  ///< 写入时刻；invalid = 没有可用缓存

        bool isValid() const { return cachedAt.isValid(); }
    };

    // 读缓存。文件缺失、损坏或格式版本不认识都返回 invalid Snapshot，
    // 不发警告——「首次启动」与「缓存过期」都是正常路径，随后的探测
    // 会自愈。小文件毫秒级，允许在 GUI 线程同步调用。
    static Snapshot load();

    // 原子写缓存（QSaveFile）。
    static core::OpResult save(const RuntimeState &python,
                               const RuntimeState &node);

    // 缓存文件路径。
    static QString filePath();
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_ENVIRONMENTCACHE_H
