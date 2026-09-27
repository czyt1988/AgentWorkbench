#ifndef AWB_AGENTS_AGENTSTATESTORE_H
#define AWB_AGENTS_AGENTSTATESTORE_H

#include <QHash>
#include <QString>

namespace awb::agents {

// agent_state.json: which agents have completed their one-time setup.
// Loaded once, written atomically via core::JsonStore.
class AgentStateStore
{
public:
    explicit AgentStateStore(const QString &dataRoot);

    // Read the file once; a missing or broken file means "no setup done".
    void load();

    bool isSetupDone(const QString &id) const;

    // Record the setup as done; false when the file cannot be written (the
    // caller reports that the setup will run again on the next start).
    bool markSetupDone(const QString &id);

    // Forget the record so the setup command runs again before the next
    // launch. False when there is nothing to write.
    bool reset(const QString &id);

    QString stateFilePath() const;

private:
    QString m_dataRoot;
    QHash<QString, bool> m_setupDone;
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTSTATESTORE_H
