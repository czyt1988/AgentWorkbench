#include "AgentLauncher.h"
#include "AgentConfig.h"
#include "Logger.h"

#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
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

// Decode bytes captured from a child process (npm/node/PowerShell etc.).
// Modern CLI tools emit UTF-8, so try that first; if the bytes aren't valid
// UTF-8, fall back to the system locale codec (e.g. GBK on zh-CN Windows) so
// legacy batch/cmd output still decodes correctly instead of mojibake.
QString decodeProcessOutput(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString result = decoder.decode(data);
    if (!decoder.hasError())
        return result;
    return QString::fromLocal8Bit(data);
}

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
    const QString text = Logger::clampOutput(output).trimmed();
    const QString line = logPrefix(QStringLiteral("cmd"), operation, id)
                         + (text.isEmpty()
                                ? QStringLiteral("output: (none)")
                                : QStringLiteral("output:\n") + text);
    if (failure)
        qWarning().noquote() << line;
    else
        qInfo().noquote() << line;
}

// The kill command used to take an agent down, per platform. It is built once
// here and both logged and executed from the same values, so the log always
// shows the real thing.
QString killProgramName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("taskkill");
#else
    return QStringLiteral("kill");
#endif
}

QStringList killProgramArgs(qint64 pid)
{
#ifdef Q_OS_WIN
    // /F force, /T kills the whole process tree (cmd -> qwen.cmd -> node).
    return {QStringLiteral("/F"), QStringLiteral("/T"),
            QStringLiteral("/PID"), QString::number(pid)};
#else
    return {QStringLiteral("-9"), QString::number(pid)};
#endif
}

} // namespace

AgentLauncher::AgentLauncher(AgentModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_nam(new QNetworkAccessManager(this))
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &AgentLauncher::checkAll);
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
    const QString resolved = resolveProgram(program);
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

    // startDetached so the agent keeps running after this launcher closes.
    QProcess proc;
#ifdef Q_OS_WIN
    // A .cmd/.bat shim cannot be executed directly by CreateProcess; run it
    // through cmd.exe so the batch is interpreted (and a /T kill later covers
    // the whole cmd -> qwen.cmd -> node tree).
    if (resolved.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive)
        || resolved.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive)) {
        proc.setProgram(QStringLiteral("cmd"));
        QStringList cmdArgs;
        cmdArgs << QStringLiteral("/c") << resolved << args;
        proc.setArguments(cmdArgs);
    } else
#endif
    {
        proc.setProgram(resolved);
        proc.setArguments(args);
    }
    const QString cwd = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    proc.setWorkingDirectory(cwd);

    // The line the log exists for: what is really executed, after PATH
    // resolution and the cmd.exe wrapping of .cmd/.bat shims.
    cmdLog(QStringLiteral("launch"), id,
           QStringLiteral("running: %1  (cwd: %2)")
               .arg(Logger::formatCommandLine(proc.program(), proc.arguments()), cwd));

    // If a token file is configured, read it and set QWEN_SERVER_TOKEN in the
    // process environment. This lets the daemon pick up the bearer token
    // without a complex --token argument on the command line.
    if (!a.tokenFile.isEmpty()) {
        const QString tokenPath = expandEnv(a.tokenFile);
        QFile f(tokenPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString token = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
            if (!token.isEmpty()) {
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                env.insert(QStringLiteral("QWEN_SERVER_TOKEN"), token);
                proc.setProcessEnvironment(env);
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
    const bool ok = proc.startDetached(&pid);
    if (!ok) {
        cmdLogError(QStringLiteral("launch"), id,
                    QStringLiteral("failed to start: %1").arg(proc.errorString()));
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

    const QStringList args = killProgramArgs(pid);
    cmdLog(QStringLiteral("stop"), id,
           QStringLiteral("running: %1")
               .arg(Logger::formatCommandLine(killProgramName(), args)));
    const bool ok = QProcess::startDetached(killProgramName(), args);
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
    const int port = portFromWebUrl(a.webUrl);
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
    for (const qint64 pid : pids) {
        const QStringList args = killProgramArgs(pid);
        cmdLog(QStringLiteral("forceStop"), id,
               QStringLiteral("running: %1")
                   .arg(Logger::formatCommandLine(killProgramName(), args)));
        if (QProcess::startDetached(killProgramName(), args))
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

int AgentLauncher::portFromWebUrl(const QString &webUrl) const
{
    if (webUrl.isEmpty())
        return -1;
    const QUrl url(webUrl);
    if (!url.isValid())
        return -1;
    const int port = url.port();
    if (port > 0)
        return port;
    // No explicit port: fall back to the scheme default.
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https"))
        return 443;
    if (scheme == QLatin1String("http"))
        return 80;
    return -1;
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

    const QString output = decodeProcessOutput(proc.readAllStandardOutput());
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
    for (auto it = m_pids.constBegin(); it != m_pids.constEnd(); ++it) {
        const qint64 pid = *it;
        if (pid == 0)
            continue;
        const QStringList args = killProgramArgs(pid);
        cmdLog(QStringLiteral("stopAll"), it.key(),
               QStringLiteral("running: %1")
                   .arg(Logger::formatCommandLine(killProgramName(), args)));
        if (QProcess::startDetached(killProgramName(), args))
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
        const QString tokenPath = expandEnv(a.tokenFile);
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
    QString dir = expandEnv(m_model->agents().at(row).configDir);
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
    const QList<Agent> &agents = m_model->agents();
    for (const Agent &a : agents) {
        if (a.webUrl.isEmpty())
            continue;
        QNetworkReply *reply = m_nam->get(QNetworkRequest(QUrl(a.webUrl)));
        reply->setParent(this);
        const QString id = a.id;
        // Any HTTP response (even an error status) means the server is up.
        connect(reply, &QNetworkReply::finished, this, [this, id, reply]() {
            bool up = false;
            if (reply->error() == QNetworkReply::NoError) {
                up = true;
            } else {
                // Connection-refused/timeout => not running.
                // Got an HTTP error status (e.g. 401/404) => server is up.
                const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                up = (code > 0);
            }
            // Once the server is up, the launch is done: clear the spinner.
            if (up)
                m_model->setLaunching(id, false);
            // Report health transitions (not every poll) so the log tells the
            // story of when an agent came up or went down.
            const int row = m_model->indexOf(id);
            const bool wasRunning = row >= 0 && m_model->agents().at(row).running;
            if (up != wasRunning) {
                appLog(QStringLiteral("state"), id,
                       up ? QStringLiteral("stopped → running")
                          : QStringLiteral("running → stopped"));
            }
            m_model->setRunning(id, up);
            reply->deleteLater();
        });
    }
}

QString AgentLauncher::expandEnv(const QString &path) const
{
    QString result = path;
    // Expand %VAR% style variables (Windows), e.g. %USERPROFILE%.
    static const QRegularExpression re(QStringLiteral("%(\\w+)%"));
    QRegularExpressionMatchIterator it = re.globalMatch(result);
    QString out;
    int cursor = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += result.mid(cursor, m.capturedStart() - cursor);
        const QString var = m.captured(1);
        const QString val = qEnvironmentVariable(qUtf8Printable(var));
        out += val.isEmpty() ? m.captured(0) : val;
        cursor = m.capturedEnd();
    }
    out += result.mid(cursor);
    // Expand ~ to the home directory (unix style, convenience).
    out.replace(QStringLiteral("~/"),
                QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/"));
    return out;
}

QString AgentLauncher::resolveProgram(const QString &program)
{
    if (program.isEmpty())
        return QString();
    // findExecutable searches PATH and, on Windows, appends the PATHEXT
    // extensions (.exe/.cmd/.bat/...), which is exactly what is needed to
    // resolve npm-style shims like "qwen" -> "qwen.cmd".
    return QStandardPaths::findExecutable(program);
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

    const int epoch = ++m_versionEpoch[id];
    m_model->setCheckingVersion(id, true);

    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    auto *proc = new QProcess(this);
    proc->setProgram(QStringLiteral("cmd"));
    proc->setArguments({QStringLiteral("/c"), cmd});
    // Default channel mode → CREATE_NO_WINDOW → no visible console.
    cmdLog(QStringLiteral("version"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(cmd)));

    const QString capturedId = id;
    const int capturedEpoch = epoch;

    // Helper: clear checkingVersion after a minimum 500 ms visibility window.
    // Guarded by epoch so a stale timer from a previous check can't clear the
    // flag while a newer check is still in progress. Safe to call multiple
    // times (e.g. timeout kill also triggers finished) — only the first
    // matching-epoch call actually clears the flag.
    auto scheduleClear = [this, capturedId, capturedEpoch, startMs]() {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startMs;
        const qint64 delay = qMax(0LL, 500 - elapsed);
        QTimer::singleShot(static_cast<int>(delay), this,
                [this, capturedId, capturedEpoch]() {
                    if (m_versionEpoch.value(capturedId) == capturedEpoch)
                        m_model->setCheckingVersion(capturedId, false);
                });
    };

    connect(proc, &QProcess::finished, this,
            [this, capturedId, capturedEpoch, startMs, proc, scheduleClear](int exitCode, QProcess::ExitStatus) {
                const QString stdOutput =
                    decodeProcessOutput(proc->readAllStandardOutput());
                const QString errOutput =
                    decodeProcessOutput(proc->readAllStandardError());

                // Try to extract a version from stdout, then stderr — some
                // tools print version info to stderr.
                QString version = extractVersion(stdOutput);
                if (version.isEmpty())
                    version = extractVersion(errOutput);

                if (exitCode == 0 || !version.isEmpty()) {
                    // Exit code 0, or we found a version string despite a
                    // non-zero exit. Some tools exit non-zero for --version.
                    m_model->setInstalled(capturedId, true);
                    m_model->setVersion(capturedId, version);
                    if (version.isEmpty()) {
                        cmdLog(QStringLiteral("version"), capturedId,
                               QStringLiteral("%1, but no version string in the output")
                                   .arg(exitSummary(exitCode, startMs)));
                        logCommandOutput(QStringLiteral("version"), capturedId,
                                         stdOutput + errOutput);
                    } else {
                        cmdLog(QStringLiteral("version"), capturedId,
                               QStringLiteral("%1 → %2")
                                   .arg(exitSummary(exitCode, startMs), version));
                    }
                } else {
                    cmdLogError(QStringLiteral("version"), capturedId,
                                exitSummary(exitCode, startMs));
                    logCommandOutput(QStringLiteral("version"), capturedId,
                                     stdOutput + errOutput, true);
                    m_model->setInstalled(capturedId, false);
                    m_model->setVersion(capturedId, QString());
                }
                scheduleClear();
                proc->deleteLater();
            });

    connect(proc, &QProcess::errorOccurred, this,
            [this, capturedId, capturedEpoch, proc, scheduleClear](QProcess::ProcessError) {
                if (proc->state() == QProcess::NotRunning) {
                    cmdLogError(QStringLiteral("version"), capturedId,
                                QStringLiteral("failed to start: %1")
                                    .arg(proc->errorString()));
                    m_model->setInstalled(capturedId, false);
                    m_model->setVersion(capturedId, QString());
                    scheduleClear();
                    proc->deleteLater();
                }
            });

    // Safety timeout: kill hung version commands after 10s.
    QTimer::singleShot(10000, proc, [this, capturedId, capturedEpoch, proc, scheduleClear]() {
        if (proc->state() != QProcess::NotRunning) {
            cmdLogError(QStringLiteral("version"), capturedId,
                        QStringLiteral("timed out after 10s, killing it"));
            proc->kill();
            m_model->setInstalled(capturedId, false);
            m_model->setVersion(capturedId, QString());
            scheduleClear();
        }
    });

    proc->start();
}

QString AgentLauncher::extractVersion(const QString &output)
{
    // Match x.y.z (optionally with a pre-release suffix), e.g. "1.2.3",
    // "v1.2.3-beta", "1.2.3.4".
    static const QRegularExpression re(QStringLiteral("(\\d+\\.\\d+\\.\\d+[\\w.-]*)"));
    const auto match = re.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
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

    // Write the setup command to a temporary .cmd file and execute that.
    // QProcess on Windows escapes internal " as \" (the C convention), but
    // cmd.exe doesn't understand \" — it treats \ as a literal character,
    // corrupting paths and causing "invalid filename syntax" errors. Writing
    // to a batch file sidesteps QProcess argument quoting entirely.
    auto *batchFile = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/agentlauncher_XXXXXX.cmd"));
    if (!batchFile->open()) {
        m_model->setSetupping(id, false);
        cmdLogError(QStringLiteral("setup"), id,
                    QStringLiteral("cannot create the temporary batch file: %1")
                        .arg(batchFile->errorString()));
        emit launchFailed(id, tr("Failed to create a temporary batch file for setup."));
        delete batchFile;
        return;
    }
    batchFile->write(QStringLiteral("@echo off\r\n").toLocal8Bit());
    batchFile->write(cmd.toLocal8Bit());
    batchFile->write("\r\n");
    batchFile->close();

    auto *proc = new QProcess(this);
    batchFile->setParent(proc); // cleaned up when proc is deleteLater'd
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setProgram(QStringLiteral("cmd"));
    proc->setArguments({QStringLiteral("/c"), batchFile->fileName()});
    // No visible console window; output is captured for live display.
    cmdLog(QStringLiteral("setup"), id,
           QStringLiteral("running: %1")
               .arg(Logger::formatCommandLine(proc->program(), proc->arguments())));

    const QString capturedId = id;
    const QString capturedCmd = cmd;
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    QSharedPointer<QString> buffer = QSharedPointer<QString>::create();

    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, capturedId, proc, buffer]() {
                buffer->append(decodeProcessOutput(proc->readAllStandardOutput()));
                m_model->setConsoleOutput(capturedId, *buffer);
            });

    connect(proc, &QProcess::finished, this,
            [this, capturedId, capturedCmd, startMs, proc, buffer](int exitCode, QProcess::ExitStatus) {
                m_model->setSetupping(capturedId, false);

                // Drain any tail bytes, then push the final text to the card.
                buffer->append(decodeProcessOutput(proc->readAllStandardOutput()));
                buffer->append(decodeProcessOutput(proc->readAllStandardError()));
                m_model->setConsoleOutput(capturedId, *buffer);

                if (exitCode == 0) {
                    cmdLog(QStringLiteral("setup"), capturedId,
                           exitSummary(exitCode, startMs));
                    logCommandOutput(QStringLiteral("setup"), capturedId, *buffer);
                    markSetupDone(capturedId);
                    // Proceed with the actual launch (clears the console area).
                    doLaunch(capturedId);
                } else {
                    cmdLogError(QStringLiteral("setup"), capturedId,
                                exitSummary(exitCode, startMs));
                    logCommandOutput(QStringLiteral("setup"), capturedId, *buffer,
                                     true);
                    const QString detail = buffer->trimmed().isEmpty()
                        ? tr("(no output)") : buffer->trimmed();
                    emit launchFailed(capturedId,
                        tr("Setup command failed (exit code %1).\n\n"
                           "Command: %2\n\n%3")
                            .arg(exitCode)
                            .arg(capturedCmd)
                            .arg(detail));
                }
                proc->deleteLater();
            });

    connect(proc, &QProcess::errorOccurred, this,
            [this, capturedId, proc](QProcess::ProcessError) {
                if (proc->state() == QProcess::NotRunning) {
                    m_model->setSetupping(capturedId, false);
                    cmdLogError(QStringLiteral("setup"), capturedId,
                                QStringLiteral("failed to start: %1")
                                    .arg(proc->errorString()));
                    emit launchFailed(capturedId,
                        tr("Failed to start setup command."));
                    proc->deleteLater();
                }
            });

    // Safety timeout: kill hung setup commands after 30s.
    QTimer::singleShot(30000, proc, [this, capturedId, proc]() {
        if (proc->state() != QProcess::NotRunning) {
            cmdLogError(QStringLiteral("setup"), capturedId,
                        QStringLiteral("timed out after 30s, killing it"));
            proc->kill();
        }
    });

    proc->start();
}

// --- Setup state persistence -----------------------------------------------

QString AgentLauncher::stateFilePath() const
{
    // Co-located with agents.json in the user data directory.
    return AgentConfig::userDataDir() + QStringLiteral("/agent_state.json");
}

void AgentLauncher::loadSetupState()
{
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();

    for (Agent &a : m_model->agents()) {
        const QJsonObject agentState = root.value(a.id).toObject();
        if (agentState.value(QStringLiteral("setupDone")).toBool())
            a.setupDone = true;
    }
}

void AgentLauncher::markSetupDone(const QString &id)
{
    m_model->setSetupDone(id, true);

    // Persist to agent_state.json.
    QFile file(stateFilePath());
    QJsonObject root;
    if (file.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }

    QJsonObject agentState = root.value(id).toObject();
    agentState[QStringLiteral("setupDone")] = true;
    root[id] = agentState;

    QDir().mkpath(QFileInfo(stateFilePath()).absolutePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
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

    // Remove from agent_state.json.
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    root.remove(id);

    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
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
    const QString resolved = QStandardPaths::findExecutable(program);
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

    auto *proc = new QProcess(this);
    proc->setProgram(QStringLiteral("cmd"));
    proc->setArguments({QStringLiteral("/c"), program + QStringLiteral(" ") + versionArg});

    const QString capturedRuntime = runtimeName;
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    // The probe hands cmd one string ("python --version"), so it is reported
    // the same way as the other cmd /c lines rather than as a quoted argv.
    cmdLog(QStringLiteral("runtime"), runtimeName,
           QStringLiteral("running: %1")
               .arg(shellCommandLine(program + QLatin1Char(' ') + versionArg)));

    connect(proc, &QProcess::finished, this,
            [this, capturedRuntime, startMs, proc](int exitCode, QProcess::ExitStatus) {
                const QString stdOutput =
                    QString::fromLocal8Bit(proc->readAllStandardOutput());
                const QString errOutput =
                    QString::fromLocal8Bit(proc->readAllStandardError());

                // Some runtimes print version to stdout, others to stderr
                // (older Python). Check both.
                QString version = extractVersion(stdOutput);
                if (version.isEmpty())
                    version = extractVersion(errOutput);

                if (capturedRuntime == QLatin1String("Python")) {
                    if (exitCode == 0 || !version.isEmpty()) {
                        m_pythonInstalled = true;
                        m_pythonVersion = version;
                    } else {
                        m_pythonInstalled = false;
                        m_pythonVersion.clear();
                    }
                } else {
                    if (exitCode == 0 || !version.isEmpty()) {
                        m_nodeInstalled = true;
                        m_nodeVersion = version;
                    } else {
                        m_nodeInstalled = false;
                        m_nodeVersion.clear();
                    }
                }
                if (version.isEmpty()) {
                    cmdLogError(QStringLiteral("runtime"), capturedRuntime,
                                QStringLiteral("%1, no version string in the output: %2")
                                    .arg(exitSummary(exitCode, startMs),
                                         (stdOutput + errOutput).trimmed()));
                } else {
                    cmdLog(QStringLiteral("runtime"), capturedRuntime,
                           QStringLiteral("%1 → %2")
                               .arg(exitSummary(exitCode, startMs), version));
                }
                emit runtimeVersionsChanged();
                proc->deleteLater();
            });

    connect(proc, &QProcess::errorOccurred, this,
            [this, capturedRuntime, proc](QProcess::ProcessError) {
                if (proc->state() == QProcess::NotRunning) {
                    cmdLogError(QStringLiteral("runtime"), capturedRuntime,
                                QStringLiteral("failed to start: %1")
                                    .arg(proc->errorString()));
                    if (capturedRuntime == QLatin1String("Python")) {
                        m_pythonInstalled = false;
                        m_pythonVersion.clear();
                    } else {
                        m_nodeInstalled = false;
                        m_nodeVersion.clear();
                    }
                    emit runtimeVersionsChanged();
                    proc->deleteLater();
                }
            });

    // Safety timeout: kill hung detection after 10s.
    QTimer::singleShot(10000, proc, [this, capturedRuntime, proc]() {
        if (proc->state() != QProcess::NotRunning) {
            cmdLogError(QStringLiteral("runtime"), capturedRuntime,
                        QStringLiteral("timed out after 10s, killing it"));
            proc->kill();
            if (capturedRuntime == QLatin1String("Python")) {
                m_pythonInstalled = false;
                m_pythonVersion.clear();
            } else {
                m_nodeInstalled = false;
                m_nodeVersion.clear();
            }
            emit runtimeVersionsChanged();
        }
    });

    proc->start();
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

    auto *proc = new QProcess(this);
    // Merge stderr into stdout so both streams surface on one channel and can
    // be streamed to the card live.
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setProgram(QStringLiteral("cmd"));
    proc->setArguments({QStringLiteral("/c"), a.installCommand});
    // No visible console window; output is captured for live display.
    cmdLog(QStringLiteral("install"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(a.installCommand)));

    const QString capturedId = id;
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    QPointer<QProcess> guard(proc);
    bool *handled = new bool(false);
    // Accumulator shared between the readyRead and finished handlers so the
    // streamed text is also reused for the failure-detail message.
    QSharedPointer<QString> buffer = QSharedPointer<QString>::create();

    // Stream output to the card as it arrives so the user can watch progress
    // instead of staring at a spinner.
    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, capturedId, guard, buffer]() {
                if (!guard)
                    return;
                buffer->append(decodeProcessOutput(guard->readAllStandardOutput()));
                m_model->setConsoleOutput(capturedId, *buffer);
            });

    connect(proc, &QProcess::finished, this,
            [this, capturedId, startMs, guard, handled, buffer](int exitCode, QProcess::ExitStatus) {
                if (*handled)
                    return;
                *handled = true;
                m_model->setInstalling(capturedId, false);

                // Drain any tail bytes emitted between the last readyRead and
                // exit, then push the final text to the card.
                if (guard) {
                    buffer->append(decodeProcessOutput(guard->readAllStandardOutput()));
                    buffer->append(decodeProcessOutput(guard->readAllStandardError()));
                    m_model->setConsoleOutput(capturedId, *buffer);
                    guard->deleteLater();
                }
                delete handled;

                if (exitCode == 0) {
                    cmdLog(QStringLiteral("install"), capturedId,
                           exitSummary(exitCode, startMs));
                    logCommandOutput(QStringLiteral("install"), capturedId, *buffer);
                    emit installFinished(capturedId, true, QString());
                } else {
                    cmdLogError(QStringLiteral("install"), capturedId,
                                exitSummary(exitCode, startMs));
                    logCommandOutput(QStringLiteral("install"), capturedId, *buffer,
                                     true);
                    QString detail = buffer->trimmed();
                    if (detail.isEmpty())
                        detail = tr("(no output)");
                    emit installFinished(capturedId, false,
                        tr("Install failed (exit code %1):\n%2")
                            .arg(exitCode).arg(detail));
                }
                // Re-check version to refresh the card.
                checkVersion(capturedId);
            });

    connect(proc, &QProcess::errorOccurred, this,
            [this, capturedId, guard, handled](QProcess::ProcessError) {
                // Only act if the process never started; otherwise finished
                // will handle cleanup.
                if (*handled)
                    return;
                if (guard && guard->state() == QProcess::NotRunning) {
                    *handled = true;
                    m_model->setInstalling(capturedId, false);
                    guard->deleteLater();
                    delete handled;
                    cmdLogError(QStringLiteral("install"), capturedId,
                                QStringLiteral("failed to start: %1")
                                    .arg(guard->errorString()));
                    emit installFinished(capturedId, false,
                        tr("Failed to start install command."));
                }
            });

    proc->start();
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

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setProgram(QStringLiteral("cmd"));
    proc->setArguments({QStringLiteral("/c"), a.updateCommand});
    // No visible console window; output is captured for live display.
    cmdLog(QStringLiteral("update"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(a.updateCommand)));

    const QString capturedId = id;
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    QPointer<QProcess> guard(proc);
    bool *handled = new bool(false);
    QSharedPointer<QString> buffer = QSharedPointer<QString>::create();

    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, capturedId, guard, buffer]() {
                if (!guard)
                    return;
                buffer->append(decodeProcessOutput(guard->readAllStandardOutput()));
                m_model->setConsoleOutput(capturedId, *buffer);
            });

    connect(proc, &QProcess::finished, this,
            [this, capturedId, startMs, guard, handled, buffer](int exitCode, QProcess::ExitStatus) {
                if (*handled)
                    return;
                *handled = true;
                m_model->setInstalling(capturedId, false);

                if (guard) {
                    buffer->append(decodeProcessOutput(guard->readAllStandardOutput()));
                    buffer->append(decodeProcessOutput(guard->readAllStandardError()));
                    m_model->setConsoleOutput(capturedId, *buffer);
                    guard->deleteLater();
                }
                delete handled;

                if (exitCode == 0) {
                    cmdLog(QStringLiteral("update"), capturedId,
                           exitSummary(exitCode, startMs));
                    logCommandOutput(QStringLiteral("update"), capturedId, *buffer);
                    emit installFinished(capturedId, true, QString());
                } else {
                    cmdLogError(QStringLiteral("update"), capturedId,
                                exitSummary(exitCode, startMs));
                    logCommandOutput(QStringLiteral("update"), capturedId, *buffer,
                                     true);
                    QString detail = buffer->trimmed();
                    if (detail.isEmpty())
                        detail = tr("(no output)");
                    emit installFinished(capturedId, false,
                        tr("Update failed (exit code %1):\n%2")
                            .arg(exitCode).arg(detail));
                }
                checkVersion(capturedId);
            });

    connect(proc, &QProcess::errorOccurred, this,
            [this, capturedId, guard, handled](QProcess::ProcessError) {
                if (*handled)
                    return;
                if (guard && guard->state() == QProcess::NotRunning) {
                    *handled = true;
                    m_model->setInstalling(capturedId, false);
                    guard->deleteLater();
                    delete handled;
                    cmdLogError(QStringLiteral("update"), capturedId,
                                QStringLiteral("failed to start: %1")
                                    .arg(guard->errorString()));
                    emit installFinished(capturedId, false,
                        tr("Failed to start update command."));
                }
            });

    proc->start();
}
