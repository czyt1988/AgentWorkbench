#include "AgentConfig.h"

#include <QDebug>
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

void AgentConfig::load()
{
    // Older builds kept the config under AppConfigLocation; bring those files
    // across before anything reads or seeds the new location.
    migrateLegacyUserData();

    const QString path = configFilePath();
    QFile file(path);

    // Seed the user config from the bundled default on first run.
    if (!file.exists()) {
        QFile seed(QStringLiteral(":/config/default_agents.json"));
        if (seed.open(QIODevice::ReadOnly)) {
            const QByteArray data = seed.readAll();
            seed.close();

            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile out(path);
            if (out.open(QIODevice::WriteOnly))
                out.write(data);
        }
    }

    // Load the bundled default config for migration fallback.
    QString defaultTitle;
    const QList<Agent> defaults = loadDefaults(&defaultTitle);

    if (!file.open(QIODevice::ReadOnly)) {
        // Fall back to the bundled default directly.
        m_agents = defaults;
        m_title = defaultTitle;
        return;
    }

    m_agents = parse(file.readAll(), m_title);

    // Merge in any fields missing from an older on-disk config.
    if (!defaults.isEmpty())
        migrate(defaults, defaultTitle);

    // Assign palette colors to agents that still have no color.
    if (assignPaletteColors())
        save();
}

bool AgentConfig::save()
{
    QJsonArray arr;
    for (const Agent &a : m_agents) {
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
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("title")] = m_title;
    root[QStringLiteral("agents")] = arr;
    if (!m_removedIds.isEmpty()) {
        QJsonArray removed;
        for (const QString &id : m_removedIds)
            removed.append(id);
        root[QStringLiteral("removed")] = removed;
    }

    const QString path = configFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

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

void AgentConfig::migrateLegacyUserData(const QString &legacyDir)
{
    const QString newDir = userDataDir();
    const QString oldDir = legacyDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        : legacyDir;
    // In test mode both resolve to the same redirected directory.
    if (oldDir.isEmpty() || oldDir == newDir)
        return;

    for (const QString &name : {QStringLiteral("agents.json"),
                                QStringLiteral("agent_state.json")}) {
        const QString legacyPath = oldDir + QLatin1Char('/') + name;
        const QString newPath = newDir + QLatin1Char('/') + name;

        // Never overwrite what this build already wrote.
        if (QFile::exists(newPath) || !QFile::exists(legacyPath))
            continue;

        QDir().mkpath(newDir);
        // Copy rather than move: the old file stays behind as a safety net,
        // so downgrading to a build that reads AppConfigLocation still works.
        if (QFile::copy(legacyPath, newPath))
            qInfo() << "Migrated" << legacyPath << "->" << newPath;
        else
            qWarning() << "Could not migrate" << legacyPath << "->" << newPath;
    }
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

void AgentConfig::migrate(const QList<Agent> &defaults, const QString &defaultTitle)
{
    bool changed = false;

    // Fill in an empty on-disk title from the default.
    if (m_title.isEmpty() && !defaultTitle.isEmpty()) {
        m_title = defaultTitle;
        changed = true;
    }

    for (const Agent &def : defaults) {
        // User deleted this built-in agent — do not resurrect it.
        if (m_removedIds.contains(def.id))
            continue;
        auto it = std::find_if(m_agents.begin(), m_agents.end(),
            [&](const Agent &a) { return a.id == def.id; });
        if (it == m_agents.end()) {
            // Agent in default but not on disk — add it.
            m_agents.append(def);
            changed = true;
        } else {
            // Fill in any empty fields from the default.
            auto fill = [&](QString &field, const QString &defVal) {
                if (field.isEmpty() && !defVal.isEmpty()) {
                    field = defVal;
                    changed = true;
                }
            };
            fill(it->name, def.name);
            fill(it->command, def.command);
            fill(it->webUrl, def.webUrl);
            fill(it->configDir, def.configDir);
            fill(it->icon, def.icon);
            fill(it->color, def.color);
            fill(it->cardColor, def.cardColor);
            fill(it->installCommand, def.installCommand);
            fill(it->updateCommand, def.updateCommand);
            fill(it->versionCommand, def.versionCommand);
            fill(it->setupCommand, def.setupCommand);
            fill(it->tokenFile, def.tokenFile);
        }
    }
    if (changed)
        save();
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

bool AgentConfig::appendMissingDefaults(QList<Agent> &agents)
{
    bool changed = false;
    const QList<Agent> defaults = loadDefaults();
    for (const Agent &def : defaults) {
        const bool exists = std::any_of(agents.cbegin(), agents.cend(),
            [&](const Agent &a) { return a.id == def.id; });
        if (!exists) {
            agents.append(def);
            changed = true;
        }
    }
    return changed;
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
