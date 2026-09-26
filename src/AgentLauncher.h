#ifndef AGENTLAUNCHER_H
#define AGENTLAUNCHER_H

#include "AgentModel.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class AgentLauncher : public QObject
{
    Q_OBJECT

    // Runtime version detection (Python / Node.js) for the top-right badges.
    Q_PROPERTY(QString pythonVersion READ pythonVersion NOTIFY runtimeVersionsChanged)
    Q_PROPERTY(bool pythonInstalled READ pythonInstalled NOTIFY runtimeVersionsChanged)
    Q_PROPERTY(QString nodeVersion READ nodeVersion NOTIFY runtimeVersionsChanged)
    Q_PROPERTY(bool nodeInstalled READ nodeInstalled NOTIFY runtimeVersionsChanged)

public:
    explicit AgentLauncher(AgentModel *model, QObject *parent = nullptr);

    QString pythonVersion() const { return m_pythonVersion; }
    bool pythonInstalled() const { return m_pythonInstalled; }
    QString nodeVersion() const { return m_nodeVersion; }
    bool nodeInstalled() const { return m_nodeInstalled; }

    Q_INVOKABLE void launch(const QString &id);
    Q_INVOKABLE bool stop(const QString &id);
    // Force-stop: kill the process listening on the agent's web port, even
    // when the launcher didn't start it (no tracked PID). Used by the card's
    // right-click "Force Stop" action.
    Q_INVOKABLE void forceStop(const QString &id);
    Q_INVOKABLE void openWeb(const QString &id);
    Q_INVOKABLE void openConfigDir(const QString &id);
    Q_INVOKABLE void install(const QString &id);
    Q_INVOKABLE void updateTool(const QString &id);
    Q_INVOKABLE void resetSetup(const QString &id);

    // True if at least one agent was started from the launcher this session.
    Q_INVOKABLE bool hasLaunchedAgents() const;

    // Terminate every process this launcher started this session. Returns the
    // number of process trees successfully killed.
    Q_INVOKABLE int stopAll();

    // Start the background health-check polling.
    void start();

    // Launcher management (Settings page). All three persist to agents.json
    // and return false when the file could not be written (or, for addAgent,
    // when the requested id already exists).
    Q_INVOKABLE bool addAgent(const QVariantMap &fields);
    Q_INVOKABLE bool updateAgentFull(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool removeAgent(const QString &id);
    Q_INVOKABLE bool restoreDefaults();

    // True when the id belongs to the bundled default_agents.json.
    Q_INVOKABLE bool isDefaultAgent(const QString &id) const;

    // Path of the on-disk agents.json (shown in error messages).
    Q_INVOKABLE QString configFilePath() const;

    // Deleted built-in ids to persist across restarts; loaded once at
    // startup by main.cpp.
    void setRemovedIds(const QStringList &ids) { m_removedIds = ids; }

signals:
    // Emitted when a launch/stop attempt fails. The UI shows an at-place
    // flash on the matching card plus a detailed popup.
    void launchFailed(const QString &id, const QString &message);

    // Emitted when an install/update finishes (success or failure).
    void installFinished(const QString &id, bool success, const QString &message);

    // Emitted when Python/Node.js version detection completes.
    void runtimeVersionsChanged();

private slots:
    void checkAll();

private:
    AgentModel *m_model;
    QNetworkAccessManager *m_nam;
    QTimer *m_timer;

    // id -> PID of the most recent process this launcher started (in-memory,
    // current session only). Used by stop(); cleared if the launcher restarts.
    QHash<QString, qint64> m_pids;

    // id -> launch epoch, bumped on each successful launch. Used to invalidate
    // stale "launching" safety timeouts so a re-launch can't be cleared early
    // by a timer from a previous attempt.
    QHash<QString, int> m_launchEpoch;

    // id -> version-check epoch, bumped on each checkVersion() call. Used to
    // invalidate stale delayed-clear timers so a re-check can't have its
    // spinner cleared early by a timer from a previous check.
    QHash<QString, int> m_versionEpoch;

    // Deleted built-in ids, persisted as the root "removed" array so the
    // migration on next startup does not resurrect them.
    QStringList m_removedIds;

    // Write the model's agents + removal records to agents.json.
    bool saveConfig();

    QString expandEnv(const QString &path) const;

    // Resolve a bare command (e.g. "qwen") to a full executable path,
    // applying PATHEXT on Windows so .cmd/.bat shims are found.
    static QString resolveProgram(const QString &program);

    // Parse the TCP port from an agent's webUrl. Returns the explicit port,
    // or the scheme default (80/443) when none is given; -1 if unparseable.
    int portFromWebUrl(const QString &webUrl) const;

    // Return the PIDs of processes listening on the given TCP port. Used by
    // forceStop() to kill agents this launcher didn't start (no tracked PID).
    QList<qint64> findPidsForPort(int port) const;

    // Run each agent's versionCommand silently on startup.
    void checkVersions();
    void checkVersion(const QString &id);

    // Extract a x.y.z version string from command output.
    static QString extractVersion(const QString &output);

    // One-time setup: run setupCommand before first launch, persist state.
    void runSetup(const QString &id);
    void doLaunch(const QString &id);
    QString stateFilePath() const;
    void loadSetupState();
    void markSetupDone(const QString &id);

    // Detect installed Python/Node.js versions for the top-right badges.
    void detectRuntimeVersions();
    void detectRuntime(const QString &program, const QString &versionArg,
                       const QString &runtimeName);

    QString m_pythonVersion;
    bool m_pythonInstalled = false;
    QString m_nodeVersion;
    bool m_nodeInstalled = false;
};

#endif // AGENTLAUNCHER_H
