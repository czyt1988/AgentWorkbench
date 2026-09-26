#include "AgentLauncher.h"
#include "AgentConfig.h"
#include "core/EnvExpander.h"
#include "core/Logging.h"
#include "core/HttpProbe.h"
#include "core/JsonStore.h"
#include "core/ProcessRunner.h"
#include "core/ScriptRunner.h"
#include "core/TextUtils.h"

#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSharedPointer>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {

// Copy the runtime-only state (health check, version probe, setup progress)
// from one agent entry to another; only the persisted fields differ between
// the two, so a config change must not blank the card.
void carryRuntimeState(const Agent &from, Agent &to)
{
    to.running = from.running;
    to.launching = from.launching;
    to.installed = from.installed;
    to.version = from.version;
    to.installing = from.installing;
    to.setupDone = from.setupDone;
    to.setupping = from.setupping;
    to.checkingVersion = from.checkingVersion;
    to.consoleOutput = from.consoleOutput;
}

// --- Operational log -------------------------------------------------------
// Everything the launcher does on the user's behalf is reported through these
// helpers, one line per event, so the log answers "what did the launcher run,
// and what came back":
//   [cmd] install "opencode": running: cmd /c npm install -g opencode-ai
//   [cmd] install "opencode": done, exit=0, 31.2s
//   [app] openWeb "kimi-code": failed to open http://127.0.0.1:4096/
// The tag separates spawned processes ("cmd") from everything else ("app");
// `id` is the agent id, or empty for operations that are not about one agent.

QString logPrefix(const QString &tag, const QString &operation, const QString &id)
{
    return id.isEmpty()
               ? QStringLiteral("[%1] %2: ").arg(tag, operation)
               : QStringLiteral("[%1] %2 \"%3\": ").arg(tag, operation, id);
}

// A spawned process: its command line, exit code, and output.
void cmdLog(const QString &operation, const QString &id, const QString &message)
{
    qInfo().noquote() << logPrefix(QStringLiteral("cmd"), operation, id) + message;
}

void cmdLogError(const QString &operation, const QString &id, const QString &message)
{
    qWarning().noquote() << logPrefix(QStringLiteral("cmd"), operation, id) + message;
}

// Everything else: opening a URL, writing config/state, health transitions.
void appLog(const QString &operation, const QString &id, const QString &message)
{
    qInfo().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

void appLogError(const QString &operation, const QString &id, const QString &message)
{
    qWarning().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

// How long a command took, e.g. "1.2s".
QString elapsedSince(qint64 startMs)
{
    return QStringLiteral("%1s")
        .arg((QDateTime::currentMSecsSinceEpoch() - startMs) / 1000.0, 0, 'f', 1);
}

// Outcome of a finished process, e.g. "done, exit=0, 31.2s".
QString exitSummary(int exitCode, qint64 startMs)
{
    return QStringLiteral("%1, exit=%2, %3")
        .arg(exitCode == 0 ? QStringLiteral("done") : QStringLiteral("FAILED"))
        .arg(exitCode)
        .arg(elapsedSince(startMs));
}

// How the launcher runs a raw install/update/setup/version command string.
QString shellCommandLine(const QString &command)
{
    return QStringLiteral("cmd /c ") + command;
}

// Verbatim output of a finished command, capped so one chatty command cannot
// fill the log. Newlines are kept so errors read exactly as the tool printed
// them. `failure` mirrors the severity of the matching outcome line.
void logCommandOutput(const QString &operation, const QString &id,
                      const QString &output, bool failure = false)
{
    const QString text = awb::core::TextUtils::clampOutput(
        output, awb::core::Logging::DEFAULT_MAX_OUTPUT).trimmed();
    const QString line = logPrefix(QStringLiteral("cmd"), operation, id)
                         + (text.isEmpty()
                                ? QStringLiteral("output: (none)")
                                : QStringLiteral("output:\n") + text);
    if (failure)
        qWarning().noquote() << line;
    else
        qInfo().noquote() << line;
}

// Script run keys: one ScriptRunner slot per "<operation>:<agent id>", so
// concurrent operations on the same agent (a version check during an
// install) never kill each other.
QString scriptKey(const QString &operation, const QString &id)
{
    return operation + QLatin1Char(':') + id;
}

bool splitScriptKey(const QString &key, QString &operation, QString &id)
{
    const int sep = key.indexOf(QLatin1Char(':'));
    if (sep <= 0)
        return false;
    operation = key.left(sep);
    id = key.mid(sep + 1);
    return true;
}

} // namespace

AgentLauncher::AgentLauncher(AgentModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_probe(new awb::core::HttpProbe(this))
    , m_scripts(new awb::core::ScriptRunner(this))
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &AgentLauncher::checkAll);
    connect(m_probe, &awb::core::HttpProbe::finished, this,
            [this](const QString &url, bool up) {
                // One probe result applies to every agent pointing at the
                // URL (0.3.0 issued one request per agent; same outcome).
                const QList<Agent> &agents = m_model->agents();
                for (const Agent &a : agents) {
                    if (a.webUrl != url)
                        continue;
                    // Once the server is up, the launch is done: clear the
                    // spinner.
                    if (up)
                        m_model->setLaunching(a.id, false);
                    // Report health transitions (not every poll) so the log
                    // tells the story of when an agent came up or went down.
                    const int row = m_model->indexOf(a.id);
                    const bool wasRunning =
                        row >= 0 && m_model->agents().at(row).running;
                    if (up != wasRunning) {
                        appLog(QStringLiteral("state"), a.id,
                               up ? QStringLiteral("stopped → running")
                                  : QStringLiteral("running → stopped"));
                    }
                    m_model->setRunning(a.id, up);
                }
            });
    connect(m_scripts, &awb::core::ScriptRunner::outputChunk, this,
            &AgentLauncher::onScriptChunk);
    connect(m_scripts, &awb::core::ScriptRunner::finished, this,
            &AgentLauncher::onScriptFinished);
}

void AgentLauncher::start()
{
    appLog(QStringLiteral("start"), QString(),
           QStringLiteral("%1 launcher(s) configured, config: %2")
               .arg(m_model->agents().size())
               .arg(configFilePath()));
    loadSetupState();
    // Mark every agent with a versionCommand as "checking" before any QML
    // paint so the spinner is visible from the first frame, even if the
    // version process finishes before the first render.
    for (const Agent &a : m_model->agents()) {
        if (!a.versionCommand.isEmpty())
            m_model->setCheckingVersion(a.id, true);
    }
    checkAll();
    checkVersions();
    detectRuntimeVersions();
    m_timer->start(3000);
}

void AgentLauncher::launch(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;

    const Agent &a = m_model->agents().at(row);

    // If the agent has a one-time setup command that hasn't been run yet,
    // run it first; doLaunch() is called from the setup's finished handler
    // on success.
    if (!a.setupCommand.isEmpty() && !a.setupDone) {
        runSetup(id);
        return;
    }

    doLaunch(id);
}

void AgentLauncher::doLaunch(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const Agent &a = m_model->agents().at(row);
    const QString command = a.command;

    // A fresh launch clears any install/setup log so the running card isn't
    // left showing stale console output.
    m_model->setConsoleOutput(id, QString());

    // Split command into program + arguments on whitespace.
    const QStringList parts = QProcess::splitCommand(command);
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
    const QString resolved = awb::core::ProcessRunner::findExecutable(program);
    if (resolved.isEmpty()) {
        const QString msg = tr("Cannot find '%1' on your PATH. "
                               "Make sure it is installed and on PATH.")
                                .arg(program);
        cmdLogError(QStringLiteral("launch"), id,
                    QStringLiteral("cannot resolve '%1' on PATH "
                                   "(configured command: %2)")
                        .arg(program, command));
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
    const QString cwd = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    // The line the log exists for: what is really executed, after PATH
    // resolution and the cmd.exe wrapping of .cmd/.bat shims.
    cmdLog(QStringLiteral("launch"), id,
           QStringLiteral("running: %1  (cwd: %2)")
               .arg(awb::core::TextUtils::formatCommandLine(execProgram, execArgs),
                    cwd));

    // If a token file is configured, read it and set QWEN_SERVER_TOKEN in the
    // process environment. This lets the daemon pick up the bearer token
    // without a complex --token argument on the command line.
    QProcessEnvironment env;
    if (!a.tokenFile.isEmpty()) {
        const QString tokenPath = awb::core::EnvExpander::expand(a.tokenFile);
        QFile f(tokenPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString token = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
            if (!token.isEmpty()) {
                env = QProcessEnvironment::systemEnvironment();
                env.insert(QStringLiteral("QWEN_SERVER_TOKEN"), token);
                // The token value itself never reaches the log.
                cmdLog(QStringLiteral("launch"), id,
                       QStringLiteral("injecting QWEN_SERVER_TOKEN from %1")
                           .arg(tokenPath));
            } else {
                cmdLogError(QStringLiteral("launch"), id,
                            QStringLiteral("token file %1 is empty").arg(tokenPath));
            }
        } else {
            cmdLogError(QStringLiteral("launch"), id,
                        QStringLiteral("cannot read token file %1: %2")
                            .arg(tokenPath, f.errorString()));
        }
    }

    qint64 pid = 0;
    QString startError;
    const bool ok = awb::core::ProcessRunner::startDetached(
        execProgram, execArgs, &pid, &startError, cwd, env);
    if (!ok) {
        cmdLogError(QStringLiteral("launch"), id,
                    QStringLiteral("failed to start: %1").arg(startError));
        emit launchFailed(id, tr("Failed to start '%1'.").arg(program));
        return;
    }

    m_pids.insert(id, pid);
    cmdLog(QStringLiteral("launch"), id, QStringLiteral("started, pid %1").arg(pid));

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
    QTimer::singleShot(1500, this, &AgentLauncher::checkAll);
}

bool AgentLauncher::stop(const QString &id)
{
    const auto it = m_pids.constFind(id);
    if (it == m_pids.constEnd() || *it == 0) {
        const QString msg = tr("This agent wasn't started from the launcher; "
                               "stop it with its own command.");
        cmdLogError(QStringLiteral("stop"), id,
                    QStringLiteral("no PID tracked in this launcher session, "
                                   "nothing to kill"));
        emit launchFailed(id, msg);
        return false;
    }

    const qint64 pid = *it;
    m_pids.erase(it);

    const QString killProgram = awb::core::ProcessRunner::killProgram();
    const QStringList args = awb::core::ProcessRunner::killProgramArgs(pid);
    cmdLog(QStringLiteral("stop"), id,
           QStringLiteral("running: %1")
               .arg(awb::core::TextUtils::formatCommandLine(killProgram, args)));
    const bool ok = awb::core::ProcessRunner::startDetached(killProgram, args);
    if (!ok) {
        cmdLogError(QStringLiteral("stop"), id,
                    QStringLiteral("failed to kill pid %1").arg(pid));
        emit launchFailed(id, tr("Failed to stop process (PID %1).").arg(pid));
    } else {
        cmdLog(QStringLiteral("stop"), id,
               QStringLiteral("killed process tree, pid %1").arg(pid));
    }

    // Re-check so the card flips back to Stopped once the port is down.
    QTimer::singleShot(500, this, &AgentLauncher::checkAll);
    return ok;
}

void AgentLauncher::forceStop(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const Agent &a = m_model->agents().at(row);

    // No tracked PID for agents not started here, so target by port instead.
    const int port = awb::core::HttpProbe::portFromUrl(a.webUrl);
    if (port < 0) {
        const QString msg = tr("Cannot determine port from web URL.");
        cmdLogError(QStringLiteral("forceStop"), id,
                    QStringLiteral("cannot determine a port from web URL '%1'")
                        .arg(a.webUrl));
        emit launchFailed(id, msg);
        return;
    }

    const QList<qint64> pids = findPidsForPort(port);
    if (pids.isEmpty()) {
        const QString msg = tr("No process found listening on port %1; "
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
    const QString killProgram = awb::core::ProcessRunner::killProgram();
    for (const qint64 pid : pids) {
        const QStringList args = awb::core::ProcessRunner::killProgramArgs(pid);
        cmdLog(QStringLiteral("forceStop"), id,
               QStringLiteral("running: %1")
                   .arg(awb::core::TextUtils::formatCommandLine(killProgram, args)));
        if (awb::core::ProcessRunner::startDetached(killProgram, args))
            anyOk = true;
    }

    // If this launcher also tracked a PID for the agent, drop it so a later
    // normal stop() doesn't try to kill an already-dead PID.
    m_pids.remove(id);

    if (!anyOk) {
        const QString msg = tr("Failed to stop process (PID %1).")
                                .arg(pids.constFirst());
        cmdLogError(QStringLiteral("forceStop"), id,
                    QStringLiteral("failed to kill pid(s) %1").arg(pidsText));
        emit launchFailed(id, msg);
    } else {
        cmdLog(QStringLiteral("forceStop"), id,
               QStringLiteral("killed the process tree(s) holding port %1")
                   .arg(port));
    }

    // Re-check so the card flips back to Stopped once the port is down.
    QTimer::singleShot(500, this, &AgentLauncher::checkAll);
}

QList<qint64> AgentLauncher::findPidsForPort(int port) const
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
    proc.setArguments({QStringLiteral("/c"), QStringLiteral("netstat -ano -p tcp")});
#else
    // lsof -ti :<port> prints just the owning PIDs, one per line.
    proc.setProgram(QStringLiteral("lsof"));
    proc.setArguments({QStringLiteral("-ti"), QStringLiteral(":%1").arg(port)});
#endif
    proc.start();
    if (!proc.waitForFinished(5000))
        return pids;

    const QString output =
        awb::core::ProcessRunner::decodeOutput(proc.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'));
    const QString portSuffix = QStringLiteral(":%1").arg(port);

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
#ifdef Q_OS_WIN
        const QStringList cols = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
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

bool AgentLauncher::hasLaunchedAgents() const
{
    return !m_pids.isEmpty();
}

int AgentLauncher::stopAll()
{
    const int tracked = m_pids.size();
    int killed = 0;
    const QString killProgram = awb::core::ProcessRunner::killProgram();
    for (auto it = m_pids.constBegin(); it != m_pids.constEnd(); ++it) {
        const qint64 pid = *it;
        if (pid == 0)
            continue;
        const QStringList args = awb::core::ProcessRunner::killProgramArgs(pid);
        cmdLog(QStringLiteral("stopAll"), it.key(),
               QStringLiteral("running: %1")
                   .arg(awb::core::TextUtils::formatCommandLine(killProgram, args)));
        if (awb::core::ProcessRunner::startDetached(killProgram, args))
            ++killed;
    }
    m_pids.clear();
    appLog(QStringLiteral("stopAll"), QString(),
           QStringLiteral("terminated %1 of %2 launcher(s) started this session")
               .arg(killed)
               .arg(tracked));
    return killed;
}

void AgentLauncher::openWeb(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const Agent &a = m_model->agents().at(row);
    QString url = a.webUrl;
    if (url.isEmpty())
        return;

    // If a token file is configured, read it and append the token as a URL
    // fragment (#token=<value>) so the web UI can authenticate to mutation
    // routes (e.g. POST /workspaces). The fragment is never sent to the
    // server, keeping the token out of access logs and Referer headers.
    if (!a.tokenFile.isEmpty()) {
        const QString tokenPath = awb::core::EnvExpander::expand(a.tokenFile);
        QFile f(tokenPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString token = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
            if (!token.isEmpty())
                url += QStringLiteral("#token=") + token;
        }
    }

    // The #token=… fragment must never reach the log, so strip it first.
    QString loggedUrl = url;
    const int fragment = loggedUrl.indexOf(QLatin1Char('#'));
    if (fragment >= 0)
        loggedUrl.truncate(fragment);

    if (QDesktopServices::openUrl(QUrl(url))) {
        appLog(QStringLiteral("openWeb"), id,
               QStringLiteral("opened %1").arg(loggedUrl));
    } else {
        appLogError(QStringLiteral("openWeb"), id,
                    QStringLiteral("failed to open %1 — no handler accepted "
                                   "the URL")
                        .arg(loggedUrl));
    }
}

void AgentLauncher::openConfigDir(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    QString dir = awb::core::EnvExpander::expand(
        m_model->agents().at(row).configDir);
    if (dir.isEmpty()) {
        appLogError(QStringLiteral("openConfigDir"), id,
                    QStringLiteral("no config directory configured"));
        return;
    }
    dir = QDir::fromNativeSeparators(dir);
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        appLog(QStringLiteral("openConfigDir"), id,
               QStringLiteral("opened %1").arg(dir));
    } else {
        appLogError(QStringLiteral("openConfigDir"), id,
                    QStringLiteral("failed to open %1").arg(dir));
    }
}

namespace {

// Build an Agent from the field map the QML edit form submits. The id comes
// from the caller (generated or immutable), the icon is resolved so the
// model always holds a displayable URL.
Agent agentFromFields(const QVariantMap &f, const QString &id)
{
    Agent a;
    a.id = id;
    a.name = f.value(QStringLiteral("name")).toString().trimmed();
    a.command = f.value(QStringLiteral("command")).toString().trimmed();
    a.webUrl = f.value(QStringLiteral("webUrl")).toString().trimmed();
    a.configDir = f.value(QStringLiteral("configDir")).toString().trimmed();
    a.icon = AgentConfig::resolveIcon(f.value(QStringLiteral("icon")).toString().trimmed());
    a.color = f.value(QStringLiteral("color")).toString().trimmed();
    a.cardColor = f.value(QStringLiteral("cardColor")).toString().trimmed();
    a.installCommand = f.value(QStringLiteral("installCommand")).toString().trimmed();
    a.updateCommand = f.value(QStringLiteral("updateCommand")).toString().trimmed();
    a.versionCommand = f.value(QStringLiteral("versionCommand")).toString().trimmed();
    a.setupCommand = f.value(QStringLiteral("setupCommand")).toString().trimmed();
    a.tokenFile = f.value(QStringLiteral("tokenFile")).toString().trimmed();
    return a;
}

} // namespace

bool AgentLauncher::addAgent(const QVariantMap &fields)
{
    QString id = fields.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        // Generate from the display name, uniquified with -2, -3, ... suffixes.
        const QString base = AgentConfig::slugFromName(
            fields.value(QStringLiteral("name")).toString());
        id = base;
        int n = 2;
        while (m_model->indexOf(id) >= 0)
            id = base + QLatin1Char('-') + QString::number(n++);
    } else if (m_model->indexOf(id) >= 0) {
        return false; // duplicate id (the form prevents this; defensive)
    }

    Agent a = agentFromFields(fields, id);
    // Empty color would render a broken card until the next restart
    // (load() assigns palette colors); assign one now.
    if (a.color.isEmpty())
        a.color = AgentConfig::paletteColorAt(m_model->agents().size());

    m_model->insertAgent(m_model->agents().size(), a);
    return saveConfig();
}

bool AgentLauncher::updateAgentFull(const QString &id, const QVariantMap &fields)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return false;

    Agent a = agentFromFields(fields, id); // id is immutable
    if (a.color.isEmpty())
        a.color = AgentConfig::paletteColorAt(row);

    // Preserve runtime state flags; only the persisted fields change.
    carryRuntimeState(m_model->agents().at(row), a);

    m_model->agents()[row] = a;
    const QModelIndex idx = m_model->index(row, 0);
    emit m_model->dataChanged(idx, idx); // no roles = all roles

    return saveConfig();
}

bool AgentLauncher::removeAgent(const QString &id)
{
    if (!m_model->removeAgentById(id))
        return false;

    // Record deleted built-ins: built-in agents are re-applied from the
    // shipped default on every start, so the id has to be remembered here to
    // keep this one deleted.
    if (AgentConfig::defaultAgentIds().contains(id) && !m_removedIds.contains(id))
        m_removedIds.append(id);

    // The process itself keeps running on purpose (documented in the UI).
    m_pids.remove(id);
    m_launchEpoch.remove(id);
    m_versionEpoch.remove(id);
    return saveConfig();
}

bool AgentLauncher::restoreDefaults()
{
    // Forget the deletions and re-apply the shipped built-in list; agents the
    // user added themselves are kept.
    m_removedIds.clear();
    const QList<Agent> previous = m_model->agents();
    QList<Agent> restored =
        AgentConfig::withBuiltinDefaults(previous, QStringList());
    for (Agent &a : restored) {
        const auto it = std::find_if(previous.cbegin(), previous.cend(),
            [&](const Agent &old) { return old.id == a.id; });
        if (it != previous.cend())
            carryRuntimeState(*it, a);
    }
    m_model->setAgents(restored);
    return saveConfig();
}

bool AgentLauncher::isDefaultAgent(const QString &id) const
{
    return AgentConfig::defaultAgentIds().contains(id);
}

QString AgentLauncher::configFilePath() const
{
    return AgentConfig::configFilePath();
}

bool AgentLauncher::saveConfig()
{
    AgentConfig cfg;
    cfg.setAgents(m_model->agents());
    cfg.setRemovedIds(m_removedIds);
    const bool ok = cfg.save();
    if (ok) {
        appLog(QStringLiteral("config"), QString(),
               QStringLiteral("saved %1").arg(configFilePath()));
    } else {
        appLogError(QStringLiteral("config"), QString(),
                    QStringLiteral("failed to write %1").arg(configFilePath()));
    }
    return ok;
}

void AgentLauncher::checkAll()
{
    // core::HttpProbe owns the semantics: any HTTP response (even an error
    // status) means the server is up; refused/timeout means it is down.
    // The result is applied in the probe-connected handler in the ctor.
    const QList<Agent> &agents = m_model->agents();
    for (const Agent &a : agents) {
        if (a.webUrl.isEmpty())
            continue;
        m_probe->probe(a.webUrl);
    }
}

// --- Version detection ---------------------------------------------------

void AgentLauncher::checkVersions()
{
    for (const Agent &a : m_model->agents())
        checkVersion(a.id);
}

void AgentLauncher::checkVersion(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const QString cmd = m_model->agents().at(row).versionCommand;
    if (cmd.isEmpty())
        return;

    m_versionEpoch[id] = m_versionEpoch.value(id) + 1;
    m_model->setCheckingVersion(id, true);

    const QString key = scriptKey(QStringLiteral("version"), id);
    m_scriptStartMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    cmdLog(QStringLiteral("version"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(cmd)));
    // Separate channels: some tools print the version to stderr. 10s safety
    // timeout kills a hung check; a stale run for this key is invalidated
    // by ScriptRunner, and the delayed spinner clear below is guarded by
    // m_versionEpoch.
    m_scripts->runShell(key, cmd, 10000, false);
}


// --- Setup (first-run prerequisite) ----------------------------------------

void AgentLauncher::runSetup(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const QString cmd = m_model->agents().at(row).setupCommand;
    if (cmd.isEmpty())
        return;

    // Clear any previous output before flipping the card to "setting up" so
    // the panel never flashes stale text from a prior run when it (re)opens.
    m_model->setConsoleOutput(id, QString());
    m_model->setSetupping(id, true);

    cmdLog(QStringLiteral("setup"), id,
           QStringLiteral("configured command: %1").arg(cmd));
    cmdLog(QStringLiteral("setup"), id,
           QStringLiteral("running: cmd /c <temporary .cmd batch> "
                          "(command quoted in the line above)"));

    const QString key = scriptKey(QStringLiteral("setup"), id);
    m_scriptBuffers.remove(key);
    m_scriptStartMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    m_scriptCommands.insert(key, cmd);
    // The batch file sidesteps cmd.exe quoting (see ScriptRunner::runBatch);
    // 30s safety timeout kills a hung setup.
    m_scripts->runBatch(key, cmd, 30000);
}

// --- Setup state persistence -----------------------------------------------

QString AgentLauncher::stateFilePath() const
{
    // Co-located with agents.json in the user data directory.
    return AgentConfig::userDataDir() + QStringLiteral("/agent_state.json");
}

void AgentLauncher::loadSetupState()
{
    const QJsonObject root = awb::core::JsonStore::readFile(stateFilePath());

    for (Agent &a : m_model->agents()) {
        const QJsonObject agentState = root.value(a.id).toObject();
        if (agentState.value(QStringLiteral("setupDone")).toBool())
            a.setupDone = true;
    }
}

void AgentLauncher::markSetupDone(const QString &id)
{
    m_model->setSetupDone(id, true);

    // Persist to agent_state.json (atomic, via core::JsonStore).
    QJsonObject root = awb::core::JsonStore::readFile(stateFilePath());
    QJsonObject agentState = root.value(id).toObject();
    agentState[QStringLiteral("setupDone")] = true;
    root[id] = agentState;

    if (awb::core::JsonStore::writeFile(stateFilePath(), root).ok) {
        appLog(QStringLiteral("setup"), id,
               QStringLiteral("marked as done in %1").arg(stateFilePath()));
    } else {
        appLogError(QStringLiteral("setup"), id,
                    QStringLiteral("cannot write %1 — the setup command will run "
                                   "again on the next start")
                        .arg(stateFilePath()));
    }
}

void AgentLauncher::resetSetup(const QString &id)
{
    m_model->setSetupDone(id, false);

    // Remove from agent_state.json; a missing file means nothing to clear.
    if (!QFile::exists(stateFilePath()))
        return;

    QJsonObject root = awb::core::JsonStore::readFile(stateFilePath());
    root.remove(id);

    if (awb::core::JsonStore::writeFile(stateFilePath(), root).ok) {
        appLog(QStringLiteral("setup"), id,
               QStringLiteral("re-initialized, the setup command runs again "
                              "before the next start"));
    } else {
        appLogError(QStringLiteral("setup"), id,
                    QStringLiteral("cannot write %1 while clearing the setup "
                                   "state")
                        .arg(stateFilePath()));
    }
}

// --- Runtime version detection (Python / Node.js) -------------------------

void AgentLauncher::detectRuntimeVersions()
{
    detectRuntime(QStringLiteral("python"), QStringLiteral("--version"),
                 QStringLiteral("Python"));
    detectRuntime(QStringLiteral("node"), QStringLiteral("--version"),
                 QStringLiteral("Node"));
}

void AgentLauncher::detectRuntime(const QString &program,
                                  const QString &versionArg,
                                  const QString &runtimeName)
{
    // Quick PATH check first — if the executable isn't found, there's no
    // point spawning a process. findExecutable applies PATHEXT on Windows.
    const QString resolved = awb::core::ProcessRunner::findExecutable(program);
    if (resolved.isEmpty()) {
        cmdLogError(QStringLiteral("runtime"), runtimeName,
                    QStringLiteral("'%1' is not on PATH").arg(program));
        if (runtimeName == QLatin1String("Python")) {
            m_pythonInstalled = false;
            m_pythonVersion.clear();
        } else {
            m_nodeInstalled = false;
            m_nodeVersion.clear();
        }
        emit runtimeVersionsChanged();
        return;
    }

    const QString key = scriptKey(QStringLiteral("runtime"), runtimeName);
    m_scriptStartMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    // The probe hands cmd one string ("python --version"), so it is reported
    // the same way as the other cmd /c lines rather than as a quoted argv.
    cmdLog(QStringLiteral("runtime"), runtimeName,
           QStringLiteral("running: %1")
               .arg(shellCommandLine(program + QLatin1Char(' ') + versionArg)));
    // Separate channels (older Python prints the version to stderr);
    // 10s safety timeout.
    m_scripts->runShell(key, program + QLatin1Char(' ') + versionArg,
                        10000, false);
}

// --- Install / Update -----------------------------------------------------

void AgentLauncher::install(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const Agent &a = m_model->agents().at(row);

    if (a.running) {
        cmdLogError(QStringLiteral("install"), id,
                    QStringLiteral("skipped, the agent is running"));
        emit launchFailed(id, tr("Please close %1 before installing/updating.").arg(a.name));
        return;
    }
    if (a.installCommand.isEmpty()) {
        cmdLogError(QStringLiteral("install"), id,
                    QStringLiteral("skipped, no install command is configured"));
        emit installFinished(id, false,
            tr("No install command configured for %1.").arg(a.name));
        return;
    }

    // Clear any previous output before flipping the card to "installing" so
    // the panel never flashes stale text from a prior run when it (re)opens.
    m_model->setConsoleOutput(id, QString());
    m_model->setInstalling(id, true);

    const QString key = scriptKey(QStringLiteral("install"), id);
    m_scriptBuffers.remove(key);
    m_scriptStartMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    // No visible console window; output is captured for live display.
    cmdLog(QStringLiteral("install"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(a.installCommand)));
    m_scripts->runShell(key, a.installCommand, 0, true);
}

void AgentLauncher::updateTool(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const Agent &a = m_model->agents().at(row);

    if (a.running) {
        cmdLogError(QStringLiteral("update"), id,
                    QStringLiteral("skipped, the agent is running"));
        emit launchFailed(id, tr("Please close %1 before installing/updating.").arg(a.name));
        return;
    }
    if (a.updateCommand.isEmpty()) {
        cmdLogError(QStringLiteral("update"), id,
                    QStringLiteral("skipped, no update command is configured"));
        emit installFinished(id, false,
            tr("No update command configured for %1.").arg(a.name));
        return;
    }

    m_model->setInstalling(id, true);
    m_model->setConsoleOutput(id, QString());

    const QString key = scriptKey(QStringLiteral("update"), id);
    m_scriptBuffers.remove(key);
    m_scriptStartMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    // No visible console window; output is captured for live display.
    cmdLog(QStringLiteral("update"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(a.updateCommand)));
    m_scripts->runShell(key, a.updateCommand, 0, true);
}

// --- ScriptRunner dispatch --------------------------------------------------
//
// Every one-shot command runs through core::ScriptRunner under a
// "<operation>:<agent id>" key. This is the single place that turns a
// finished run into card state, log lines and user-facing signals — the
// per-operation rules are unchanged from 0.3.0.

void AgentLauncher::onScriptChunk(const QString &key, const QString &text)
{
    QString operation, id;
    if (!splitScriptKey(key, operation, id))
        return;
    if (operation != QLatin1String("install")
        && operation != QLatin1String("update")
        && operation != QLatin1String("setup"))
        return; // version/runtime output is only read at completion

    QString &buffer = m_scriptBuffers[key];
    buffer += text;
    m_model->setConsoleOutput(id, buffer);
}

void AgentLauncher::onScriptFinished(const QString &key, bool ok, int exitCode,
                                     const QString &stdOut, const QString &stdErr,
                                     const QString &error)
{
    QString operation, id;
    if (!splitScriptKey(key, operation, id))
        return;

    const qint64 startMs = m_scriptStartMs.take(key);
    // Merged-channel runs report everything on stdout; separated runs put
    // stderr after stdout, matching 0.3.0's stdOutput + errOutput.
    const QString output = stdOut + stdErr;

    if (operation == QLatin1String("install")
        || operation == QLatin1String("update")) {
        m_model->setInstalling(id, false);
        m_scriptBuffers.remove(key);
        // Authoritative full text (the chunks above only streamed deltas).
        m_model->setConsoleOutput(id, output);

        if (!error.isEmpty()) {
            // The command never started (install/update have no timeout).
            cmdLogError(operation, id, error);
            emit installFinished(id, false,
                operation == QLatin1String("install")
                    ? tr("Failed to start install command.")
                    : tr("Failed to start update command."));
        } else if (ok) {
            cmdLog(operation, id, exitSummary(exitCode, startMs));
            logCommandOutput(operation, id, output);
            emit installFinished(id, true, QString());
        } else {
            cmdLogError(operation, id, exitSummary(exitCode, startMs));
            logCommandOutput(operation, id, output, true);
            QString detail = output.trimmed();
            if (detail.isEmpty())
                detail = tr("(no output)");
            emit installFinished(id, false,
                operation == QLatin1String("install")
                    ? tr("Install failed (exit code %1):\n%2")
                          .arg(exitCode).arg(detail)
                    : tr("Update failed (exit code %1):\n%2")
                          .arg(exitCode).arg(detail));
        }
        // Re-check version to refresh the card.
        checkVersion(id);
        return;
    }

    if (operation == QLatin1String("setup")) {
        const QString command = m_scriptCommands.take(key);
        m_model->setSetupping(id, false);
        m_scriptBuffers.remove(key);
        m_model->setConsoleOutput(id, output);

        if (!error.isEmpty()
            && error.startsWith(QLatin1String("failed to start"))) {
            cmdLogError(operation, id, error);
            emit launchFailed(id, tr("Failed to start setup command."));
            return;
        }
        if (ok) {
            cmdLog(operation, id, exitSummary(exitCode, startMs));
            logCommandOutput(operation, id, output);
            markSetupDone(id);
            // Proceed with the actual launch (clears the console area).
            doLaunch(id);
            return;
        }
        // Non-zero exit, or a timeout (ScriptRunner says so in `error`).
        cmdLogError(operation, id,
                    error.isEmpty() ? exitSummary(exitCode, startMs) : error);
        logCommandOutput(operation, id, output, true);
        QString detail = output.trimmed();
        if (detail.isEmpty())
            detail = tr("(no output)");
        emit launchFailed(id,
            tr("Setup command failed (exit code %1).\n\nCommand: %2\n\n%3")
                .arg(exitCode)
                .arg(command)
                .arg(detail));
        return;
    }

    if (operation == QLatin1String("version")) {
        // Captured now: the delayed spinner clear below must only fire if
        // no newer check started in the meantime.
        const int epoch = m_versionEpoch.value(id);

        if (!error.isEmpty()) {
            // Never started, or killed by the 10s safety timeout.
            cmdLogError(operation, id, error);
            m_model->setInstalled(id, false);
            m_model->setVersion(id, QString());
        } else {
            // Try to extract a version from stdout, then stderr — some
            // tools print version info to stderr.
            QString version = awb::core::TextUtils::extractVersion(stdOut);
            if (version.isEmpty())
                version = awb::core::TextUtils::extractVersion(stdErr);

            if (exitCode == 0 || !version.isEmpty()) {
                // Exit code 0, or we found a version string despite a
                // non-zero exit. Some tools exit non-zero for --version.
                m_model->setInstalled(id, true);
                m_model->setVersion(id, version);
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
                cmdLogError(operation, id, exitSummary(exitCode, startMs));
                logCommandOutput(operation, id, output, true);
                m_model->setInstalled(id, false);
                m_model->setVersion(id, QString());
            }
        }

        // Keep the spinner visible for at least 500 ms so it does not
        // flicker; guarded so a stale timer cannot clear a newer check.
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startMs;
        QTimer::singleShot(static_cast<int>(qMax(0LL, 500 - elapsed)), this,
                [this, id, epoch]() {
                    if (m_versionEpoch.value(id) == epoch)
                        m_model->setCheckingVersion(id, false);
                });
        return;
    }

    if (operation == QLatin1String("runtime")) {
        const bool isPython = (id == QLatin1String("Python"));

        QString version;
        if (error.isEmpty()) {
            version = awb::core::TextUtils::extractVersion(stdOut);
            if (version.isEmpty())
                version = awb::core::TextUtils::extractVersion(stdErr);
        }
        const bool installed =
            error.isEmpty() && (exitCode == 0 || !version.isEmpty());
        if (isPython) {
            m_pythonInstalled = installed;
            m_pythonVersion = installed ? version : QString();
        } else {
            m_nodeInstalled = installed;
            m_nodeVersion = installed ? version : QString();
        }

        if (!error.isEmpty()) {
            cmdLogError(operation, id, error);
        } else if (version.isEmpty()) {
            cmdLogError(operation, id,
                        QStringLiteral("%1, no version string in the output: %2")
                            .arg(exitSummary(exitCode, startMs),
                                 output.trimmed()));
        } else {
            cmdLog(operation, id,
                   QStringLiteral("%1 → %2")
                       .arg(exitSummary(exitCode, startMs), version));
        }
        emit runtimeVersionsChanged();
        return;
    }

    qWarning().noquote() << QStringLiteral(
        "ScriptRunner: finished for unknown operation '%1' (key %2)")
        .arg(operation, key);
}
