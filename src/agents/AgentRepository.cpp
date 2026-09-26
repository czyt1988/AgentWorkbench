#include "agents/AgentRepository.h"

#include "core/IconResolver.h"
#include "core/JsonStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace awb::agents {

namespace {

// Persisted form of one definition — the single place that decides which
// fields reach agents.json.
QJsonObject definitionObject(const AgentDefinition &a)
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

QJsonArray definitionsArray(const QList<AgentDefinition> &agents)
{
    QJsonArray arr;
    for (const AgentDefinition &a : agents)
        arr.append(definitionObject(a));
    return arr;
}

} // namespace

AgentRepository::AgentRepository(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
}

QString AgentRepository::configFilePath() const
{
    return m_dataRoot + QStringLiteral("/agents.json");
}

void AgentRepository::load()
{
    m_definitions.clear();
    m_removedIds.clear();

    QByteArray data;
    {
        QFile file(configFilePath());
        if (file.open(QIODevice::ReadOnly))
            data = file.readAll();
    }
    if (!data.isEmpty()) {
        m_definitions = parse(data);

        //0.4.0: the root "title" field no longer drives the window title —
        // that lives in settings.json now. Hint users who still have one,
        // but only once a settings file exists; before that there is
        // nowhere to move it to (03-migration-plan.md S0-T6).
        const QString legacyTitle = QJsonDocument::fromJson(data)
                                        .object()
                                        .value(QStringLiteral("title"))
                                        .toString();
        if (!legacyTitle.isEmpty()
            && QFile::exists(m_dataRoot + QStringLiteral("/settings.json"))) {
            qInfo().noquote() << QStringLiteral(
                "agents.json: the root \"title\" field is ignored; set the "
                "window title in Settings (settings.json window.title) "
                "instead.");
        }
    }

    // Built-in agents always come from the bundled default, so the on-disk
    // file only decides which of them the user deleted, plus the agents the
    // user added on top.
    const QList<AgentDefinition> synced =
        withBuiltinDefaults(m_definitions, m_removedIds);
    const bool changed = definitionsArray(synced) != definitionsArray(m_definitions);
    m_definitions = synced;

    // Give agents the user added without a color one, so the card renders;
    // either change is persisted, which keeps the on-disk file matching what
    // the UI shows.
    const bool colorsAssigned = assignPaletteColors();
    if (changed || colorsAssigned)
        save();
}

bool AgentRepository::save()
{
    const QString path = configFilePath();

    // A config that never diverged from the shipped default is written as the
    // bundled file byte for byte: with no user-added agents and no deletions,
    // <dataRoot>/agents.json stays an exact copy of
    // config/default_agents.json, which keeps the two diffable while working
    // on the default launcher list.
    const QList<AgentDefinition> defaults = loadDefaults();
    if (m_removedIds.isEmpty()
        && definitionsArray(m_definitions) == definitionsArray(defaults)) {
        QFile bundled(QStringLiteral(":/config/default_agents.json"));
        if (bundled.open(QIODevice::ReadOnly))
            return core::JsonStore::writeBytes(path, bundled.readAll()).ok;
    }

    QJsonObject root;
    root[QStringLiteral("agents")] = definitionsArray(m_definitions);
    if (!m_removedIds.isEmpty()) {
        QJsonArray removed;
        for (const QString &id : m_removedIds)
            removed.append(id);
        root[QStringLiteral("removed")] = removed;
    }

    // Atomic, consistently indented (core::JsonStore is the only writer of
    // configuration files).
    return core::JsonStore::writeFile(path, root).ok;
}

bool AgentRepository::restoreDefaults(const QList<AgentDefinition> &current)
{
    m_removedIds.clear();
    m_definitions = withBuiltinDefaults(current, QStringList());
    return save();
}

bool AgentRepository::isDefaultAgent(const QString &id) const
{
    return defaultAgentIds().contains(id);
}

QList<AgentDefinition> AgentRepository::parse(const QByteArray &data)
{
    QList<AgentDefinition> result;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();
    const QJsonArray removed = root.value(QStringLiteral("removed")).toArray();
    for (const QJsonValue &v : removed)
        m_removedIds.append(v.toString());
    const QJsonArray arr = root.value(QStringLiteral("agents")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        AgentDefinition a;
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

QList<AgentDefinition> AgentRepository::withBuiltinDefaults(
    const QList<AgentDefinition> &current, const QStringList &removedIds)
{
    const QList<AgentDefinition> defaults = loadDefaults();

    QList<AgentDefinition> result;
    result.reserve(defaults.size() + current.size());
    for (const AgentDefinition &def : defaults) {
        // Deleted in the Settings page — stays deleted.
        if (removedIds.contains(def.id))
            continue;
        result.append(def);
    }

    // Whatever the user added on top keeps its own definition and order.
    for (const AgentDefinition &a : current) {
        const bool builtin = std::any_of(
            defaults.cbegin(), defaults.cend(),
            [&](const AgentDefinition &def) { return def.id == a.id; });
        if (!builtin)
            result.append(a);
    }
    return result;
}

// --- Palette color assignment -----------------------------------------------

bool AgentRepository::assignPaletteColors()
{
    bool changed = false;
    for (int i = 0; i < m_definitions.size(); ++i) {
        if (m_definitions[i].color.isEmpty()) {
            // Prefer the current theme's agentPalette; fall back to the
            // built-in Mocha array (specs/03 S3-T1).
            m_definitions[i].color = m_agentPalette.isEmpty()
                ? paletteColorAt(i)
                : m_agentPalette.at(((i % m_agentPalette.size())
                                     + m_agentPalette.size())
                                    % m_agentPalette.size());
            changed = true;
        }
    }
    return changed;
}

QString AgentRepository::paletteColorAt(int index)
{
    // Catppuccin Mocha palette — vibrant colors that read well on the dark
    // card background (#313244). S3 replaces this with the current theme's
    // agentPalette (specs/03 S2-T2).
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

QString AgentRepository::resolveIcon(const QString &raw)
{
    // The application-level fallback lives here; core never hardcodes an
    // app resource path (01-architecture.md §4.1).
    return core::IconResolver::resolve(raw,
                                       QStringLiteral("qrc:/icons/default.svg"));
}

// --- Default config helpers ---------------------------------------------------

QList<AgentDefinition> AgentRepository::loadDefaults()
{
    QFile def(QStringLiteral(":/config/default_agents.json"));
    if (!def.open(QIODevice::ReadOnly))
        return {};
    return AgentRepository(QString()).parse(def.readAll());
}

QStringList AgentRepository::defaultAgentIds()
{
    QStringList ids;
    const QList<AgentDefinition> defaults = loadDefaults();
    for (const AgentDefinition &a : defaults)
        ids.append(a.id);
    return ids;
}

QString AgentRepository::slugFromName(const QString &name)
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

} // namespace awb::agents
