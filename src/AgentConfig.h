#ifndef AGENTCONFIG_H
#define AGENTCONFIG_H

#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>

struct Agent
{
    QString id;
    QString name;
    QString command;
    QString webUrl;
    QString configDir;
    QString icon;
    QString color;
    QString cardColor; // optional card background color (non-running state)
    QString installCommand;
    QString updateCommand;
    QString versionCommand;
    QString setupCommand; // one-time setup command run before first launch
    QString tokenFile;    // path to a bearer token file (env-expanded); set as
                          // QWEN_SERVER_TOKEN on launch and appended as
                          // #token=<value> to the web URL when opening the browser
    bool running = false;
    bool launching = false; // transient UI state, never persisted
    bool installed = false;       // runtime, detected via versionCommand
    QString version;              // runtime, parsed from versionCommand output
    bool installing = false;      // transient UI state, never persisted
    bool setupDone = false;       // runtime state, persisted in agent_state.json
    bool setupping = false;       // transient UI state, never persisted
    bool checkingVersion = false; // transient UI state: version check in progress
    QString consoleOutput;        // live stdout/stderr of install/update/setup,
                                  // shown on the card so the user can see progress
};

class AgentConfig
{
public:
    void load();
    bool save();

    QList<Agent> agents() const { return m_agents; }
    void setAgents(const QList<Agent> &agents) { m_agents = agents; }

    QString title() const { return m_title; }
    void setTitle(const QString &title) { m_title = title; }

    // Ids of built-in agents the user deleted (Settings page). Persisted as
    // the root "removed" array: built-in agents always come from the shipped
    // default, so without this list a deleted launcher would be back on the
    // next start.
    QStringList removedIds() const { return m_removedIds; }
    void setRemovedIds(const QStringList &ids) { m_removedIds = ids; }

    // The bundled default agent definitions from :/config/default_agents.json.
    // outTitle, when not null, receives the default root title.
    static QList<Agent> loadDefaults(QString *outTitle = nullptr);

    // Ids of the bundled default agents.
    static QStringList defaultAgentIds();

    // Apply the shipped default to a user list: every built-in agent in the
    // bundled order with the bundled values, skipping ids listed in
    // removedIds, followed by the user's own agents in their existing order.
    // Built-in agents are therefore defined by config/default_agents.json
    // alone — an edit there reaches every install on the next start, while
    // agents the user added themselves are preserved untouched.
    static QList<Agent> withBuiltinDefaults(const QList<Agent> &current,
                                            const QStringList &removedIds);

    // Turn a display name into a config id: "Kimi Code" -> "kimi-code".
    static QString slugFromName(const QString &name);

    // Catppuccin Mocha palette color by position (auto color assignment).
    static QString paletteColorAt(int index);

    // Resolve an icon string to a displayable image URL: qrc:/, http(s)://,
    // file:// pass through; an existing local file becomes a file:/// URL;
    // anything else (including empty) falls back to qrc:/icons/default.svg.
    static QString resolveIcon(const QString &raw);

    // Directory holding the per-user data files: agents.json and
    // agent_state.json, co-located with the log directory (~/.AgentLauncher)
    // so everything the app writes lives in one place.
    static QString userDataDir();

    static QString configFilePath();

private:
    QList<Agent> m_agents;
    QString m_title;
    QStringList m_removedIds;

    QList<Agent> parse(const QByteArray &data, QString &outTitle);

    // Assign a color from the built-in palette to every agent whose `color`
    // is still empty. Colors are assigned by cycling through the palette based
    // on the agent's position in the list. Returns true if any color was
    // assigned (so the caller can persist).
    bool assignPaletteColors();

    // Expand %VAR% environment variables and ~ in a path.
    static QString expandEnv(const QString &path);
};

#endif // AGENTCONFIG_H
