#ifndef AWB_AGENTS_AGENTMODEL_H
#define AWB_AGENTS_AGENTMODEL_H

#include "agents/AgentDefinition.h"
#include "agents/AgentState.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>

namespace awb::agents {

// List model over agents: the persisted definitions plus a per-id runtime
// state map. Role names and order are byte-compatible with 0.3.0 so the
// card QML keeps working unchanged.
class AgentModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        CommandRole,
        WebUrlRole,
        ConfigDirRole,
        IconRole,
        ColorRole,
        CardColorRole,
        RunningRole,
        LaunchingRole,
        InstallCommandRole,
        UpdateCommandRole,
        VersionCommandRole,
        SetupCommandRole,
        InstalledRole,
        VersionRole,
        InstallingRole,
        SetupDoneRole,
        SetuppingRole,
        CheckingVersionRole,
        ConsoleOutputRole
    };
    Q_ENUM(Roles)

    explicit AgentModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replace all definitions. Existing runtime state for ids that survive
    // the swap is kept, so callers never blank a running card.
    void setDefinitions(const QList<AgentDefinition> &definitions);
    const QList<AgentDefinition> &definitions() const { return m_definitions; }

    // Replace the definition at its existing row (by id) and refresh the
    // whole row. Runtime state is untouched — it is keyed separately.
    bool replaceDefinition(const AgentDefinition &definition);

    // Insert a definition at the given row (out-of-range rows append).
    void insertAgent(int row, const AgentDefinition &definition);

    // Remove the agent with the given id. Returns false when not found.
    bool removeAgentById(const QString &id);

    Q_INVOKABLE int indexOf(const QString &id) const;
    // Definitions + runtime state merged into one map (edit form input).
    Q_INVOKABLE QVariantMap agent(const QString &id) const;

    AgentState state(const QString &id) const { return m_states.value(id); }

public slots:
    void setRunning(const QString &id, bool running);
    void setLaunching(const QString &id, bool launching);
    void setInstalled(const QString &id, bool installed);
    void setVersion(const QString &id, const QString &version);
    void setInstalling(const QString &id, bool installing);
    void setSetupDone(const QString &id, bool done);
    void setSetupping(const QString &id, bool setupping);
    void setCheckingVersion(const QString &id, bool checking);
    void setConsoleOutput(const QString &id, const QString &text);

private:
    QList<AgentDefinition> m_definitions;
    QHash<QString, AgentState> m_states;
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTMODEL_H
