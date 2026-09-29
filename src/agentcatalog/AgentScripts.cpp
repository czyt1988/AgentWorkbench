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

// Operational log helpers, byte-compatible with 0.3.0 ("[cmd] install
// \"opencode\": running: cmd /c …", "done, exit=0").
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

// How the launcher runs a raw command string.
QString shellCommandLine(const QString &command)
{
    return QStringLiteral("cmd /c ") + command;
}

// Verbatim output of a finished command, capped so one chatty command cannot
// fill the log. `failure` mirrors the severity of the matching outcome line.
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

// One ScriptRunner slot per "<operation>:<agent id>", so concurrent
// operations on the same agent (a version check during an install) never
// kill each other.
QString scriptKey(const QString &operation, const QString &id)
{
    return operation + QLatin1Char(':') + id;
}

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

AgentScripts::AgentScripts(AgentModel *model, AgentStateStore *stateStore,
                           QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_stateStore(stateStore)
{
    // The runner instance is per-scripts object so keys stay private to
    // this module; slots in AgentRuntime never touch it.
    m_runner = new core::ScriptRunner(this);
    connect(m_runner, &core::ScriptRunner::outputChunk, this,
            &AgentScripts::onScriptChunk);
    connect(m_runner, &core::ScriptRunner::finished, this,
            &AgentScripts::onScriptFinished);
}

// --- Install / Update -----------------------------------------------------

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

    // Clear any previous output before flipping the card to "installing" so
    // the panel never flashes stale text from a prior run when it (re)opens.
    m_model->setConsoleOutput(id, QString());
    m_model->setInstalling(id, true);

    const QString key = scriptKey(QStringLiteral("install"), id);
    m_buffers.remove(key);
    m_startMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    // No visible console window; output is captured for live display.
    cmdLog(QStringLiteral("install"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(a.installCommand)));
    m_runner->runShell(key, a.installCommand, 0, true);
}

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
    // No visible console window; output is captured for live display.
    cmdLog(QStringLiteral("update"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(a.updateCommand)));
    m_runner->runShell(key, a.updateCommand, 0, true);
}

// --- Setup (first-run prerequisite) ----------------------------------------

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
    m_buffers.remove(key);
    m_startMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    m_setupCommands.insert(key, cmd);
    // The batch file sidesteps cmd.exe quoting (see ScriptRunner::runBatch);
    // 30s safety timeout kills a hung setup.
    m_runner->runBatch(key, cmd, 30000);
}

// --- Version detection -------------------------------------------------------

void AgentScripts::checkVersions()
{
    for (const AgentDefinition &a : m_model->definitions()) {
        checkVersion(a.id);
    }
}

void AgentScripts::checkVersion(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return;
    }
    const QString cmd = m_model->definitions().at(row).versionCommand;
    if (cmd.isEmpty()) {
        return;
    }

    ++m_versionEpoch[id];
    m_model->setCheckingVersion(id, true);

    const QString key = scriptKey(QStringLiteral("version"), id);
    m_startMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    cmdLog(QStringLiteral("version"), id,
           QStringLiteral("running: %1").arg(shellCommandLine(cmd)));
    // Separate channels: some tools print the version to stderr. 10s safety
    // timeout kills a hung check; a stale run for this key is invalidated by
    // ScriptRunner, and the delayed spinner clear in onScriptFinished is
    // guarded by m_versionEpoch.
    m_runner->runShell(key, cmd, 10000, false);
}

// --- ScriptRunner dispatch ---------------------------------------------------

void AgentScripts::onScriptChunk(const QString &key, const QString &text)
{
    QString operation, id;
    if (!splitScriptKey(key, operation, id)) {
        return;
    }
    if (operation != QStringLiteral("install")
        && operation != QStringLiteral("update")
        && operation != QStringLiteral("setup")) {
        return; // version output is only read at completion
    }

    QString &buffer = m_buffers[key];
    buffer += text;
    m_model->setConsoleOutput(id, buffer);
}

void AgentScripts::onScriptFinished(const QString &key, bool ok, int exitCode,
                                    const QString &stdOut, const QString &stdErr,
                                    const QString &error)
{
    QString operation, id;
    if (!splitScriptKey(key, operation, id)) {
        return;
    }

    const qint64 startMs = m_startMs.take(key);
    // Merged-channel runs report everything on stdout; separated runs put
    // stderr after stdout, matching 0.3.0's stdOutput + errOutput.
    const QString output = stdOut + stdErr;

    if (operation == QStringLiteral("install")
        || operation == QStringLiteral("update")) {
        m_model->setInstalling(id, false);
        m_buffers.remove(key);
        // Authoritative full text (the chunks above only streamed deltas).
        m_model->setConsoleOutput(id, output);

        if (!error.isEmpty()) {
            // The command never started (install/update have no timeout).
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
        // Re-check version to refresh the card.
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
        // Non-zero exit, or a timeout (ScriptRunner says so in `error`).
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
            QString version = core::TextUtils::extractVersion(stdOut);
            if (version.isEmpty()) {
                version = core::TextUtils::extractVersion(stdErr);
            }

            if (exitCode == 0 || !version.isEmpty()) {
                // Exit code 0, or we found a version string despite a
                // non-zero exit. Some tools exit non-zero for --version.
                m_model->setInstalled(id, true);
                m_model->setVersion(id, version);
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
