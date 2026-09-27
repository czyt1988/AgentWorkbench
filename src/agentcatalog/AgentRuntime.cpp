#include "agentcatalog/AgentRuntime.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentUrls.h"
#include "core/HttpProbe.h"
#include "core/Paths.h"
#include "core/ProcessRunner.h"
#include "core/TextUtils.h"

#include <QDateTime>
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

// Session-URL watch budget: 500 ms ticks for 60 s — the dsh log shows ~35 s
// between launch and the printed URL on a cold boot.
constexpr int kSessionUrlTickMs = 500;
constexpr int kSessionUrlTicks = 120;

// The operational log for spawned processes ("[cmd] …"), kept identical to
// 0.3.0 so existing log-parsing expectations hold.
QString logPrefix(const QString &tag, const QString &operation, const QString &id)
{
    return id.isEmpty()
               ? QStringLiteral("[%1] %2: ").arg(tag, operation)
               : QStringLiteral("[%1] %2 \"%3\": ").arg(tag, operation, id);
}

void cmdLog(const QString &operation, const QString &id, const QString &message)
{
    qInfo().noquote() << logPrefix(QStringLiteral("cmd"), operation, id) + message;
}

void cmdLogError(const QString &operation, const QString &id, const QString &message)
{
    qWarning().noquote() << logPrefix(QStringLiteral("cmd"), operation, id) + message;
}

void appLog(const QString &operation, const QString &id, const QString &message)
{
    qInfo().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

void appLogError(const QString &operation, const QString &id, const QString &message)
{
    qWarning().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

} // namespace

AgentRuntime::AgentRuntime(AgentModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    // Poll the redirected output for a session URL. 500 ms is gentle on the
    // disk (a handful of reads per agent launch) while still landing the
    // retarget well inside the first health check after launch.
    m_sessionUrlTimer.setInterval(kSessionUrlTickMs);
    m_sessionUrlTimer.setSingleShot(false);
    connect(&m_sessionUrlTimer, &QTimer::timeout, this, [this]() {
        const QList<QString> ids = m_sessionUrlWatch.keys();
        for (const QString &id : ids)
            watchSessionUrl(id);
    });
}

void AgentRuntime::launch(const AgentDefinition &definition,
                          const QString &tokenValue)
{
    const QString id = definition.id;

    // A fresh launch clears any install/setup log so the running card isn't
    // left showing stale console output.
    m_model->setConsoleOutput(id, QString());

    // Split command into program + arguments on whitespace.
    const QStringList parts = QProcess::splitCommand(definition.command);
    if (parts.isEmpty()) {
        cmdLogError(QStringLiteral("launch"), id,
                    QStringLiteral("skipped, the startup command is empty"));
        emit launchFailed(id, tr("Startup command is empty."));
        return;
    }

    const QString program = parts.first();
    const QStringList args = parts.mid(1);

    // Resolve the bare program through PATH (PATHEXT on Windows) so npm-style
    // .cmd/.bat shims (e.g. "qwen" -> "qwen.cmd") are found. CreateProcess on
    // its own does not try those extensions, which is why "qwen serve" failed
    // silently before.
    const QString resolved = core::ProcessRunner::findExecutable(program);
    if (resolved.isEmpty()) {
        const QString msg = tr("Cannot find '%1' on your PATH. "
                                        "Make sure it is installed and on PATH.")
                                .arg(program);
        cmdLogError(QStringLiteral("launch"), id,
                    QStringLiteral("cannot resolve '%1' on PATH "
                                   "(configured command: %2)")
                        .arg(program, definition.command));
        emit launchFailed(id, msg);
        return;
    }

    // Build the real command line: a .cmd/.bat shim cannot be executed
    // directly by CreateProcess, so it is wrapped in cmd.exe (and a /T kill
    // later covers the whole cmd -> qwen.cmd -> node tree).
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

    // The line the log exists for: what is really executed, after PATH
    // resolution and the cmd.exe wrapping of .cmd/.bat shims.
    cmdLog(QStringLiteral("launch"), id,
           QStringLiteral("running: %1  (cwd: %2)")
               .arg(core::TextUtils::formatCommandLine(execProgram, execArgs),
                    cwd));

    // If a token file is configured, hand the token to the child as
    // QWEN_SERVER_TOKEN so the daemon picks up the bearer token without a
    // complex --token argument on the command line.
    QProcessEnvironment env;
    if (!definition.tokenFile.isEmpty()) {
        if (!tokenValue.isEmpty()) {
            env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("QWEN_SERVER_TOKEN"), tokenValue);
            // The token value itself never reaches the log.
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
    // Token-gated harnesses print their authenticated URL to stdout. Capture
    // it into a per-launch log file (the health probe proves the port is up
    // but the bare webUrl is answered with 401 — only the printed URL works).
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
        emit launchFailed(id, tr("Failed to start '%1'.").arg(program));
        return;
    }

    m_pids.insert(id, pid);
    cmdLog(QStringLiteral("launch"), id,
           QStringLiteral("started, pid %1").arg(pid));

    // Same-server URLs in the output are the session URL (dsh); no match
    // after a grace period (qwen & co. print none) ends the watch quietly.
    if (!definition.webUrl.isEmpty()) {
        m_sessionUrlWatch.insert(id, {definition.webUrl, definition.tokenFile,
                                      kSessionUrlTicks});
        if (!m_sessionUrlTimer.isActive())
            m_sessionUrlTimer.start();
    }

    // Mark the card as "launching" so the action button shows a spinner until
    // the health check confirms the server is up — or a 30s safety timeout
    // fires in case the agent crashes on boot. The epoch guards against a
    // stale timeout from an earlier attempt clearing a newer launch.
    const int epoch = ++m_launchEpoch[id];
    m_model->setLaunching(id, true);
    QTimer::singleShot(30000, this, [this, id, epoch]() {
        if (m_launchEpoch.value(id) == epoch)
            m_model->setLaunching(id, false);
    });

    // Re-check shortly so the card flips to running fast.
    QTimer::singleShot(1500, this, &AgentRuntime::recheckRequested);
}

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
        emit launchFailed(id, msg);
        return false;
    }

    const qint64 pid = *it;
    m_pids.erase(it);
    // The per-process token died with the process — the bare webUrl is all
    // that is left to open until the next launch captures a fresh URL.
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
        emit launchFailed(id, tr("Failed to stop process (PID %1).")
                                  .arg(pid));
    } else {
        cmdLog(QStringLiteral("stop"), id,
               QStringLiteral("killed process tree, pid %1").arg(pid));
    }

    // Re-check so the card flips back to Stopped once the port is down.
    QTimer::singleShot(500, this, &AgentRuntime::recheckRequested);
    return ok;
}

void AgentRuntime::forceStop(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const AgentDefinition def = m_model->definitions().at(row);

    // No tracked PID for agents not started here, so target by port instead.
    const int port = core::HttpProbe::portFromUrl(def.webUrl);
    if (port < 0) {
        const QString msg = tr("Cannot determine port from web URL.");
        cmdLogError(QStringLiteral("forceStop"), id,
                    QStringLiteral("cannot determine a port from web URL '%1'")
                        .arg(def.webUrl));
        emit launchFailed(id, msg);
        return;
    }

    const QList<qint64> pids = findPidsForPort(port);
    if (pids.isEmpty()) {
        const QString msg =
            tr("No process found listening on port %1; "
                        "the agent may already be stopped.").arg(port);
        cmdLogError(QStringLiteral("forceStop"), id,
                    QStringLiteral("no process listening on port %1").arg(port));
        emit launchFailed(id, msg);
        return;
    }

    QStringList pidList;
    for (const qint64 pid : pids)
        pidList << QString::number(pid);
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
        if (core::ProcessRunner::startDetached(killProgram, args))
            anyOk = true;
    }

    // If this launcher also tracked a PID for the agent, drop it so a later
    // normal stop() doesn't try to kill an already-dead PID.
    m_pids.remove(id);
    dropSessionUrl(id);

    if (!anyOk) {
        const QString msg =
            tr("Failed to stop process (PID %1).").arg(pids.constFirst());
        cmdLogError(QStringLiteral("forceStop"), id,
                    QStringLiteral("failed to kill pid(s) %1").arg(pidsText));
        emit launchFailed(id, msg);
    } else {
        cmdLog(QStringLiteral("forceStop"), id,
               QStringLiteral("killed the process tree(s) holding port %1")
                   .arg(port));
    }

    // Re-check so the card flips back to Stopped once the port is down.
    QTimer::singleShot(500, this, &AgentRuntime::recheckRequested);
}

bool AgentRuntime::hasLaunchedAgents() const
{
    return !m_pids.isEmpty();
}

int AgentRuntime::stopAll()
{
    const int tracked = m_pids.size();
    int killed = 0;
    const QString killProgram = core::ProcessRunner::killProgram();
    for (auto it = m_pids.constBegin(); it != m_pids.constEnd(); ++it) {
        const qint64 pid = *it;
        if (pid == 0)
            continue;
        const QStringList args = core::ProcessRunner::killProgramArgs(pid);
        cmdLog(QStringLiteral("stopAll"), it.key(),
               QStringLiteral("running: %1")
                   .arg(core::TextUtils::formatCommandLine(killProgram, args)));
        if (core::ProcessRunner::startDetached(killProgram, args))
            ++killed;
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

void AgentRuntime::forget(const QString &id)
{
    m_pids.remove(id);
    m_launchEpoch.remove(id);
    dropSessionUrl(id);
}

QString AgentRuntime::sessionUrl(const QString &id) const
{
    return m_sessionUrls.value(id);
}

void AgentRuntime::watchSessionUrl(const QString &id)
{
    const auto it = m_sessionUrlWatch.find(id);
    if (it == m_sessionUrlWatch.end())
        return;
    SessionWatch watch = it.value();

    QString text;
    {
        QFile file(core::Paths::logsDir() + QStringLiteral("/output/")
                    + id + QStringLiteral(".log"));
        if (file.open(QIODevice::ReadOnly))
            text = core::ProcessRunner::decodeOutput(file.readAll());
    }

    const QString captured = AgentUrls::sessionUrlFromOutput(text, watch.webUrl);
    if (!captured.isEmpty()) {
        const QString url = AgentUrls::finalUrl(captured, watch.tokenFile);
        m_sessionUrls.insert(id, url);
        m_sessionUrlWatch.erase(it);
        if (m_sessionUrlWatch.isEmpty())
            m_sessionUrlTimer.stop();
        // The URL itself never reaches the log — it carries the token.
        cmdLog(QStringLiteral("session-url"), id,
               QStringLiteral("captured an authenticated URL from the "
                              "agent output"));
        emit sessionUrlChanged(id, url);
        return;
    }

    if (--watch.attemptsLeft <= 0) {
        m_sessionUrlWatch.erase(it);
        if (m_sessionUrlWatch.isEmpty())
            m_sessionUrlTimer.stop();
    } else {
        it.value() = watch;
    }
}

void AgentRuntime::dropSessionUrl(const QString &id)
{
    m_sessionUrls.remove(id);
    m_sessionUrlWatch.remove(id);
    if (m_sessionUrlWatch.isEmpty())
        m_sessionUrlTimer.stop();
}

QList<qint64> AgentRuntime::findPidsForPort(int port)
{
    QList<qint64> pids;
    QSet<qint64> seen;

    QProcess proc;
#ifdef Q_OS_WIN
    // netstat -ano prints one row per connection: proto local foreign state PID.
    // Keep only LISTENING rows whose local address ends with ":<port>" — this
    // avoids matching the foreign-address column and avoids ":3000" hitting a
    // longer port like ":53000" (the leading colon is a delimiter).
    proc.setProgram(QStringLiteral("cmd"));
    proc.setArguments({QStringLiteral("/c"),
                       QStringLiteral("netstat -ano -p tcp")});
#else
    // lsof -ti :<port> prints just the owning PIDs, one per line.
    proc.setProgram(QStringLiteral("lsof"));
    proc.setArguments({QStringLiteral("-ti"),
                       QStringLiteral(":%1").arg(port)});
#endif
    proc.start();
    if (!proc.waitForFinished(5000))
        return pids;

    const QString output =
        core::ProcessRunner::decodeOutput(proc.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'));
    const QString portSuffix = QStringLiteral(":%1").arg(port);

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
#ifdef Q_OS_WIN
        const QStringList cols =
            trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (cols.size() < 5)
            continue;
        bool isListening = false;
        for (const QString &c : cols) {
            if (c == QLatin1String("LISTENING")) {
                isListening = true;
                break;
            }
        }
        if (!isListening)
            continue;
        // cols[0]=proto, cols[1]=local address, cols[2]=foreign, then state, PID.
        if (!cols.at(1).endsWith(portSuffix, Qt::CaseInsensitive))
            continue;
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
