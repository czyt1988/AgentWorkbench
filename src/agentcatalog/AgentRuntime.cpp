#include "agentcatalog/AgentRuntime.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentUrls.h"
#include "core/HttpProbe.h"
#include "core/Paths.h"
#include "core/ProcessRunner.h"
#include "core/TextUtils.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace awb::agentcatalog {

namespace {

// 会话 URL 的监视预算：500 ms 一个刻度、共 60 s——dsh 日志显示冷启动时
// 从 launch 到打印 URL 约 35 s。
constexpr int kSessionUrlTickMs = 500;
constexpr int kSessionUrlTicks = 120;

/**
 * @brief 拼运行日志的前缀
 *
 * 形如 "[cmd] launch \"qwen\": "。与 0.3.0 保持一致，既有的日志解析
 * 依赖它。
 *
 * @param tag       分类标签（"cmd" 或 "app"）
 * @param operation 操作名（launch、stop…）
 * @param id        agent id；空串时省去 id 段
 * @return 可直接拼接消息的前缀
 */
QString logPrefix(const QString &tag, const QString &operation, const QString &id)
{
    return id.isEmpty()
               ? QStringLiteral("[%1] %2: ").arg(tag, operation)
               : QStringLiteral("[%1] %2 \"%3\": ").arg(tag, operation, id);
}

/**
 * @brief 记一条 [cmd] 级的运行日志
 *
 * @param operation 操作名
 * @param id        agent id
 * @param message   消息（英文）
 */
void cmdLog(const QString &operation, const QString &id, const QString &message)
{
    qInfo().noquote() << logPrefix(QStringLiteral("cmd"), operation, id) + message;
}

/**
 * @brief 记一条 [cmd] 级的告警日志
 *
 * @param operation 操作名
 * @param id        agent id
 * @param message   消息（英文）
 */
void cmdLogError(const QString &operation, const QString &id, const QString &message)
{
    qWarning().noquote() << logPrefix(QStringLiteral("cmd"), operation, id) + message;
}

/**
 * @brief 记一条 [app] 级的运行日志
 *
 * @param operation 操作名
 * @param id        agent id；空串表示应用级事件
 * @param message   消息（英文）
 */
void appLog(const QString &operation, const QString &id, const QString &message)
{
    qInfo().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

/**
 * @brief 记一条 [app] 级的告警日志
 *
 * @param operation 操作名
 * @param id        agent id；空串表示应用级事件
 * @param message   消息（英文）
 */
void appLogError(const QString &operation, const QString &id, const QString &message)
{
    qWarning().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

} // namespace

/**
 * @brief 构造运行器
 *
 * @param model  agent 列表来源，launch 状态写回它
 * @param parent QObject 父项
 */
AgentRuntime::AgentRuntime(AgentModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    // 轮询重定向出来的输出找会话 URL。500 ms 对磁盘很温和（每次 launch
    // 只读几次文件），又能在 launch 后第一次健康检查之前完成换靶。
    m_sessionUrlTimer.setInterval(kSessionUrlTickMs);
    m_sessionUrlTimer.setSingleShot(false);
    connect(&m_sessionUrlTimer, &QTimer::timeout, this, [this]() {
        const QList<QString> ids = m_sessionUrlWatch.keys();
        for (const QString &id : ids) {
            watchSessionUrl(id);
        }
    });
}

/**
 * @brief 启动 agent 的进程
 *
 * 启动命令按空白拆分、经 PATH 解析（Windows 上含 PATHEXT，找得到 npm
 * 风格的 .cmd/.bat 垫片），垫片再包一层 cmd /c 执行；stdout+stderr 重定向
 * 到 <logsDir>/output/<id>.log 供会话 URL 监视。启动成功后卡片进入
 * "launching" 态，直到健康检查确认起来或 30 s 安全超时清掉（代数防旧
 * 尝试的超时误清新一次启动）。
 *
 * @param definition  agent 定义
 * @param tokenValue  从 tokenFile 读出的 token；定义未配置 tokenFile 时
 *                    忽略
 */
void AgentRuntime::launch(const AgentDefinition &definition,
                          const QString &tokenValue)
{
    const QString id = definition.id;

    // 新一次启动清掉旧的 install/setup 日志，运行中的卡片不能停在
    // 过时的控制台输出上。
    m_model->setConsoleOutput(id, QString());

    // 命令按空白拆成程序 + 参数。两个 Qt 版本都走 ProcessRunner 的
    // 可移植实现（QProcess::splitCommand 只有 Qt 6 才有）。
    const QStringList parts = core::ProcessRunner::splitCommand(definition.command);
    if (parts.isEmpty()) {
        cmdLogError(QStringLiteral("launch"), id,
                    QStringLiteral("skipped, the startup command is empty"));
        Q_EMIT launchFailed(id, tr("Startup command is empty."));
        return;
    }

    const QString program = parts.first();
    const QStringList args = parts.mid(1);

    // 裸程序名经 PATH 解析（Windows 上含 PATHEXT），npm 风格的 .cmd/.bat
    // 垫片（如 "qwen" -> "qwen.cmd"）才找得到。CreateProcess 自己不会
    // 试这些扩展名——"qwen serve" 之前就是这么静默失败的。
    const QString resolved = core::ProcessRunner::findExecutable(program);
    if (resolved.isEmpty()) {
        const QString msg = tr("Cannot find '%1' on your PATH. "
                                        "Make sure it is installed and on PATH.")
                                .arg(program);
        cmdLogError(QStringLiteral("launch"), id,
                    QStringLiteral("cannot resolve '%1' on PATH "
                                   "(configured command: %2)")
                        .arg(program, definition.command));
        Q_EMIT launchFailed(id, msg);
        return;
    }

    // 拼出真正执行的命令行：.cmd/.bat 垫片不能被 CreateProcess 直接执行，
    // 要包一层 cmd.exe（之后 /T 一次 kill 就覆盖整棵 cmd -> qwen.cmd ->
    // node 进程树）。
    QString execProgram = resolved;
    QStringList execArgs = args;
#ifdef Q_OS_WIN
    if (resolved.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive)
        || resolved.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive)) {
        execProgram = QStringLiteral("cmd");
        execArgs = QStringList{QStringLiteral("/c"), resolved} + args;
    }
#endif
    const QString cwd =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    // 日志为这一行而存在：PATH 解析与 cmd.exe 包装之后真正执行的是什么。
    cmdLog(QStringLiteral("launch"), id,
           QStringLiteral("running: %1  (cwd: %2)")
               .arg(core::TextUtils::formatCommandLine(execProgram, execArgs),
                    cwd));

    // 配了 token 文件时把 token 作为 QWEN_SERVER_TOKEN 交给子进程，
    // 免去命令行上复杂的 --token 参数。
    QProcessEnvironment env;
    if (!definition.tokenFile.isEmpty()) {
        if (!tokenValue.isEmpty()) {
            env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("QWEN_SERVER_TOKEN"), tokenValue);
            // token 值本身从不进日志。
            cmdLog(QStringLiteral("launch"), id,
                   QStringLiteral("injecting QWEN_SERVER_TOKEN from the "
                                  "configured token file"));
        } else {
            cmdLogError(QStringLiteral("launch"), id,
                        QStringLiteral("token file is empty or unreadable"));
        }
    }

    qint64 pid = 0;
    QString startError;
    // 带 token 门禁的 harness 把鉴权 URL 打到 stdout。收进每次 launch
    // 独立的日志文件（健康探测只能证明端口起来了，裸 webUrl 会被 401
    // 挡回——只有打印出来的那个 URL 可用）。
    const QString outputDir =
            core::Paths::logsDir() + QStringLiteral("/output");
    QDir().mkpath(outputDir);
    const QString outputFile = outputDir + QLatin1Char('/') + id
                               + QStringLiteral(".log");
    const bool ok = core::ProcessRunner::startDetached(
            execProgram, execArgs, &pid, &startError, cwd, env, outputFile);
    if (!ok) {
        cmdLogError(QStringLiteral("launch"), id,
                    QStringLiteral("failed to start: %1").arg(startError));
        Q_EMIT launchFailed(id, tr("Failed to start '%1'.").arg(program));
        return;
    }

    m_pids.insert(id, pid);
    cmdLog(QStringLiteral("launch"), id,
           QStringLiteral("started, pid %1").arg(pid));

    // 输出里指向同一服务器的 URL 就是会话 URL（dsh）；宽限期后仍无匹配
    //（qwen 等从不打印）则安静地结束监视。
    if (!definition.webUrl.isEmpty()) {
        m_sessionUrlWatch.insert(id, {definition.webUrl, definition.tokenFile,
                                      kSessionUrlTicks});
        if (!m_sessionUrlTimer.isActive()) {
            m_sessionUrlTimer.start();
        }
    }

    // 把卡片标成 "launching"：操作按钮显示转圈，直到健康检查确认服务
    // 起来——或者 30 s 安全超时兜底（agent 启动即崩时）。代数用来挡上
    // 一次尝试的陈旧超时误清新的启动。
    const int epoch = ++m_launchEpoch[id];
    m_model->setLaunching(id, true);
    QTimer::singleShot(30000, this, [this, id, epoch]() {
        if (m_launchEpoch.value(id) == epoch) {
            m_model->setLaunching(id, false);
        }
    });

    // 稍后复查一次，让卡片尽快翻成运行中。
    QTimer::singleShot(1500, this, &AgentRuntime::recheckRequested);
}

/**
 * @brief 结束本次会话中由此启动器启动的进程树
 *
 * 只有由本启动器启动的进程才有 PID：健康检查判定为运行中、但并非此处
 * 启动的 agent 走 stop() 只会收到提示（见 launchFailed 的消息），要用它
 * 们自己的命令或 forceStop() 停止。
 *
 * @param id agent id
 * @return 成功发起 kill 返回 true；没有记账的 PID 返回 false（同时发
 *         launchFailed 说明原因）
 */
bool AgentRuntime::stop(const QString &id)
{
    const auto it = m_pids.constFind(id);
    if (it == m_pids.constEnd() || *it == 0) {
        const QString msg =
            tr("This agent wasn't started from the launcher; "
                        "stop it with its own command.");
        cmdLogError(QStringLiteral("stop"), id,
                    QStringLiteral("no PID tracked in this launcher session, "
                                   "nothing to kill"));
        Q_EMIT launchFailed(id, msg);
        return false;
    }

    const qint64 pid = *it;
    m_pids.erase(it);
    // 每进程 token 已随进程消亡——下一次 launch 抓到新 URL 之前，
    // 只剩裸 webUrl 可开。
    dropSessionUrl(id);

    const QString killProgram = core::ProcessRunner::killProgram();
    const QStringList args = core::ProcessRunner::killProgramArgs(pid);
    cmdLog(QStringLiteral("stop"), id,
           QStringLiteral("running: %1")
               .arg(core::TextUtils::formatCommandLine(killProgram, args)));
    const bool ok = core::ProcessRunner::startDetached(killProgram, args);
    if (!ok) {
        cmdLogError(QStringLiteral("stop"), id,
                    QStringLiteral("failed to kill pid %1").arg(pid));
        Q_EMIT launchFailed(id, tr("Failed to stop process (PID %1).")
                                  .arg(pid));
    } else {
        cmdLog(QStringLiteral("stop"), id,
               QStringLiteral("killed process tree, pid %1").arg(pid));
    }

    // 稍后复查，端口下去之后卡片翻回「已停止」。
    QTimer::singleShot(500, this, &AgentRuntime::recheckRequested);
    return ok;
}

/**
 * @brief 按端口强制结束占用该 agent web 端口的进程
 *
 * 对并非本启动器启动的 agent 也有效（右键菜单的显式动作）：没有 PID 可
 * 依，就经 netstat/lsof 按端口找。若本启动器同时记着该 agent 的 PID，
 * 一并清掉，避免之后普通的 stop() 去杀已经死掉的 PID。
 *
 * @param id agent id
 */
void AgentRuntime::forceStop(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return;
    }
    const AgentDefinition def = m_model->definitions().at(row);

    // 不是这里启动的没有 PID 记账，改按端口定位。
    const int port = core::HttpProbe::portFromUrl(def.webUrl);
    if (port < 0) {
        const QString msg = tr("Cannot determine port from web URL.");
        cmdLogError(QStringLiteral("forceStop"), id,
                    QStringLiteral("cannot determine a port from web URL '%1'")
                        .arg(def.webUrl));
        Q_EMIT launchFailed(id, msg);
        return;
    }

    const QList<qint64> pids = findPidsForPort(port);
    if (pids.isEmpty()) {
        const QString msg =
            tr("No process found listening on port %1; "
                        "the agent may already be stopped.").arg(port);
        cmdLogError(QStringLiteral("forceStop"), id,
                    QStringLiteral("no process listening on port %1").arg(port));
        Q_EMIT launchFailed(id, msg);
        return;
    }

    QStringList pidList;
    for (const qint64 pid : pids) {
        pidList << QString::number(pid);
    }
    const QString pidsText = pidList.join(QStringLiteral(", "));
    cmdLog(QStringLiteral("forceStop"), id,
           QStringLiteral("port %1 is held by pid(s) %2").arg(port).arg(pidsText));

    bool anyOk = false;
    const QString killProgram = core::ProcessRunner::killProgram();
    for (const qint64 pid : pids) {
        const QStringList args = core::ProcessRunner::killProgramArgs(pid);
        cmdLog(QStringLiteral("forceStop"), id,
               QStringLiteral("running: %1")
                   .arg(core::TextUtils::formatCommandLine(killProgram, args)));
        if (core::ProcessRunner::startDetached(killProgram, args)) {
            anyOk = true;
        }
    }

    // 本启动器也记着该 agent 的 PID 时一并清掉，之后的普通 stop() 不会
    // 去杀一个已经死掉的 PID。
    m_pids.remove(id);
    dropSessionUrl(id);

    if (!anyOk) {
        const QString msg =
            tr("Failed to stop process (PID %1).").arg(pids.constFirst());
        cmdLogError(QStringLiteral("forceStop"), id,
                    QStringLiteral("failed to kill pid(s) %1").arg(pidsText));
        Q_EMIT launchFailed(id, msg);
    } else {
        cmdLog(QStringLiteral("forceStop"), id,
               QStringLiteral("killed the process tree(s) holding port %1")
                   .arg(port));
    }

    // 稍后复查，端口下去之后卡片翻回「已停止」。
    QTimer::singleShot(500, this, &AgentRuntime::recheckRequested);
}

/**
 * @brief 判断本次会话是否启动过 agent
 *
 * @return 至少记着一个 PID 时返回 true
 */
bool AgentRuntime::hasLaunchedAgents() const
{
    return !m_pids.isEmpty();
}

/**
 * @brief 结束本次会话启动的全部进程
 *
 * 退出路径调用。逐个 PID 杀进程树；无论成败都清空全部记账与会话 URL
 * 监视。
 *
 * @return 成功杀掉的进程树数量
 */
int AgentRuntime::stopAll()
{
    const int tracked = m_pids.size();
    int killed = 0;
    const QString killProgram = core::ProcessRunner::killProgram();
    for (auto it = m_pids.constBegin(); it != m_pids.constEnd(); ++it) {
        const qint64 pid = *it;
        if (pid == 0) {
            continue;
        }
        const QStringList args = core::ProcessRunner::killProgramArgs(pid);
        cmdLog(QStringLiteral("stopAll"), it.key(),
               QStringLiteral("running: %1")
                   .arg(core::TextUtils::formatCommandLine(killProgram, args)));
        if (core::ProcessRunner::startDetached(killProgram, args)) {
            ++killed;
        }
    }
    m_pids.clear();
    m_sessionUrls.clear();
    m_sessionUrlWatch.clear();
    m_sessionUrlTimer.stop();
    appLog(QStringLiteral("stopAll"), QString(),
           QStringLiteral("terminated %1 of %2 launcher(s) started this session")
               .arg(killed)
               .arg(tracked));
    return killed;
}

/**
 * @brief 忘掉该 id 的全部记账
 *
 * agent 从配置里移除时用：清 PID、launch 代数与会话 URL，agent 进程
 * 本身继续运行。
 *
 * @param id agent id
 */
void AgentRuntime::forget(const QString &id)
{
    m_pids.remove(id);
    m_launchEpoch.remove(id);
    dropSessionUrl(id);
}

/**
 * @brief 取为该 agent 抓到的会话 URL
 *
 * @param id agent id
 * @return 已合并 token 的会话 URL；本次会话没抓到（或已随停止丢弃）返回空串
 */
QString AgentRuntime::sessionUrl(const QString &id) const
{
    return m_sessionUrls.value(id);
}

/**
 * @brief 轮询一次该 id 的输出日志，尝试抓会话 URL
 *
 * 由 m_sessionUrlTimer 每 500 ms 调一次。日志里出现指向 webUrl 同一
 * 服务器的 URL 即捕获成功：经 AgentUrls::finalUrl 合并 token 后存入
 * m_sessionUrls 并发 sessionUrlChanged，随后撤销监视。宽限期（剩余
 * 轮询次数）耗尽则安静放弃——有些 agent 从不打印 URL。
 *
 * @param id agent id
 * @sa AgentUrls::sessionUrlFromOutput
 */
void AgentRuntime::watchSessionUrl(const QString &id)
{
    const auto it = m_sessionUrlWatch.find(id);
    if (it == m_sessionUrlWatch.end()) {
        return;
    }
    SessionWatch watch = it.value();

    QString text;
    {
        QFile file(core::Paths::logsDir() + QStringLiteral("/output/")
                    + id + QStringLiteral(".log"));
        if (file.open(QIODevice::ReadOnly)) {
            text = core::ProcessRunner::decodeOutput(file.readAll());
        }
    }

    const QString captured = AgentUrls::sessionUrlFromOutput(text, watch.webUrl);
    if (!captured.isEmpty()) {
        const QString url = AgentUrls::finalUrl(captured, watch.tokenFile);
        m_sessionUrls.insert(id, url);
        m_sessionUrlWatch.erase(it);
        if (m_sessionUrlWatch.isEmpty()) {
            m_sessionUrlTimer.stop();
        }
        // URL 本身从不进日志——它带着 token。
        cmdLog(QStringLiteral("session-url"), id,
               QStringLiteral("captured an authenticated URL from the "
                              "agent output"));
        Q_EMIT sessionUrlChanged(id, url);
        return;
    }

    if (--watch.attemptsLeft <= 0) {
        m_sessionUrlWatch.erase(it);
        if (m_sessionUrlWatch.isEmpty()) {
            m_sessionUrlTimer.stop();
        }
    } else {
        it.value() = watch;
    }
}

/**
 * @brief 丢弃该 id 的会话 URL 与在途监视
 *
 * 会话 URL 带着每进程的 token，进程一死 token 就失效，继续用只会得到
 * 401；监视也一并撤销，最后一个撤销时停掉轮询定时器。
 *
 * @param id agent id
 */
void AgentRuntime::dropSessionUrl(const QString &id)
{
    m_sessionUrls.remove(id);
    m_sessionUrlWatch.remove(id);
    if (m_sessionUrlWatch.isEmpty()) {
        m_sessionUrlTimer.stop();
    }
}

/**
 * @brief 找出监听给定 TCP 端口的进程
 *
 * Windows 走 netstat -ano -p tcp：一行一个连接（协议、本地地址、外部
 * 地址、状态、PID），只保留本地地址以 ":<port>" 结尾的 LISTENING 行
 * ——不碰外部地址列，也避免 ":3000" 误中 ":53000" 这类更长端口（前导
 * 冒号是分隔符）。其它平台走 lsof -ti :<port>，一行一个 PID。结果按
 * 出现顺序去重。
 *
 * @param port TCP 端口
 * @return 监听该端口的 PID 列表；命令超时（5 s）或无监听时为空
 */
QList<qint64> AgentRuntime::findPidsForPort(int port)
{
    QList<qint64> pids;
    QSet<qint64> seen;

    QProcess proc;
#ifdef Q_OS_WIN
    proc.setProgram(QStringLiteral("cmd"));
    proc.setArguments({QStringLiteral("/c"),
                       QStringLiteral("netstat -ano -p tcp")});
#else
    // lsof -ti :<port> 只打印持有端口的 PID，一行一个。
    proc.setProgram(QStringLiteral("lsof"));
    proc.setArguments({QStringLiteral("-ti"),
                       QStringLiteral(":%1").arg(port)});
#endif
    proc.start();
    if (!proc.waitForFinished(5000)) {
        return pids;
    }

    const QString output =
        core::ProcessRunner::decodeOutput(proc.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'));
    const QString portSuffix = QStringLiteral(":%1").arg(port);

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
#ifdef Q_OS_WIN
        const QStringList cols =
            trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (cols.size() < 5) {
            continue;
        }
        bool isListening = false;
        for (const QString &c : cols) {
            if (c == QStringLiteral("LISTENING")) {
                isListening = true;
                break;
            }
        }
        if (!isListening) {
            continue;
        }
        // cols[0]=协议，cols[1]=本地地址，cols[2]=外部地址，其后是状态、PID。
        if (!cols.at(1).endsWith(portSuffix, Qt::CaseInsensitive)) {
            continue;
        }
        bool ok = false;
        const qint64 pid = cols.constLast().toLongLong(&ok);
        if (ok && pid > 0 && !seen.contains(pid)) {
            seen.insert(pid);
            pids.append(pid);
        }
#else
        bool ok = false;
        const qint64 pid = trimmed.toLongLong(&ok);
        if (ok && pid > 0 && !seen.contains(pid)) {
            seen.insert(pid);
            pids.append(pid);
        }
#endif
    }
    return pids;
}

} // namespace awb::agentcatalog
