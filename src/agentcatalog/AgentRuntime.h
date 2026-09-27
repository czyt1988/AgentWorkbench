#ifndef AWB_AGENTS_AGENTRUNTIME_H
#define AWB_AGENTS_AGENTRUNTIME_H

#include "agentcatalog/AgentDefinition.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace awb::agents {

class AgentModel;

// The agent's own long-running process: launch, stop, force-stop, and the
// session-scoped PID bookkeeping.
//
// PIDs live in memory only: an agent detected as running by the HTTP health
// check but not started from this launcher session has no PID and can only
// be force-stopped by port.
//
// Token-gated harnesses (dsh) print a per-process authenticated URL to
// stdout instead of writing it to a token file. launch() redirects the
// child's output to <logsDir>/output/<id>.log and watches that file until a
// URL pointing at the agent's webUrl server shows up (sessionUrlChanged) —
// the web surface needs it because the bare webUrl is answered with 401.
class AgentRuntime : public QObject
{
    Q_OBJECT

public:
    explicit AgentRuntime(AgentModel *model, QObject *parent = nullptr);

    // Start the agent's process detached (it survives this application
    // exiting). `tokenValue` is handed to the child as QWEN_SERVER_TOKEN
    // when the definition has a tokenFile.
    void launch(const AgentDefinition &definition, const QString &tokenValue);

    // Kill the process tree of an agent started in this session. Returns
    // false (and emits launchFailed) when no PID is tracked.
    bool stop(const QString &id);

    // Kill whatever listens on the agent's web port — works even for
    // agents this launcher did not start (explicit user action).
    void forceStop(const QString &id);

    // True if at least one agent was started from this launcher this session.
    bool hasLaunchedAgents() const;

    // Terminate every process started this session; returns the number of
    // process trees successfully killed.
    int stopAll();

    // Forget the tracked PID (the agent's process keeps running — used when
    // an agent is removed from the configuration).
    void forget(const QString &id);

    // Return the PIDs of processes listening on the given TCP port. The
    // port→PID parsing behind forceStop(); public so the mapping can be
    // verified directly (the logic is kept AND has cases).
    static QList<qint64> findPidsForPort(int port);

    // The captured session URL for the agent (empty when none). Dropped
    // when the agent's process is stopped — the per-process token died with
    // it, so openWeb falls back to the configured webUrl.
    QString sessionUrl(const QString &id) const;

signals:
    // A launch/stop attempt failed. The UI shows an at-place flash on the
    // matching card plus a detailed popup.
    void launchFailed(const QString &id, const QString &message);

    // Ask the health monitor for an immediate re-check so cards flip state
    // quickly after a launch/stop (0.3.0 re-checked on a short timer).
    void recheckRequested();

    // The launch captured the agent's authenticated session URL (dsh prints
    // a per-process token URL to its output). Emitted at most once per
    // launch; the URL never reaches the log.
    void sessionUrlChanged(const QString &id, const QString &url);

private:
    void watchSessionUrl(const QString &id);
    void dropSessionUrl(const QString &id);

    AgentModel *m_model;

    // id -> PID of the most recent process this launcher started (in-memory,
    // current session only). Cleared when the launcher restarts.
    QHash<QString, qint64> m_pids;

    // id -> launch epoch, bumped on each successful launch. Invalidates the
    // stale "launching" safety timeout of an earlier attempt.
    QHash<QString, int> m_launchEpoch;

    // id -> the URL captured from that agent's output this launch.
    QHash<QString, QString> m_sessionUrls;

    // One pending output watch: what to match and how many 500 ms ticks are
    // left before giving up (agents that print no URL).
    struct SessionWatch {
        QString webUrl;
        QString tokenFile;
        int attemptsLeft;
    };
    QHash<QString, SessionWatch> m_sessionUrlWatch;

    // The output-polling timer while a session URL is still being sought.
    QTimer m_sessionUrlTimer;
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTRUNTIME_H
