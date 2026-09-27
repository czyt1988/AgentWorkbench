#ifndef AWB_AGENTS_AGENTSCRIPTS_H
#define AWB_AGENTS_AGENTSCRIPTS_H

#include <QHash>
#include <QObject>
#include <QString>

namespace awb::core {
class ScriptRunner;
} // namespace awb::core

namespace awb::agents {

class AgentModel;
class AgentStateStore;

// One-shot commands for agents — install / update / version / setup.
// Each command runs through core::ScriptRunner
// under a "<operation>:<id>" key; this class owns the per-operation rules
// (log lines, card state, user-facing signals) that were part of the 0.3.0
// AgentLauncher.
class AgentScripts : public QObject
{
    Q_OBJECT

public:
    AgentScripts(AgentModel *model, AgentStateStore *stateStore,
                 QObject *parent = nullptr);

    // Each method checks its own preconditions (running agent, missing
    // command) exactly like 0.3.0 and reports failures through the signals
    // below.
    void install(const QString &id);
    void update(const QString &id);
    void runSetup(const QString &id);

    // Run each agent's versionCommand (spinner first, then the probe).
    void checkVersions();
    void checkVersion(const QString &id);

signals:
    // An install/update finished (success or failure) — relayed to QML by
    // the facade under the same name it had in 0.3.0.
    void installFinished(const QString &id, bool success, const QString &message);

    // Launch-blocking failures: setup failed to start/run, or the agent is
    // running while an install was requested.
    void launchFailed(const QString &id, const QString &message);

    // The version probe resolved a version string for the card.
    void versionResolved(const QString &id, const QString &version);

    // A one-time setup finished; ok=true means it succeeded (state already
    // recorded in the AgentStateStore) and the caller may launch now.
    void setupFinished(const QString &id, bool ok);

private slots:
    void onScriptFinished(const QString &key, bool ok, int exitCode,
                          const QString &stdOut, const QString &stdErr,
                          const QString &error);
    void onScriptChunk(const QString &key, const QString &text);

private:
    AgentModel *m_model;
    AgentStateStore *m_stateStore;
    // One runner per scripts object: operation keys stay module-private.
    core::ScriptRunner *m_runner;

    // Accumulated console output per script run key (live display).
    QHash<QString, QString> m_buffers;
    // Start time per run key, for the "done, exit=0, 1.2s" log line.
    QHash<QString, qint64> m_startMs;
    // The setup command text, kept until its run finishes so the failure
    // message can quote it.
    QHash<QString, QString> m_setupCommands;
    // id -> version-check epoch, invalidating delayed spinner clears.
    QHash<QString, int> m_versionEpoch;
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTSCRIPTS_H
