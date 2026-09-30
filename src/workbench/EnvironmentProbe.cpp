#include "workbench/EnvironmentProbe.h"

#include "core/ProcessRunner.h"
#include "core/TextUtils.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStringList>

namespace awb::workbench {

bool RuntimeState::operator==(const RuntimeState &other) const
{
    return known == other.known && installed == other.installed
           && version == other.version && path == other.path;
}

bool RuntimeState::operator!=(const RuntimeState &other) const
{
    return !(*this == other);
}

namespace {

/// 单个候选程序、单条命令的等待上限（毫秒）。
///
/// 实测本机 `python --version` 约 0.2 s、`node --version` 约 0.4 s（经 cmd
/// 各再慢 0.2~0.3 s），4 s 已经是两个数量级的余量；再长只会让「其实没装」
/// 的机器白等——那台机器上等多久都不会有结果。
constexpr int kCommandTimeoutMs = 4000;
/// 一个运行时的总预算（毫秒）：所有候选、两轮尝试都花这笔钱，用完即收工。
/// 它给 worker 占线程池的时间设了上界，应用退出时不会长时间等探测收尾。
constexpr int kRuntimeBudgetMs = 9000;
/// 候选尝试的轮数：第二轮只重试上一轮「超时/起不来」的候选。
constexpr int kAttemptRounds = 2;

/// 一个运行时的候选程序与两条问话命令。
struct RuntimeSpec
{
    /// 候选程序名（按顺序试，先命中先赢）。
    QStringList candidates;
    /// 问版本：`<程序> --version`。
    QStringList versionArgs;
    /// 问自己装在哪：Python 看 `sys.executable`，Node 看 `process.execPath`。
    /// 实参里一个双引号都没有——Windows 上 QProcess 按 C 惯例转义内嵌引号，
    /// 无引号的脚本可以被原样传递，不用赌各语言自己的 argv 解析。
    QStringList pathArgs;
};

/**
 * @brief Python 的探测规格
 *
 * `python` 之外还试 `py`（Windows 官方的启动器，装在别处时它在、`python`
 * 不在）与 `python3`（非 Windows 上的惯例名）。某些机器上 `python` 是
 * Microsoft Store 的占位程序：跑得起来但只打印一句「去商店装」、没有
 * 版本——那种候选会被判为「跑了但没版本」，继续试下一个。
 *
 * @return 候选与命令参数
 */
RuntimeSpec pythonSpec()
{
    RuntimeSpec spec;
    spec.candidates << QStringLiteral("python") << QStringLiteral("python3")
                    << QStringLiteral("py");
    spec.versionArgs << QStringLiteral("--version");
    spec.pathArgs << QStringLiteral("-c")
                  << QStringLiteral("import sys;print(sys.executable)");
    return spec;
}

/**
 * @brief Node.js 的探测规格
 *
 * @return 候选与命令参数
 */
RuntimeSpec nodeSpec()
{
    RuntimeSpec spec;
    spec.candidates << QStringLiteral("node") << QStringLiteral("nodejs");
    spec.versionArgs << QStringLiteral("--version");
    spec.pathArgs << QStringLiteral("-e")
                  << QStringLiteral("console.log(process.execPath)");
    return spec;
}

/// 真正要跑的程序与参数：.cmd/.bat 垫片必须包一层 cmd.exe（CreateProcess
/// 起不了批处理），正常安装的 .exe 直接跑。
struct Invocation
{
    QString program;   ///< 交给 ProcessRunner 的程序
    QStringList args;  ///< 与之配套的参数
};

/**
 * @brief 由解析出的可执行文件决定真正要跑什么
 *
 * 与 AgentRuntime 的启动路径同一条规则：npm 风格的 .cmd/.bat 垫片不能被
 * CreateProcess 直接执行，得包一层 cmd /c（这也正是「不走 cmd /c」这条
 * 优化只对正常安装的 .exe 生效的原因）。
 *
 * @param resolved 已解析的绝对程序路径
 * @param args 想传给该程序的参数
 * @return 可直接交给 core::ProcessRunner::run() 的程序与参数
 */
Invocation invocationFor(const QString &resolved, const QStringList &args)
{
    Invocation invocation;
    invocation.program = resolved;
    invocation.args = args;
#ifdef Q_OS_WIN
    if (resolved.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive)
        || resolved.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive)) {
        invocation.program = QStringLiteral("cmd");
        invocation.args = QStringList{QStringLiteral("/c"), resolved} + args;
    }
#endif
    return invocation;
}

/// 单个候选的探测结果。
struct CandidateOutcome
{
    /// 结果分类。
    enum class Kind
    {
        Unavailable, ///< PATH 上解析不到这个候选
        Failed,      ///< 超时或起不来（瞬时故障，值得重试）
        NoVersion,   ///< 跑完了但输出里没有版本（占位程序、坏安装）
        OutOfBudget, ///< 运行时预算已用完，没来得及跑
        Found        ///< 拿到了版本
    };

    Kind kind = Kind::Unavailable; ///< 本轮分类
    QString version;               ///< Found 时的版本串
    QString path;                  ///< Found 时的可执行文件路径
    QString note;                  ///< 一行排查记录
};

/**
 * @brief 问一个候选程序它装在哪
 *
 * 失败一律返回空串，由调用方回退到 PATH 解析出来的路径：路径只是展示
 * 信息，不值得为它把整次探测判失败。
 *
 * @param resolved 已解析的绝对程序路径
 * @param spec 运行时规格（取 pathArgs）
 * @param clock 本次运行时探测的计时器（预算共享）
 * @return 绝对路径；问不出来时为空串
 */
QString queryReportedPath(const QString &resolved, const RuntimeSpec &spec,
                          const QElapsedTimer &clock)
{
    const int remaining = kRuntimeBudgetMs - static_cast<int>(clock.elapsed());
    if (remaining <= 0) {
        return QString();
    }
    const Invocation invocation = invocationFor(resolved, spec.pathArgs);
    const core::ProcessResult run = core::ProcessRunner::run(
        invocation.program, invocation.args, qMin(kCommandTimeoutMs, remaining));
    if (!run.error.isEmpty()) {
        return QString();
    }
    return EnvironmentProbe::parseReportedPath(run.stdOut, QString());
}

/**
 * @brief 探测一个候选程序
 *
 * @param candidate 候选程序名
 * @param spec 运行时规格
 * @param clock 本次运行时探测的计时器（预算共享）
 * @return 该候选的结论
 */
CandidateOutcome probeCandidate(const QString &candidate,
                                const RuntimeSpec &spec,
                                const QElapsedTimer &clock)
{
    CandidateOutcome outcome;
    const int remaining = kRuntimeBudgetMs - static_cast<int>(clock.elapsed());
    if (remaining <= 0) {
        outcome.kind = CandidateOutcome::Kind::OutOfBudget;
        outcome.note = QStringLiteral("%1: out of time budget").arg(candidate);
        return outcome;
    }

    const QString resolved = core::ProcessRunner::findExecutable(candidate);
    if (resolved.isEmpty()) {
        outcome.note = QStringLiteral("%1: not on PATH").arg(candidate);
        return outcome;
    }

    // 版本可能落在任一通道：Python 2 与部分发行版把版本打到 stderr。
    const Invocation invocation = invocationFor(resolved, spec.versionArgs);
    const core::ProcessResult run = core::ProcessRunner::run(
        invocation.program, invocation.args, qMin(kCommandTimeoutMs, remaining));
    if (!run.error.isEmpty()) {
        outcome.kind = CandidateOutcome::Kind::Failed;
        outcome.note = QStringLiteral("%1: %2").arg(candidate, run.error);
        return outcome;
    }

    QString version = core::TextUtils::extractVersion(run.stdOut);
    if (version.isEmpty()) {
        version = core::TextUtils::extractVersion(run.stdErr);
    }
    if (version.isEmpty()) {
        // 命令跑完却没版本：Microsoft Store 的占位程序就是这样（打印一句
        // 「去商店装」后退出）。这是「这个候选不可用」的权威信号，但还没
        // 走到「整个运行时不存在」——别的候选可能才是真的。
        outcome.kind = CandidateOutcome::Kind::NoVersion;
        outcome.note = QStringLiteral("%1: no version in output").arg(candidate);
        return outcome;
    }

    outcome.kind = CandidateOutcome::Kind::Found;
    outcome.version = version;
    outcome.path = queryReportedPath(resolved, spec, clock);
    if (outcome.path.isEmpty()) {
        outcome.path = resolved;
    }
    outcome.note = QStringLiteral("%1: %2").arg(candidate, version);
    return outcome;
}

/**
 * @brief 按规格探测一个运行时
 *
 * 结论规则（见 RuntimeProbe::Status 的三态）：
 *  - 有候选报出版本 → Found（候选按顺序试，先命中先赢）；
 *  - 候选跑完但没版本 → 该候选不算数，继续试下一个；
 *  - 任何候选超时或起不来 → Unknown，绝不冒充 Missing；
 *  - 一个候选都没解析到、或解析到的候选全部跑完没版本 → Missing。
 *
 * 最后一个「存在但只会在商店里装」的占位程序也归入 Missing：用户视角
 * 就是这台机器上没有可用的 Python/Node。
 *
 * @param spec 运行时规格
 * @return 该运行时的结论
 */
RuntimeProbe probeRuntime(const RuntimeSpec &spec)
{
    RuntimeProbe probe;
    QStringList notes;
    QStringList pending = spec.candidates;
    bool transientFailure = false;

    // 计时器在整轮内共享：候选数与重试轮数都被 kRuntimeBudgetMs 兜住。
    QElapsedTimer clock;
    clock.start();

    for (int round = 0; round < kAttemptRounds && !pending.isEmpty(); ++round) {
        QStringList retry;
        for (const QString &candidate : std::as_const(pending)) {
            const CandidateOutcome outcome = probeCandidate(candidate, spec,
                                                           clock);
            if (!outcome.note.isEmpty()) {
                notes.append(outcome.note);
            }
            switch (outcome.kind) {
            case CandidateOutcome::Kind::Found:
                probe.status = RuntimeProbe::Status::Found;
                probe.version = outcome.version;
                probe.path = outcome.path;
                probe.detail = notes.join(QStringLiteral("; "));
                return probe;
            case CandidateOutcome::Kind::Failed:
                // 瞬时故障：留着下一轮再试一次。
                transientFailure = true;
                retry.append(candidate);
                break;
            case CandidateOutcome::Kind::OutOfBudget:
                // 没跑完就没有结论，同样按「不知道」处理。
                transientFailure = true;
                break;
            case CandidateOutcome::Kind::NoVersion:
            case CandidateOutcome::Kind::Unavailable:
                break;
            }
        }
        pending = retry;
    }

    probe.status = transientFailure ? RuntimeProbe::Status::Unknown
                                    : RuntimeProbe::Status::Missing;
    probe.detail = notes.join(QStringLiteral("; "));
    return probe;
}

} // namespace

/**
 * @brief 同步探测两个运行时
 *
 * 两个运行时依次探测（互不影响）：一个的故障不该拖住另一个的结论。
 *
 * @return 两个运行时的结论快照
 */
EnvironmentSnapshot EnvironmentProbe::run()
{
    EnvironmentSnapshot snapshot;
    snapshot.python = probeRuntime(pythonSpec());
    snapshot.node = probeRuntime(nodeSpec());
    return snapshot;
}

/**
 * @brief 把一次探测结论并入已知状态
 *
 * @param current 已知状态（缓存里的或上一轮的）
 * @param probe 本轮结论
 * @return 合并后的状态；Unknown 时原样返回 current
 */
RuntimeState EnvironmentProbe::merge(const RuntimeState &current,
                                     const RuntimeProbe &probe)
{
    switch (probe.status) {
    case RuntimeProbe::Status::Unknown:
        return current;
    case RuntimeProbe::Status::Missing: {
        RuntimeState next;
        next.known = true;
        next.installed = false;
        return next;
    }
    case RuntimeProbe::Status::Found: {
        RuntimeState next;
        next.known = true;
        next.installed = true;
        next.version = probe.version;
        next.path = probe.path;
        return next;
    }
    }
    return current;
}

/**
 * @brief 从运行时自报的输出里取可执行文件路径
 *
 * 只认绝对路径行——运行时的启动噪声（警告、实验特性提示）可能先于路径
 * 出现，非绝对路径的行一律跳过。
 *
 * @param output 命令的 stdout
 * @param fallback 没有可用路径行时的返回值
 * @return 绝对路径（分隔符已转为本机风格）；否则为 fallback
 */
QString EnvironmentProbe::parseReportedPath(const QString &output,
                                            const QString &fallback)
{
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString candidate = line.trimmed();
        if (candidate.isEmpty()) {
            continue;
        }
        if (QFileInfo(candidate).isAbsolute()) {
            return QDir::toNativeSeparators(candidate);
        }
    }
    return fallback;
}

} // namespace awb::workbench
