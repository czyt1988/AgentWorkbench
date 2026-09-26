#include "AgentConfig.h"

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
    QString defaultTitle;
    loadDefaults(&defaultTitle);

    m_agents.clear();
    m_removedIds.clear();
    m_title.clear();

    QByteArray data;
    {
        QFile file(configFilePath());
        if (file.open(QIODevice::ReadOnly))
            data = file.readAll();
    }
    if (!data.isEmpty())
        m_agents = parse(data, m_title);

    // An empty root title means "use the shipped one".
    if (m_title.isEmpty())
        m_title = defaultTitle;

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
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    // A config that never diverged from the shipped default is written as the
    // bundled file byte for byte: with no user-added agents and no deletions,
    // ~/.AgentLauncher/agents.json stays an exact copy of
    // config/default_agents.json, which keeps the two diffable while working
    // on the default launcher list.
    QString defaultTitle;
    const QList<Agent> defaults = loadDefaults(&defaultTitle);
    if (m_removedIds.isEmpty() && m_title == defaultTitle
        && agentsArray(m_agents) == agentsArray(defaults)) {
        QFile bundled(QStringLiteral(":/config/default_agents.json"));
        if (bundled.open(QIODevice::ReadOnly))
            return file.write(bundled.readAll()) > 0;
    }

    QJsonObject root;
    root[QStringLiteral("title")] = m_title;
    root[QStringLiteral("agents")] = agentsArray(m_agents);
    if (!m_removedIds.isEmpty()) {
        QJsonArray removed;
        for (const QString &id : m_removedIds)
            removed.append(id);
        root[QStringLiteral("removed")] = removed;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

QString AgentConfig::userDataDir()
{
    // QStandardPaths test mode only redirects the App* locations (see
    // QStandardPaths::setTestModeEnabled), never HomeLocation, so unit tests
    // would otherwise read and rewrite the developer's real config. Keep them
    // on the redirected location.
    if (QStandardPaths::isTestModeEnabled())
        return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
           + QStringLiteral("/.AgentLauncher");
}

QString AgentConfig::configFilePath()
{
    return userDataDir() + QStringLiteral("/agents.json");
}

QList<Agent> AgentConfig::parse(const QByteArray &data, QString &outTitle)
{
    QList<Agent> result;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();
    outTitle = root.value(QStringLiteral("title")).toString();
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
    if (raw.isEmpty())
        return QStringLiteral("qrc:/icons/default.svg");

    // Built-in resources, remote URLs and file URLs are used as-is.
    if (raw.startsWith(QStringLiteral("qrc:/"))
        || raw.startsWith(QStringLiteral("http://"))
        || raw.startsWith(QStringLiteral("https://"))
        || raw.startsWith(QStringLiteral("file://")))
        return raw;

    // Treat anything else as a local file path. Expand environment variables
    // and ~ so users can write e.g. "%USERPROFILE%/icons/my-agent.svg".
    const QString expanded = expandEnv(raw);
    const QFileInfo fi(expanded);
    if (fi.exists())
        return QUrl::fromLocalFile(fi.absoluteFilePath()).toString();

    // File not found — fall back to the default icon rather than showing
    // nothing.
    return QStringLiteral("qrc:/icons/default.svg");
}

QString AgentConfig::expandEnv(const QString &path)
{
    QString result = path;
    // Expand %VAR% style variables (Windows), e.g. %USERPROFILE%.
    static const QRegularExpression re(QStringLiteral("%(\\w+)%"));
    QRegularExpressionMatchIterator it = re.globalMatch(result);
    QString out;
    int cursor = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += result.mid(cursor, m.capturedStart() - cursor);
        const QString var = m.captured(1);
        const QString val = qEnvironmentVariable(qUtf8Printable(var));
        out += val.isEmpty() ? m.captured(0) : val;
        cursor = m.capturedEnd();
    }
    out += result.mid(cursor);
    // Expand ~ to the home directory (unix style, convenience).
    out.replace(QStringLiteral("~/"),
                QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/"));
    return out;
}

// --- Default config helpers ---------------------------------------------------

QList<Agent> AgentConfig::loadDefaults(QString *outTitle)
{
    QFile def(QStringLiteral(":/config/default_agents.json"));
    if (!def.open(QIODevice::ReadOnly)) {
        if (outTitle)
            outTitle->clear();
        return {};
    }
    QString title;
    const QList<Agent> agents = AgentConfig().parse(def.readAll(), title);
    if (outTitle)
        *outTitle = title;
    return agents;
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
