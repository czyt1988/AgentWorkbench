#include "skillcatalog/SkillScanner.h"

#include "core/Logging.h"
#include "core/OpResult.h"
#include "core/Settings.h"
#include "skillcatalog/SkillCache.h"

#include <QJsonArray>
#include <QThreadPool>
#include <QtConcurrent>

namespace awb::skillcatalog {

SkillScanner::SkillScanner(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QList<SkillRoot> SkillScanner::roots() const
{
    return m_roots.isEmpty() ? SkillRoots::defaults() : m_roots;
}

void SkillScanner::setRootEnabled(const QString &id, bool enabled)
{
    // Persist the full effective list so toggles survive a restart even
    // when the user never customized the roots (a non-empty
    // skills.roots completely replaces the defaults).
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

void SkillScanner::adoptResults(const QList<SkillDefinition> &definitions,
                                const Stats &stats)
{
    applyResults(definitions, stats);
}

void SkillScanner::applyResults(const QList<SkillDefinition> &definitions,
                                const Stats &stats)
{
    m_definitions = definitions;
    m_lastStats = stats;
    Q_EMIT scanFinished();
}

} // namespace awb::skillcatalog
