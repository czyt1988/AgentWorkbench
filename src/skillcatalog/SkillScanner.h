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

    // 当前配置的根清单（settings 覆盖优先，否则默认清单）
    QList<SkillRoot> roots() const;
    // 持久化某个根的启用/禁用开关（写进 skills.roots）
    void setRootEnabled(const QString &id, bool enabled);

    // 发起一次扫描。立即返回——扫描跑在全局线程池上，结果经
    // scanFinished() 回到 GUI 线程；新结果由 worker 在返回前顺手固化
    // 进 JSON 缓存。
    Q_INVOKABLE void refresh();

    // refresh() 到对应 scanFinished() 之间为 true
    bool scanning() const { return m_scanning; }

    // 最近一次扫描产出的定义（首次 refresh()/adoptResults() 之前为空）
    const QList<SkillDefinition> &definitions() const { return m_definitions; }

    // 最近一次扫描的统计（首次 refresh()/adoptResults() 之前全 0）
    Stats lastStats() const { return m_lastStats; }

    // 把现成的结果当作最近一次扫描结果采用（启动时从 JSON 缓存恢复）。
    // 发射 scanFinished()，订阅方（模型、页面）与真扫描走同一条落地路径。
    void adoptResults(const QList<SkillDefinition> &definitions,
                      const Stats &stats);

Q_SIGNALS:
    /**
     * @brief 一次扫描的结果落地时发射（真扫描与缓存恢复共用）
     */
    void scanFinished();

    /**
     * @brief 真扫描开始时发射（缓存恢复不发）
     */
    void scanStarted();

    /**
     * @brief 只在 m_scanning 真实翻转时发射（缓存恢复不发）
     *
     * 订阅方的骨架屏、按钮转圈都挂在它上，重复发射会重放动画。
     */
    void scanningChanged();

private:
    // 落地一份结果：更新成员并发射 scanFinished()
    void applyResults(const QList<SkillDefinition> &definitions,
                      const Stats &stats);

    core::Settings *m_settings;             ///< 设置访问（仅 GUI 线程触碰）
    QList<SkillRoot> m_roots;               ///< 最近一次解析的根清单；空 = 用默认
    QList<SkillDefinition> m_definitions;   ///< 最近一次扫描的定义
    Stats m_lastStats;                      ///< 最近一次扫描的统计
    bool m_scanning = false;                ///< 扫描进行中标志（防抖用）
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLSCANNER_H
