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

namespace awb::agentcatalog {

class AgentHealthMonitor;
class AgentModel;
class AgentRepository;
class AgentRuntime;
class AgentScripts;
class AgentStateStore;

// The QML facade for the agents feature: it
// aggregates repository, model, runtime, scripts and health monitor, and
// keeps the Q_INVOKABLE/signature names of the 0.3.0 `launcher` object so
// the existing QML only needs its prefix renamed (`launcher.` -> `agents.`).
//
// openWeb is NOT a facade method: opening the web UI is the
// cross-domain workbench intent `workbench.openWeb(id)`. openConfigDir
// stays — WorkbenchContext delegates to it. Python/Node detection lives
// in EnvironmentService.
class AgentsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)

public:
    // `theme` supplies the agent auto-assignment palette;
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

    // The authenticated session URL captured from this agent's launch output
    // (token-gated harnesses such as dsh print one per process). Empty when
    // none was captured — openWeb then falls back to the configured webUrl.
    Q_INVOKABLE QString sessionUrl(const QString &id) const;

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

    // Health transition relay (BuiltinPages wires it to the web tabs —
    void runningChanged(const QString &id, bool running);
    // An agent was deleted from the configuration (BuiltinPages closes its
    // tabs).
    void agentRemoved(const QString &id);
    // The launch captured the agent's authenticated session URL (dsh-style
    // per-process token). BuiltinPages retargets an open tab at it.
    void sessionUrlChanged(const QString &id, const QString &url);

private:
    bool saveConfig();

    AgentRepository *m_repo;
    AgentStateStore *m_stateStore;
    AgentModel *m_model;
    AgentRuntime *m_runtime;
    AgentScripts *m_scripts;
    AgentHealthMonitor *m_health;
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTSFACADE_H
