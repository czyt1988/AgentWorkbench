#include "workbench/EnvironmentService.h"

#include "core/Logging.h"
#include "core/OpResult.h"
#include "workbench/EnvironmentCache.h"

#include <QFutureWatcher>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>

namespace awb::workbench {

namespace {

/// 探测拿不到结论后的自动重试间隔（毫秒）。
///
/// 三次之后停手：失败通常是「这台机器上启动子进程就是慢」（冷启动、杀软
/// 扫命令行），而不是会自愈的偶发；无限重试只会让后台一直有进程起来。
/// 停手后仍保留上次的结论，用户也可以手动 Re-detect。
constexpr int kRetryDelaysMs[] = {3000, 15000, 60000};
constexpr int kRetryCount = 3;

/**
 * @brief 把结论渲染成一句日志描述
 *
 * @param state 运行时结论
 * @return "not found" / "unknown" / "<版本> at <路径>"
 */
QString describe(const RuntimeState &state)
{
    if (!state.known) {
        return QStringLiteral("unknown");
    }
    if (!state.installed) {
        return QStringLiteral("not found");
    }
    if (state.path.isEmpty()) {
        return state.version;
    }
    return QStringLiteral("%1 at %2").arg(state.version, state.path);
}

} // namespace

/**
 * @brief 构造检测服务并恢复上次的结论
 *
 * 只读缓存，不起任何进程：装配期不必等探测。缓存文件缺失或过期时属性
 * 保持「没有结论」（unknown），start() 派发的那轮探测随后补齐。
 *
 * @param parent QObject 父项
 */
EnvironmentService::EnvironmentService(QObject *parent)
    : QObject(parent)
{
    const EnvironmentCache::Snapshot cached = EnvironmentCache::load();
    if (!cached.isValid()) {
        return;
    }
    m_python = cached.python;
    m_node = cached.node;
    qInfo().noquote()
        << QStringLiteral("EnvironmentService: restored the cache from %1 "
                          "(Python %2; Node.js %3)")
               .arg(cached.cachedAt.toString(Qt::ISODate),
                    describe(m_python), describe(m_node));
}

/**
 * @brief 派发首次后台探测
 */
void EnvironmentService::start()
{
    beginProbe();
}

/**
 * @brief 立即重测两个运行时
 *
 * 手动重测重置自动重试预算——用户刚装完东西来点它，理应重新拿到三次
 * 重试机会。探测在途时忽略（见 beginProbe 的防抖）。
 */
void EnvironmentService::refresh()
{
    m_retryIndex = 0;
    beginProbe();
}

/**
 * @brief 派发一轮后台探测
 *
 * worker 是纯函数（EnvironmentProbe::run()），只认 PATH、只回值类型快照，
 * 因此在外部线程池上跑是安全的；QFutureWatcher 的 finished 回到发起线程
 * （GUI）落地结果。防抖：在途时忽略——一轮最长十几秒，结束时必然广播。
 */
void EnvironmentService::beginProbe()
{
    if (m_detecting) {
        return;
    }
    m_detecting = true;
    Q_EMIT changed();

    // watcher 以 this 为父：应用退出时不泄漏。反过来，worker 不碰 this，
    // 所以服务先销毁、任务后结束也没有悬垂访问。
    auto *watcher = new QFutureWatcher<EnvironmentSnapshot>(this);
    connect(watcher, &QFutureWatcher<EnvironmentSnapshot>::finished, this,
            [this, watcher]() {
                const EnvironmentSnapshot snapshot = watcher->result();
                watcher->deleteLater();
                m_detecting = false;
                applySnapshot(snapshot);
            });
    watcher->setFuture(QtConcurrent::run(QThreadPool::globalInstance(), []() {
        return EnvironmentProbe::run();
    }));
}

/**
 * @brief 把一轮探测结果并入已知状态
 *
 * 权威结论（found/missing）按 merge 规则覆盖；拿不到结论（unknown）保留
 * 上次的值并安排重试。只有结论真的变了才重写缓存、才记一条 INFO：正常
 * 机器上一次启动最多写这一行。
 *
 * @param snapshot 本轮两个运行时的结论
 */
void EnvironmentService::applySnapshot(const EnvironmentSnapshot &snapshot)
{
    m_pythonDetail = snapshot.python.detail;
    m_nodeDetail = snapshot.node.detail;

    const RuntimeState previousPython = m_python;
    const RuntimeState previousNode = m_node;
    m_python = EnvironmentProbe::merge(m_python, snapshot.python);
    m_node = EnvironmentProbe::merge(m_node, snapshot.node);

    if (m_python != previousPython || m_node != previousNode) {
        qInfo().noquote()
            << QStringLiteral("EnvironmentService: Python %1; Node.js %2")
                   .arg(describe(m_python), describe(m_node));
        persistCache();
    }

    const bool inconclusive =
        snapshot.python.status == RuntimeProbe::Status::Unknown
        || snapshot.node.status == RuntimeProbe::Status::Unknown;
    if (inconclusive) {
        // 保留上次结论时要说清「这轮其实没测出来」，否则日志里看不出
        // 界面上的版本其实来自缓存。
        if (snapshot.python.status == RuntimeProbe::Status::Unknown) {
            qWarning().noquote()
                << QStringLiteral("EnvironmentService: the Python probe was "
                                  "inconclusive, keeping \"%1\": %2")
                       .arg(describe(m_python), m_pythonDetail);
        }
        if (snapshot.node.status == RuntimeProbe::Status::Unknown) {
            qWarning().noquote()
                << QStringLiteral("EnvironmentService: the Node.js probe was "
                                  "inconclusive, keeping \"%1\": %2")
                       .arg(describe(m_node), m_nodeDetail);
        }
        scheduleRetry();
    } else {
        m_retryIndex = 0;
    }

    // 结果没变也要发：detecting 从 true 翻回 false 靠它广播。
    Q_EMIT changed();
}

/**
 * @brief 安排一次自动重试
 *
 * 预算用尽后停手并记一条 WARNING：保留的结论仍是界面上显示的值，用户
 * 按 Re-detect 或重启可以再来一轮。
 */
void EnvironmentService::scheduleRetry()
{
    if (m_retryIndex >= kRetryCount) {
        qWarning().noquote()
            << QStringLiteral("EnvironmentService: giving up after %1 retries; "
                              "the last known version(s) stay on screen until "
                              "the next re-detect or restart")
                   .arg(kRetryCount);
        return;
    }
    const int delayMs = kRetryDelaysMs[m_retryIndex];
    ++m_retryIndex;
    QTimer::singleShot(delayMs, this, [this]() { beginProbe(); });
}

/**
 * @brief 把当前两个结论写进缓存
 */
void EnvironmentService::persistCache()
{
    const core::OpResult saved = EnvironmentCache::save(m_python, m_node);
    if (!saved.ok) {
        qWarning().noquote()
            << QStringLiteral("EnvironmentService: could not persist the "
                              "environment cache: %1")
                   .arg(saved.error);
    }
}

/**
 * @brief 取结论对应的 QML 状态字符串
 *
 * @param state 运行时结论
 * @return "unknown" / "missing" / "found"
 */
QString EnvironmentService::statusKey(const RuntimeState &state)
{
    if (!state.known) {
        return QStringLiteral("unknown");
    }
    return state.installed ? QStringLiteral("found") : QStringLiteral("missing");
}

} // namespace awb::workbench
