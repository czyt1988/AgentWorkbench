#include "skillcatalog/SkillScanner.h"

#include "core/Logging.h"
#include "core/OpResult.h"
#include "core/Settings.h"
#include "skillcatalog/SkillCache.h"

#include <QJsonArray>
#include <QThreadPool>
#include <QtConcurrent>

namespace awb::skillcatalog {

/**
 * @brief 构造扫描协调者
 *
 * @param settings 设置访问层（存活期须覆盖本对象）
 * @param parent QObject 父项
 */
SkillScanner::SkillScanner(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

/**
 * @brief 取当前生效的根清单
 *
 * @return settings 覆盖过的清单；未配置过（m_roots 为空）时返回默认清单
 */
QList<SkillRoot> SkillScanner::roots() const
{
    return m_roots.isEmpty() ? SkillRoots::defaults() : m_roots;
}

/**
 * @brief 切换一个根的启用状态并持久化
 *
 * @param id 根的稳定标识
 * @param enabled 新的启用状态
 */
void SkillScanner::setRootEnabled(const QString &id, bool enabled)
{
    // 持久化的是完整的生效清单：即使用户从未自定义过根，开关状态也能
    // 活过重启（skills.roots 一旦非空就完全取代默认清单）。
    const QList<SkillRoot> current =
        m_roots.isEmpty() ? SkillRoots::defaults() : m_roots;
    QJsonArray array;
    for (SkillRoot root : current) {
        if (root.id == id) {
            root.enabled = enabled;
        }
        QJsonObject o;
        o[QStringLiteral("id")] = root.id;
        o[QStringLiteral("label")] = root.label;
        o[QStringLiteral("path")] = root.path;
        o[QStringLiteral("kind")] = root.kind;
        o[QStringLiteral("enabled")] = root.enabled;
        array.append(o);
    }
    m_settings->setSkillRoots(array);
    m_settings->save();
    m_roots = SkillRoots::fromJson(array);
}

/**
 * @brief 发起一次异步扫描
 *
 * 扫描进行中再次调用直接忽略（防抖）。流程：把 Settings 读成值类型
 * 快照 SkillScanParams -> QtConcurrent 派发到全局线程池 ->
 * worker 里跑 SkillScanTask::run() 并顺手固化 JSON 缓存 ->
 * QFutureWatcher 的 finished 回到 GUI 线程落地结果并复位状态。
 */
void SkillScanner::refresh()
{
    if (m_scanning) {
        return;
    }

    // Worker 的输入是值类型快照：Settings 是 QObject，必须留在 GUI 线程，
    // 所以这里先把它读成 SkillScanParams 再交给线程池。
    const auto &options = m_settings->skillsOptions();
    const QList<SkillRoot> configured = options.roots.isEmpty()
        ? SkillRoots::defaults()
        : SkillRoots::fromJson(options.roots);
    m_roots = configured;

    SkillScanParams params;
    params.roots = configured;
    params.maxDepth = options.maxDepth;
    params.includePluginCaches = options.includePluginCaches;

    m_scanning = true;
    Q_EMIT scanningChanged();
    Q_EMIT scanStarted();

    // QFutureWatcher 的 finished 回到发起线程（GUI）：结果落地、状态复位
    // 都在这里做；watcher 以 this 为 parent，应用退出时不会泄漏。
    auto *watcher = new QFutureWatcher<SkillScanTask::Result>(this);
    connect(watcher, &QFutureWatcher<SkillScanTask::Result>::finished, this,
            [this, watcher]() {
                const SkillScanTask::Result result = watcher->result();
                watcher->deleteLater();
                m_scanning = false;
                Q_EMIT scanningChanged();
                applyResults(result.definitions, result.stats);
                qInfo().noquote() << QStringLiteral(
                    "SkillScanner: found %1 skill(s) in %2 root(s), %3 "
                    "skipped, %4 duplicate(s) dropped (%5 ms)")
                    .arg(result.stats.skillCount)
                    .arg(result.stats.rootsScanned)
                    .arg(result.stats.rootsSkipped)
                    .arg(result.stats.duplicatesDropped)
                    .arg(result.stats.elapsedMs);
            });
    watcher->setFuture(QtConcurrent::run(QThreadPool::globalInstance(),
                                         [params]() {
                                             const SkillScanTask::Result result =
                                                 SkillScanTask::run(params);
                                             // 缓存固化也在 worker 里做：
                                             // 磁盘 IO 不该由 GUI 线程买单。
                                             const core::OpResult saved =
                                                 SkillCache::save(
                                                     result.definitions,
                                                     result.stats);
                                             if (!saved.ok) {
                                                 qWarning().noquote()
                                                     << QStringLiteral(
                                                         "SkillScanner: could "
                                                         "not persist the "
                                                         "skill cache: %1")
                                                         .arg(saved.error);
                                             }
                                             return result;
                                         }));
}

/**
 * @brief 把现成结果当作最近一次扫描结果采用
 *
 * 启动时从 JSON 缓存恢复走这里，与真扫描共用落地路径（applyResults）。
 *
 * @param definitions 缓存恢复的定义列表
 * @param stats 同一份缓存里的统计
 */
void SkillScanner::adoptResults(const QList<SkillDefinition> &definitions,
                                const Stats &stats)
{
    applyResults(definitions, stats);
}

/**
 * @brief 落地一份扫描结果
 *
 * @param definitions 新的定义列表
 * @param stats 新的统计
 */
void SkillScanner::applyResults(const QList<SkillDefinition> &definitions,
                                const Stats &stats)
{
    m_definitions = definitions;
    m_lastStats = stats;
    Q_EMIT scanFinished();
}

} // namespace awb::skillcatalog
