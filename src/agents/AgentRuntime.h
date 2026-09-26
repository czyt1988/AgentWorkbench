#ifndef AWB_AGENTS_AGENTRUNTIME_H
#define AWB_AGENTS_AGENTRUNTIME_H

#include "agents/AgentDefinition.h"

#include <QHash>
#include <QObject>
#include <QStringList>

namespace awb::agents {

class AgentModel;

// The agent's own long-running process: launch, stop, force-stop, and the
// session-scoped PID bookkeeping (01-architecture.md §4.3).
//
// PIDs live in memory only: an agent detected as running by the HTTP health
// check but not started from this launcher session has no PID and can only
// be force-stopped by port.
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

signals:
    // A launch/stop attempt failed. The UI shows an at-place flash on the
    // matching card plus a detailed popup.
    void launchFailed(const QString &id, const QString &message);

    // Ask the health monitor for an immediate re-check so cards flip state
    // quickly after a launch/stop (0.3.0 re-checked on a short timer).
    void recheckRequested();

private:
    // Return the PIDs of processes listening on the given TCP port. Used by
    // forceStop() to kill agents this launcher didn't start (no tracked PID).
    static QList<qint64> findPidsForPort(int port);

    AgentModel *m_model;

    // id -> PID of the most recent process this launcher started (in-memory,
    // current session only). Cleared when the launcher restarts.
    QHash<QString, qint64> m_pids;

    // id -> launch epoch, bumped on each successful launch. Invalidates the
    // stale "launching" safety timeout of an earlier attempt.
    QHash<QString, int> m_launchEpoch;
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTRUNTIME_H
