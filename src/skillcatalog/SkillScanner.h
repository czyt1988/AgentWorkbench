#ifndef AWB_SKILLS_SKILLSCANNER_H
#define AWB_SKILLS_SKILLSCANNER_H

#include "skillcatalog/SkillDefinition.h"
#include "skillcatalog/SkillRoot.h"
#include "skillcatalog/SkillScanTask.h"

#include <QList>
#include <QObject>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::skillcatalog {

// 扫描协调者：持有当前根配置与最近一次结果，把纯计算 SkillScanTask 派发
// 到全局线程池执行。
//
// 线程契约：本对象永远活在 GUI 线程（QObject + parent）；worker 线程里跑
// 的只有 SkillScanTask::run() 与 SkillCache::save() 这两个纯静态函数，
// 它们只碰值类型副本，不回来碰 Settings/本对象。结果经 QFutureWatcher
// 的 finished 回到 GUI 线程。
//
// 扫描进行中再次 refresh() 会被忽略（防抖）：根列表本来就从 settings
// 实时快照而来，排队第二次扫描没有意义——等结果落地后手动再点即可。
class SkillScanner : public QObject
{
    Q_OBJECT

public:
    using Stats = SkillScanTask::Stats;

    explicit SkillScanner(core::Settings *settings,
                          QObject *parent = nullptr);

    // Roots as configured (settings override or defaults).
    QList<SkillRoot> roots() const;
    // Persist an enabled/disabled flag for one root (into skills.roots).
    void setRootEnabled(const QString &id, bool enabled);

    // Kick a scan. Returns immediately — the scan runs on the global thread
    // pool, the result arrives through scanFinished() on the GUI thread,
    // and the fresh definitions are persisted to the JSON cache by the
    // worker before it returns.
    Q_INVOKABLE void refresh();

    // True between refresh() and the matching scanFinished().
    bool scanning() const { return m_scanning; }

    // The definitions produced by the last scan (empty until the first
    // refresh() or adoptResults()).
    const QList<SkillDefinition> &definitions() const { return m_definitions; }

    // Stats of the last scan (zeros until the first refresh()/adoptResults()).
    Stats lastStats() const { return m_lastStats; }

    // 把现成的结果当作最近一次扫描结果采用（启动时从 JSON 缓存恢复）。
    // 发射 scanFinished()，订阅方（模型、页面）与真扫描走同一条落地路径。
    void adoptResults(const QList<SkillDefinition> &definitions,
                      const Stats &stats);

Q_SIGNALS:
    void scanFinished();
    void scanStarted();
    /// 只在 m_scanning 真实翻转时发射（缓存恢复不发）——订阅方的骨架
    /// 屏、按钮转圈都挂在它上，重复发射会重放动画。
    void scanningChanged();

private:
    void applyResults(const QList<SkillDefinition> &definitions,
                      const Stats &stats);

    core::Settings *m_settings;
    QList<SkillRoot> m_roots;
    QList<SkillDefinition> m_definitions;
    Stats m_lastStats;
    bool m_scanning = false;
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLSCANNER_H
