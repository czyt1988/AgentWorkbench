#include "agentcatalog/AgentScripts.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentStateStore.h"
#include "core/Logging.h"
#include "core/ScriptRunner.h"
#include "core/TextUtils.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

namespace awb::agentcatalog {

namespace {

// 运行日志的辅助函数，与 0.3.0 逐字节兼容（"[cmd] install \"opencode\":
// running: cmd /c …"、"done, exit=0"）。

/**
 * @brief 拼运行日志的前缀
 *
 * 形如 "[cmd] install \"opencode\": "。
 *
 * @param tag       分类标签（"cmd" 或 "app"）
 * @param operation 操作名（install、update…）
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
 * @brief 距 startMs 经过了多久
 *
 * @param startMs 起始时刻（毫秒纪元）
 * @return 形如 "1.2s" 的耗时文本
 */
QString elapsedSince(qint64 startMs)
{
    return QStringLiteral("%1s")
        .arg((QDateTime::currentMSecsSinceEpoch() - startMs) / 1000.0, 0, 'f', 1);
}

/**
 * @brief 结束进程的结局摘要
 *
 * @param exitCode 退出码
 * @param startMs  起始时刻，用于算耗时
 * @return 形如 "done, exit=0, 31.2s"（非零退出码时 "FAILED"）
 */
QString exitSummary(int exitCode, qint64 startMs)
{
    return QStringLiteral("%1, exit=%2, %3")
        .arg(exitCode == 0 ? QStringLiteral("done") : QStringLiteral("FAILED"))
        .arg(exitCode)
        .arg(elapsedSince(startMs));
}

/**
 * @brief 启动器运行原始命令串的形态
 *
 * @param command 原始命令串
 * @return "cmd /c <command>"
 */
QString shellCommandLine(const QString &command)
{
    return QStringLiteral("cmd /c ") + command;
}

/**
 * @brief 把结束命令的输出原样记进日志
 *
 * 截断到日志上限，免得一条话痨命令灌满日志；failure 决定记告警还是
 * 信息级别，与对应结局行的级别一致。
 *
 * @param operation 操作名
 * @param id        agent id
 * @param output    命令的累计输出
 * @param failure   失败结局时为 true（记为告警）
 */
void logCommandOutput(const QString &operation, const QString &id,
                      const QString &output, bool failure = false)
{
    const QString text =
        core::TextUtils::clampOutput(output, core::Logging::DEFAULT_MAX_OUTPUT)
            .trimmed();
    const QString line = logPrefix(QStringLiteral("cmd"), operation, id)
                         + (text.isEmpty()
                                ? QStringLiteral("output: (none)")
                                : QStringLiteral("output:\n") + text);
    if (failure) {
        qWarning().noquote() << line;
    }
    else {
        qInfo().noquote() << line;
    }
}

/**
 * @brief 版本探测的自动重试预算
 *
 * 一轮：环境探测那边有 3/15/60 s 的阶梯，这里不学它——版本探测失败几乎
 * 全部发生在启动拥堵的几秒里，错开一轮足够；真挂死的命令重试多少次都
 * 一样。预算用尽后结论停在「不知道」，等显式 checkVersion 再来。
 */
constexpr int kVersionRetryLimit = 1;

/**
 * @brief 拼脚本运行 key
 *
 * 一个操作一个 ScriptRunner 槽位，同一 agent 上的并发操作（安装期间
 * 查版本）因此互不相杀。
 *
 * @param operation 操作名（install/update/setup/version）
 * @param id        agent id
 * @return "<operation>:<id>"
 */
QString scriptKey(const QString &operation, const QString &id)
{
    return operation + QLatin1Char(':') + id;
}

/**
 * @brief 拆开脚本运行 key
 *
 * @param key       运行 key
 * @param operation 拆出的操作名（出参）
 * @param id        拆出的 agent id（出参）
 * @return key 形如 "<operation>:<id>" 时返回 true
 */
bool splitScriptKey(const QString &key, QString &operation, QString &id)
{
    const int sep = key.indexOf(QLatin1Char(':'));
    if (sep <= 0) {
        return false;
    }
    operation = key.left(sep);
    id = key.mid(sep + 1);
    return true;
}

} // namespace

/**
 * @brief 构造一次性命令运行器
 *
 * @param model      agent 列表来源，操作状态写回它
 * @param stateStore setup 完成状态的落盘处
 * @param parent     QObject 父项
 */
AgentScripts::AgentScripts(AgentModel *model, AgentStateStore *stateStore,
                           QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_stateStore(stateStore)
{
    // runner 每个 scripts 对象一个，key 因此保持模块私有；
    // AgentRuntime 里的槽绝不会碰到它。
    m_runner = new core::ScriptRunner(this);
    connect(m_runner, &core::ScriptRunner::outputChunk, this,
            &AgentScripts::onScriptChunk);
    connect(m_runner, &core::ScriptRunner::finished, this,
            &AgentScripts::onScriptFinished);
}

// --- Install / Update ------------------------------------------------------

/**
 * @brief 运行该 agent 的安装命令
 *
 * 前置条件与 0.3.0 一致：agent 运行中或未配置 installCommand 时拒绝
 * （分别发 launchFailed / installFinished(false)）。命令经 cmd /c 运行、
 * 不限时、通道合并，输出实时上卡片；结束时 onScriptFinished 收尾。
 *
 * @param id agent id
 */
void AgentScripts::install(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return;
    }
    const AgentDefinition a = m_model->definitions().at(row);

    if (m_model->state(id).running) {
        cmdLogError(QStringLiteral("install"), id,
                    QStringLiteral("skipped, the agent is running"));
        Q_EMIT launchFailed(id, tr("Please close %1 before installing/updating.")
                                  .arg(a.name));
        return;
    }
    if (a.installCommand.isEmpty()) {
        cmdLogError(QStringLiteral("install"), id,
                    QStringLiteral("skipped, no install command is configured"));
        Q_EMIT installFinished(id, false,
            tr("No install command configured for %1.").arg(a.name));
        return;
    }

    // 先清上一次的输出再翻到 "installing"：面板（重新）打开时不闪现
    // 上一轮的旧文本。
    m_model->setConsoleOutput(id, QString());
    m_model->setInstalling(id, true);

    const QString key = scriptKey(QStringLiteral("install"), id);
    m_buffers.remove(key);
    m_startMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    // 不弹控制台窗口；输出收进来供实时显示。
    cmdLog(QStringLiteral("install"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(a.installCommand)));
    m_runner->runShell(key, a.installCommand, 0, true);
}

/**
 * @brief 运行该 agent 的更新命令
 *
 * 前置条件与失败上报同 install()；命令经 cmd /c 运行、不限时、通道合并。
 *
 * @param id agent id
 */
void AgentScripts::update(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return;
    }
    const AgentDefinition a = m_model->definitions().at(row);

    if (m_model->state(id).running) {
        cmdLogError(QStringLiteral("update"), id,
                    QStringLiteral("skipped, the agent is running"));
        Q_EMIT launchFailed(id, tr("Please close %1 before installing/updating.")
                                  .arg(a.name));
        return;
    }
    if (a.updateCommand.isEmpty()) {
        cmdLogError(QStringLiteral("update"), id,
                    QStringLiteral("skipped, no update command is configured"));
        Q_EMIT installFinished(id, false,
            tr("No update command configured for %1.").arg(a.name));
        return;
    }

    m_model->setInstalling(id, true);
    m_model->setConsoleOutput(id, QString());

    const QString key = scriptKey(QStringLiteral("update"), id);
    m_buffers.remove(key);
    m_startMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    // 不弹控制台窗口；输出收进来供实时显示。
    cmdLog(QStringLiteral("update"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(a.updateCommand)));
    m_runner->runShell(key, a.updateCommand, 0, true);
}

// --- Setup（首跑前置） ------------------------------------------------------

/**
 * @brief 运行该 agent 的一次性 setup 命令
 *
 * 未配置 setupCommand 时静默返回（setup 是可选的）。命令写进临时
 * .cmd 文件再执行（绕开 cmd.exe 的引号问题），30 s 安全超时杀掉挂死
 * 的 setup；输出实时上卡片，结束时 onScriptFinished 收尾。
 *
 * @param id agent id
 */
void AgentScripts::runSetup(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return;
    }
    const QString cmd = m_model->definitions().at(row).setupCommand;
    if (cmd.isEmpty()) {
        return;
    }

    // 先清上一次的输出再翻到 "setting up"：面板（重新）打开时不闪现
    // 上一轮的旧文本。
    m_model->setConsoleOutput(id, QString());
    m_model->setSetupping(id, true);

    cmdLog(QStringLiteral("setup"), id,
           QStringLiteral("configured command: %1").arg(cmd));
    cmdLog(QStringLiteral("setup"), id,
           QStringLiteral("running: cmd /c <temporary .cmd batch> "
                          "(command quoted in the line above)"));

    const QString key = scriptKey(QStringLiteral("setup"), id);
    m_buffers.remove(key);
    m_startMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    m_setupCommands.insert(key, cmd);
    // 批处理文件绕开 cmd.exe 的引号问题（见 ScriptRunner::runBatch）；
    // 30 s 安全超时杀掉挂死的 setup。
    m_runner->runBatch(key, cmd, 30000);
}

// --- 版本探测 ---------------------------------------------------------------

/**
 * @brief 逐个运行所有 agent 的 versionCommand
 *
 * 启动时的版本检查走这里。各 agent 错开 m_staggerMs 出发（第一个立即）：
 * N 个探测同时起进程会在进程创建处排队——杀软/DLP 对每个新进程做串行
 * 扫描的机器上实测把同时出发的检查全部拖过超时（0.4.x 五张卡片全灭）。
 * spinner 由 AgentsFacade::start() 预先点亮，错峰只推迟真实起进程的时机，
 * 界面上看不出空档。未配置命令的 agent 静默跳过；期间被手动检查接管的
 * agent（epoch 已变）不再重复出发。
 */
void AgentScripts::checkVersions()
{
    int launched = 0;
    for (const AgentDefinition &a : m_model->definitions()) {
        if (a.versionCommand.isEmpty()) {
            continue;
        }
        m_versionRetries.insert(a.id, 0);
        const int delay = launched * m_staggerMs;
        ++launched;
        if (delay == 0) {
            checkVersion(a.id);
            continue;
        }
        const QString id = a.id;
        const int epoch = m_versionEpoch.value(id);
        QTimer::singleShot(delay, this, [this, id, epoch]() {
            // 错峰等待期间已有更新的检查出发（手动复查、装完复查）就
            // 让位，别作废在跑的那轮。
            if (m_versionEpoch.value(id) != epoch) {
                return;
            }
            checkVersion(id);
        });
    }
}

/**
 * @brief 运行单个 agent 的 versionCommand（显式入口）
 *
 * 与内部路径的差别只有一处：重置该 id 的重试预算——自动重试每轮探测
 * 只有一次，用户手动触发（装完复查、卡片菜单）时应该重新拿到它。
 *
 * @param id agent id；未配置 versionCommand 时静默返回
 */
void AgentScripts::checkVersion(const QString &id)
{
    m_versionRetries.insert(id, 0);
    launchVersionCheck(id);
}

/**
 * @brief 真正发起一轮版本探测
 *
 * 命令经 cmd /c 运行、m_versionTimeoutMs 安全超时、通道分开（有些工具把
 * 版本打到 stderr）。结束后 onScriptFinished 解析版本并决定 installed
 * 状态；瞬时失败（超时/没能启动）由 scheduleVersionRetry() 安排重试。
 *
 * @param id agent id；未配置 versionCommand 时清掉 spinner 静默返回
 */
void AgentScripts::launchVersionCheck(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return;
    }
    const QString cmd = m_model->definitions().at(row).versionCommand;
    if (cmd.isEmpty()) {
        // 门面在 start() 里预点亮了 spinner；错峰等待期间定义被改空时
        // 在这里灭掉，别让转圈挂满整个会话。
        m_model->setCheckingVersion(id, false);
        return;
    }

    ++m_versionEpoch[id];
    m_model->setCheckingVersion(id, true);

    const QString key = scriptKey(QStringLiteral("version"), id);
    m_startMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    cmdLog(QStringLiteral("version"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(cmd)));
    // 通道分开：有些工具把版本打到 stderr。安全超时杀掉挂死的检查；该
    // key 的陈旧运行由 ScriptRunner 作废，onScriptFinished 里延迟的
    // spinner 清除由 m_versionEpoch 守卫。
    m_runner->runShell(key, cmd, m_versionTimeoutMs, false);
}

/**
 * @brief 瞬时失败后安排一次自动重试
 *
 * 重试预算 kVersionRetryLimit 轮；等待期间出现更新的检查（epoch 已变）
 * 则作废本次重试，由新检查接管。预算用尽只记日志：结论停留在「不知道」，
 * 等下一次显式 checkVersion（卡片菜单或重启）。
 *
 * @param id agent id
 */
void AgentScripts::scheduleVersionRetry(const QString &id)
{
    if (m_versionRetries.value(id, 0) >= kVersionRetryLimit) {
        cmdLogError(QStringLiteral("version"), id,
                    QStringLiteral("no more retries; keeping the last verdict "
                                   "until the next re-check"));
        return;
    }
    ++m_versionRetries[id];
    const int epoch = m_versionEpoch.value(id);
    QTimer::singleShot(m_versionRetryDelayMs, this, [this, id, epoch]() {
        if (m_versionEpoch.value(id) != epoch) {
            return;
        }
        launchVersionCheck(id);
    });
}

/**
 * @brief 注入版本探测的时序参数（单元测试专用）
 *
 * 默认时序（20 s 超时 / 3 s 重试间隔 / 1.5 s 错峰）让「超时后重试」在
 * 测试里要等半分钟；测试注入毫秒级短值即可在百毫秒内走完超时→重试→
 * 放弃的全链路。产品代码不调用。
 *
 * @param timeoutMs    单次版本探测的超时（毫秒）
 * @param retryDelayMs 瞬时失败后的重试间隔（毫秒）
 * @param staggerMs    启动时各 agent 之间的错峰间隔（毫秒）
 */
void AgentScripts::setVersionProbeTimingForTesting(int timeoutMs,
                                                   int retryDelayMs,
                                                   int staggerMs)
{
    m_versionTimeoutMs = timeoutMs;
    m_versionRetryDelayMs = retryDelayMs;
    m_staggerMs = staggerMs;
}

// --- ScriptRunner 分派 -------------------------------------------------------

/**
 * @brief 处理一段实时到达的脚本输出
 *
 * 只处理 install/update/setup（version 的输出只在完成时读）；把增量
 * 追加进该 key 的缓冲并整体上卡片。
 *
 * @param key  运行 key（"<operation>:<id>"）
 * @param text 本次到达的文本
 */
void AgentScripts::onScriptChunk(const QString &key, const QString &text)
{
    QString operation, id;
    if (!splitScriptKey(key, operation, id)) {
        return;
    }
    if (operation != QStringLiteral("install")
        && operation != QStringLiteral("update")
        && operation != QStringLiteral("setup")) {
        return; // version 的输出只在完成时读
    }

    QString &buffer = m_buffers[key];
    buffer += text;
    m_model->setConsoleOutput(id, buffer);
}

/**
 * @brief 处理一次脚本运行的结束
 *
 * 按 key 里的操作分派：install/update 清 "installing"、写权威输出、
 * 发 installFinished 并顺手复查版本；setup 落盘 AgentStateStore、发
 * setupFinished（失败经 launchFailed 报带命令原文的消息）；version 解析
 * 版本串、写 installed/version/versionKnown 并发 versionResolved——命令
 * 跑完才算权威结论，超时/没能启动是「不知道」（保留上次状态、安排重试），
 * spinner 至少显示 500 ms 且只清本次检查的。
 *
 * @param key      运行 key（"<operation>:<id>"）
 * @param ok       命令干净退出为 true
 * @param exitCode 退出码；没能启动时为 -1
 * @param stdOut   stdout 累计文本
 * @param stdErr   stderr 累计文本
 * @param error    启动失败或超时的原因；干净运行为空串
 */
void AgentScripts::onScriptFinished(const QString &key, bool ok, int exitCode,
                                    const QString &stdOut, const QString &stdErr,
                                    const QString &error)
{
    QString operation, id;
    if (!splitScriptKey(key, operation, id)) {
        return;
    }

    const qint64 startMs = m_startMs.take(key);
    // 合并通道的运行全部记在 stdout；分开通道的运行把 stderr 接在
    // stdout 后面，对应 0.3.0 的 stdOutput + errOutput。
    const QString output = stdOut + stdErr;

    if (operation == QStringLiteral("install")
        || operation == QStringLiteral("update")) {
        m_model->setInstalling(id, false);
        m_buffers.remove(key);
        // 权威的完整文本（上面的 chunk 只流过增量）。
        m_model->setConsoleOutput(id, output);

        if (!error.isEmpty()) {
            // 命令从未启动（install/update 不设超时）。
            cmdLogError(operation, id, error);
            Q_EMIT installFinished(id, false,
                operation == QStringLiteral("install")
                    ? tr("Failed to start install command.")
                    : tr("Failed to start update command."));
        } else if (ok) {
            cmdLog(operation, id, exitSummary(exitCode, startMs));
            logCommandOutput(operation, id, output);
            Q_EMIT installFinished(id, true, QString());
        } else {
            cmdLogError(operation, id, exitSummary(exitCode, startMs));
            logCommandOutput(operation, id, output, true);
            QString detail = output.trimmed();
            if (detail.isEmpty()) {
                detail = tr("(no output)");
            }
            Q_EMIT installFinished(id, false,
                operation == QStringLiteral("install")
                    ? tr("Install failed (exit code %1):\n%2")
                          .arg(exitCode).arg(detail)
                    : tr("Update failed (exit code %1):\n%2")
                          .arg(exitCode).arg(detail));
        }
        // 复查一次版本，刷新卡片。
        checkVersion(id);
        return;
    }

    if (operation == QStringLiteral("setup")) {
        const QString command = m_setupCommands.take(key);
        m_model->setSetupping(id, false);
        m_buffers.remove(key);
        m_model->setConsoleOutput(id, output);

        if (!error.isEmpty()
            && error.startsWith(QStringLiteral("failed to start"))) {
            cmdLogError(operation, id, error);
            Q_EMIT launchFailed(id, tr("Failed to start setup command."));
            Q_EMIT setupFinished(id, false);
            return;
        }
        if (ok) {
            cmdLog(operation, id, exitSummary(exitCode, startMs));
            logCommandOutput(operation, id, output);
            if (m_stateStore->markSetupDone(id)) {
                m_model->setSetupDone(id, true);
            } else {
                qWarning().noquote() << QStringLiteral(
                    "[app] setup \"%1\": cannot write the setup state — the "
                    "setup command will run again on the next start").arg(id);
            }
            Q_EMIT setupFinished(id, true);
            return;
        }
        // 非零退出码，或超时（ScriptRunner 会写进 error 说明）。
        cmdLogError(operation, id,
                    error.isEmpty() ? exitSummary(exitCode, startMs) : error);
        logCommandOutput(operation, id, output, true);
        QString detail = output.trimmed();
        if (detail.isEmpty()) {
            detail = tr("(no output)");
        }
        Q_EMIT launchFailed(id,
            tr("Setup command failed (exit code %1).\n\nCommand: %2\n\n%3")
                .arg(exitCode)
                .arg(command)
                .arg(detail));
        Q_EMIT setupFinished(id, false);
        return;
    }

    if (operation == QStringLiteral("version")) {
        // 现在就取：下面延迟的 spinner 清除只在没有更新的检查开始时
        // 才允许触发。
        const int epoch = m_versionEpoch.value(id);

        if (!error.isEmpty()) {
            // 从未启动，或被安全超时杀掉：结论是「不知道」而不是「未安装」。
            // 一台进程创建被杀软/DLP 拖慢的机器上（冷启动实测 5~12 s），
            // 超时只说明探测没跑完；把 installed 翻成 false 会让整页卡片
            // 显示「未安装」且整个会话不再复查（0.4.x 实际发生过）。保留
            // 上次的结论，versionKnown=false 让界面隐藏版本徽标，重试/手动
            // 复查（checkVersion）再补齐。
            cmdLogError(operation, id, error);
            m_model->setVersionKnown(id, false);
            // 瞬时失败安排一次重试：多数超时来自启动瞬间的进程拥堵，
            // 错开高峰后第二轮通常就能跑完。
            scheduleVersionRetry(id);
        } else {
            // 先从 stdout 再从 stderr 里提取版本——有些工具把版本
            // 信息打到 stderr。
            QString version = core::TextUtils::extractVersion(stdOut);
            if (version.isEmpty()) {
                version = core::TextUtils::extractVersion(stdErr);
            }

            if (exitCode == 0 || !version.isEmpty()) {
                // 退出码 0，或非零退出但仍解析出了版本串——有些工具
                // 的 --version 就是非零退出。
                m_model->setInstalled(id, true);
                m_model->setVersion(id, version);
                m_model->setVersionKnown(id, true);
                Q_EMIT versionResolved(id, version);
                if (version.isEmpty()) {
                    cmdLog(operation, id,
                           QStringLiteral("%1, but no version string in the output")
                               .arg(exitSummary(exitCode, startMs)));
                    logCommandOutput(operation, id, output);
                } else {
                    cmdLog(operation, id,
                           QStringLiteral("%1 → %2")
                               .arg(exitSummary(exitCode, startMs), version));
                }
            } else {
                // 命令跑完、退出非零且输出里没有版本：权威的「未安装」。
                cmdLogError(operation, id, exitSummary(exitCode, startMs));
                logCommandOutput(operation, id, output, true);
                m_model->setInstalled(id, false);
                m_model->setVersion(id, QString());
                m_model->setVersionKnown(id, true);
            }
        }

        // spinner 至少亮 500 ms 免得闪烁；有守卫，陈旧的定时器清不掉
        // 更新一次的检查。
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startMs;
        QTimer::singleShot(static_cast<int>(qMax(0LL, 500 - elapsed)), this,
                [this, id, epoch]() {
                    if (m_versionEpoch.value(id) == epoch) {
                        m_model->setCheckingVersion(id, false);
                    }
                });
        return;
    }

    qWarning().noquote() << QStringLiteral(
        "AgentScripts: finished for unknown operation '%1' (key %2)")
        .arg(operation, key);
}

} // namespace awb::agentcatalog
