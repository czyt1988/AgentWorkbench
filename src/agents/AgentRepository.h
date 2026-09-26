#ifndef AWB_AGENTS_AGENTREPOSITORY_H
#define AWB_AGENTS_AGENTREPOSITORY_H

#include "agents/AgentDefinition.h"

#include <QString>
#include <QStringList>

namespace awb::agents {

// agents.json: read/write plus the built-in sync semantics of 0.3.0
// (01-architecture.md §4.3). The data root is injected at construction —
// no global state inside the module.
class AgentRepository
{
public:
    explicit AgentRepository(const QString &dataRoot);

    // Read agents.json, re-apply the bundled defaults to the built-ins,
    // keep the `removed` list, assign palette colors to user agents without
    // one, and persist any change this produced.
    void load();

    // Write the current definitions + removal records back to
    // <dataRoot>/agents.json (atomically, via core::JsonStore).
    bool save();

    QList<AgentDefinition> definitions() const { return m_definitions; }
    void setDefinitions(const QList<AgentDefinition> &definitions)
    {
        m_definitions = definitions;
    }

    // Ids of built-in agents the user deleted (Settings page). Persisted as
    // the root "removed" array so a deleted built-in stays deleted across
    // restarts.
    QStringList removedIds() const { return m_removedIds; }
    void setRemovedIds(const QStringList &ids) { m_removedIds = ids; }

    // Forget the deletion records and re-apply the shipped built-in list on
    // top of `current` (the user's own agents keep their definitions and
    // order). Persists the result.
    bool restoreDefaults(const QList<AgentDefinition> &current);

    // True when the id belongs to the bundled default_agents.json.
    bool isDefaultAgent(const QString &id) const;

    // Path of the on-disk agents.json (shown in error messages).
    QString configFilePath() const;

    QString dataRoot() const { return m_dataRoot; }

    // The bundled default agent definitions from :/config/default_agents.json.
    static QList<AgentDefinition> loadDefaults();

    // Ids of the bundled default agents.
    static QStringList defaultAgentIds();

    // Turn a display name into a config id: "Kimi Code" -> "kimi-code".
    static QString slugFromName(const QString &name);

    // Catppuccin Mocha palette color by position (auto color assignment).
    // Until S3 the palette is the built-in array; the theme's agentPalette
    // takes over then (specs/03 S2-T2).
    static QString paletteColorAt(int index);

    // Resolve an icon string for display; the application-level fallback
    // lives here, core::IconResolver never hardcodes app resources.
    static QString resolveIcon(const QString &raw);

private:
    // Built-in sync: built-ins come from the shipped default in shipped
    // order (skipping removedIds), then the user's own agents in their
    // existing order.
    static QList<AgentDefinition> withBuiltinDefaults(
        const QList<AgentDefinition> &current, const QStringList &removedIds);

    QList<AgentDefinition> parse(const QByteArray &data);

    // Assign a palette color to every agent whose `color` is still empty.
    // Returns true if any color was assigned (so the caller can persist).
    bool assignPaletteColors();

    QString m_dataRoot;
    QList<AgentDefinition> m_definitions;
    QStringList m_removedIds;
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTREPOSITORY_H
