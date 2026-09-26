#ifndef AWB_AGENTS_AGENTSFACADE_H
#define AWB_AGENTS_AGENTSFACADE_H

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::theme {
class Theme;
} // namespace awb::theme

namespace awb::agents {

class AgentHealthMonitor;
class AgentModel;
class AgentRepository;
class AgentRuntime;
class AgentScripts;
class AgentStateStore;

// The QML facade for the agents feature (01-architecture.md §4.3): it
// aggregates repository, model, runtime, scripts and health monitor, and
// keeps the Q_INVOKABLE/signature names of the 0.3.0 `launcher` object so
// the existing QML only needs its prefix renamed (`launcher.` -> `agents.`).
//
// Interim members that move out in S4: openWeb/openConfigDir (called through
// workbench now); Python/Node detection moved to EnvironmentService.
class AgentsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)

public:
    // `theme` supplies the agent auto-assignment palette (specs/01 §4.3);
    // nullptr uses the built-in palette (unit tests).
    AgentsFacade(core::Settings *settings, const QString &dataRoot,
                 theme::Theme *theme = nullptr, QObject *parent = nullptr);

    QAbstractItemModel *model() const;
    // Typed access for in-module callers and tests.
    AgentModel *agentModel() const { return m_model; }

    Q_INVOKABLE void launch(const QString &id);
    Q_INVOKABLE bool stop(const QString &id);
    // Force-stop: kill the process listening on the agent's web port, even
    // when the launcher didn't start it (no tracked PID).
    Q_INVOKABLE void forceStop(const QString &id);
    Q_INVOKABLE void openWeb(const QString &id);
    Q_INVOKABLE void openConfigDir(const QString &id);
    Q_INVOKABLE void install(const QString &id);
    Q_INVOKABLE void updateTool(const QString &id);
    Q_INVOKABLE void resetSetup(const QString &id);

    // True if at least one agent was started from the launcher this session.
    Q_INVOKABLE bool hasLaunchedAgents() const;

    // Terminate every process this launcher started this session. Returns
    // the number of process trees successfully killed.
    Q_INVOKABLE int stopAll();

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

    // Load state, apply it to the model, then start the health poll, the
    // version checks and the runtime detection.
    void start();

signals:
    // Emitted when a launch/stop attempt fails. The UI shows an at-place
    // flash on the matching card plus a detailed popup.
    void launchFailed(const QString &id, const QString &message);

    // Emitted when an install/update finishes (success or failure).
    void installFinished(const QString &id, bool success, const QString &message);

private:
    bool saveConfig();

    AgentRepository *m_repo;
    AgentStateStore *m_stateStore;
    AgentModel *m_model;
    AgentRuntime *m_runtime;
    AgentScripts *m_scripts;
    AgentHealthMonitor *m_health;
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTSFACADE_H
