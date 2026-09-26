#include "AgentConfig.h"

#include "core/IconResolver.h"
#include "core/JsonStore.h"
#include "core/OpResult.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace {

// Persisted form of one agent — the single place that decides which fields
// reach agents.json.
QJsonObject agentObject(const Agent &a)
{
    QJsonObject o;
    o[QStringLiteral("id")] = a.id;
    o[QStringLiteral("name")] = a.name;
    o[QStringLiteral("command")] = a.command;
    o[QStringLiteral("webUrl")] = a.webUrl;
    o[QStringLiteral("configDir")] = a.configDir;
    o[QStringLiteral("icon")] = a.icon;
    o[QStringLiteral("color")] = a.color;
    o[QStringLiteral("cardColor")] = a.cardColor;
    o[QStringLiteral("installCommand")] = a.installCommand;
    o[QStringLiteral("updateCommand")] = a.updateCommand;
    o[QStringLiteral("versionCommand")] = a.versionCommand;
    o[QStringLiteral("setupCommand")] = a.setupCommand;
    o[QStringLiteral("tokenFile")] = a.tokenFile;
    return o;
}

QJsonArray agentsArray(const QList<Agent> &agents)
{
    QJsonArray arr;
    for (const Agent &a : agents)
        arr.append(agentObject(a));
    return arr;
}

} // namespace

void AgentConfig::load()
{
    m_agents.clear();
    m_removedIds.clear();

    QByteArray data;
    {
        QFile file(configFilePath());
        if (file.open(QIODevice::ReadOnly))
            data = file.readAll();
    }
    if (!data.isEmpty()) {
        m_agents = parse(data);

        //0.4.0: the root "title" field no longer drives the window title —
        // that lives in settings.json now. Hint users who still have one,
        // but only once a settings file exists; before that there is
        // nowhere to move it to (03-migration-plan.md S0-T6).
        const QString legacyTitle = QJsonDocument::fromJson(data)
                                        .object()
                                        .value(QStringLiteral("title"))
                                        .toString();
        if (!legacyTitle.isEmpty()
            && QFile::exists(userDataDir() + QStringLiteral("/settings.json"))) {
            qInfo().noquote() << QStringLiteral(
                "agents.json: the root \"title\" field is ignored; set the "
                "window title in Settings (settings.json window.title) "
                "instead.");
        }
    }

    // Built-in agents always come from the bundled default, so the on-disk
    // file only decides which of them the user deleted, plus the agents the
    // user added on top.
    const QList<Agent> synced = withBuiltinDefaults(m_agents, m_removedIds);
    const bool changed = agentsArray(synced) != agentsArray(m_agents);
    m_agents = synced;

    // Give agents the user added without a color one, so the card renders;
    // either change is persisted, which keeps the on-disk file matching what
    // the UI shows.
    const bool colorsAssigned = assignPaletteColors();
    if (changed || colorsAssigned)
        save();
}

bool AgentConfig::save()
{
    const QString path = configFilePath();

    // A config that never diverged from the shipped default is written as the
    // bundled file byte for byte: with no user-added agents and no deletions,
    // ~/.AgentWorkbench/agents.json stays an exact copy of
    // config/default_agents.json, which keeps the two diffable while working
    // on the default launcher list.
    const QList<Agent> defaults = loadDefaults();
    if (m_removedIds.isEmpty() && agentsArray(m_agents) == agentsArray(defaults)) {
        QFile bundled(QStringLiteral(":/config/default_agents.json"));
        if (bundled.open(QIODevice::ReadOnly))
            return awb::core::JsonStore::writeBytes(path, bundled.readAll()).ok;
    }

    QJsonObject root;
    root[QStringLiteral("agents")] = agentsArray(m_agents);
    if (!m_removedIds.isEmpty()) {
        QJsonArray removed;
        for (const QString &id : m_removedIds)
            removed.append(id);
        root[QStringLiteral("removed")] = removed;
    }

    // Atomic, consistently indented (core::JsonStore is the only writer of
    // configuration files).
    return awb::core::JsonStore::writeFile(path, root).ok;
}

QString AgentConfig::userDataDir()
{
    // The data root lives in core::Paths (test-mode aware, single source).
    return awb::core::Paths::dataRoot();
}

QString AgentConfig::configFilePath()
{
    return userDataDir() + QStringLiteral("/agents.json");
}

QList<Agent> AgentConfig::parse(const QByteArray &data)
{
    QList<Agent> result;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();
    m_removedIds.clear();
    const QJsonArray removed = root.value(QStringLiteral("removed")).toArray();
    for (const QJsonValue &v : removed)
        m_removedIds.append(v.toString());
    const QJsonArray arr = root.value(QStringLiteral("agents")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Agent a;
        a.id = o.value(QStringLiteral("id")).toString();
        a.name = o.value(QStringLiteral("name")).toString();
        a.command = o.value(QStringLiteral("command")).toString();
        a.webUrl = o.value(QStringLiteral("webUrl")).toString();
        a.configDir = o.value(QStringLiteral("configDir")).toString();
        a.icon = o.value(QStringLiteral("icon")).toString();
        a.color = o.value(QStringLiteral("color")).toString();
        a.cardColor = o.value(QStringLiteral("cardColor")).toString();
        a.installCommand = o.value(QStringLiteral("installCommand")).toString();
        a.updateCommand = o.value(QStringLiteral("updateCommand")).toString();
        a.versionCommand = o.value(QStringLiteral("versionCommand")).toString();
        a.setupCommand = o.value(QStringLiteral("setupCommand")).toString();
        a.tokenFile = o.value(QStringLiteral("tokenFile")).toString();
        a.icon = resolveIcon(a.icon);
        result.append(a);
    }
    return result;
}

QList<Agent> AgentConfig::withBuiltinDefaults(const QList<Agent> &current,
                                              const QStringList &removedIds)
{
    const QList<Agent> defaults = loadDefaults();

    QList<Agent> result;
    result.reserve(defaults.size() + current.size());
    for (const Agent &def : defaults) {
        // Deleted in the Settings page — stays deleted.
        if (removedIds.contains(def.id))
            continue;
        result.append(def);
    }

    // Whatever the user added on top keeps its own definition and order.
    for (const Agent &a : current) {
        const bool builtin = std::any_of(defaults.cbegin(), defaults.cend(),
            [&](const Agent &def) { return def.id == a.id; });
        if (!builtin)
            result.append(a);
    }
    return result;
}

// --- Palette color assignment -----------------------------------------------

bool AgentConfig::assignPaletteColors()
{
    bool changed = false;
    for (int i = 0; i < m_agents.size(); ++i) {
        if (m_agents[i].color.isEmpty()) {
            m_agents[i].color = paletteColorAt(i);
            changed = true;
        }
    }
    return changed;
}

QString AgentConfig::paletteColorAt(int index)
{
    // Catppuccin Mocha palette — vibrant colors that read well on the dark
    // card background (#313244).
    static const QStringList palette = {
        QStringLiteral("#f38ba8"), // Red
        QStringLiteral("#fab387"), // Peach
        QStringLiteral("#f9e2af"), // Yellow
        QStringLiteral("#a6e3a1"), // Green
        QStringLiteral("#94e2d5"), // Teal
        QStringLiteral("#89b4fa"), // Blue
        QStringLiteral("#cba6f7"), // Mauve
        QStringLiteral("#f5c2e7"), // Pink
    };
    return palette.at(((index % palette.size()) + palette.size()) % palette.size());
}

// --- Icon resolution --------------------------------------------------------

QString AgentConfig::resolveIcon(const QString &raw)
{
    // The application-level fallback lives here; core never hardcodes an
    // app resource path (01-architecture.md §4.1).
    return awb::core::IconResolver::resolve(
        raw, QStringLiteral("qrc:/icons/default.svg"));
}

// --- Default config helpers ---------------------------------------------------

QList<Agent> AgentConfig::loadDefaults()
{
    QFile def(QStringLiteral(":/config/default_agents.json"));
    if (!def.open(QIODevice::ReadOnly))
        return {};
    return AgentConfig().parse(def.readAll());
}

QStringList AgentConfig::defaultAgentIds()
{
    QStringList ids;
    const QList<Agent> defaults = loadDefaults();
    for (const Agent &a : defaults)
        ids.append(a.id);
    return ids;
}

QString AgentConfig::slugFromName(const QString &name)
{
    QString s = name.toLower().trimmed();
    s.remove(QRegularExpression(QStringLiteral("[^a-z0-9\\s_-]")));
    s.replace(QRegularExpression(QStringLiteral("[\\s_]+")), QStringLiteral("-"));
    s.replace(QRegularExpression(QStringLiteral("-+")), QStringLiteral("-"));
    while (s.startsWith(QLatin1Char('-')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('-')))
        s.chop(1);
    if (s.isEmpty())
        s = QStringLiteral("agent");
    return s;
}
