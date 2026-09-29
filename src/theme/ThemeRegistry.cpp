#include "theme/ThemeRegistry.h"

#include "core/Paths.h"
#include "theme/ThemeLoader.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <utility>

namespace awb::theme {

namespace {

const QStringList kBuiltinIds = {QStringLiteral("mocha-dark"),
                                 QStringLiteral("latte-light")};

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral(
            "ThemeRegistry: cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning().noquote() << QStringLiteral(
            "ThemeRegistry: %1 is not a valid JSON object; skipping it")
            .arg(path);
        return {};
    }
    return doc.object();
}

} // namespace

ThemeRegistry::ThemeRegistry(QObject *parent)
    : QObject(parent)
{
    // Built-ins first: they are complete by construction, so they parse
    // with an invalid baseline (no fallback, no unknown-key filtering
    // beyond the top-level list).
    for (const QString &id : kBuiltinIds) {
        const QString path = QStringLiteral(":/themes/") + id
                             + QStringLiteral(".json");
        ThemeFile file = ThemeLoader::loadFile(path, ThemeFile());
        if (file.isValid()) {
            m_builtins.insert(id, file);
            m_themes.insert(id, file);
            m_sources.insert(id, path);
        }
    }

    // The user theme directory is watched for hot reload, so create it
    // eagerly (empty = "no user themes yet").
    QDir().mkpath(core::Paths::themesDir());

    scan();
    armWatchers();

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this]() { refresh(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &) { refresh(); });
}

QList<ThemeFile> ThemeRegistry::themes() const
{
    // Built-ins in their fixed order first, then user-only themes.
    QList<ThemeFile> result;
    QSet<QString> seen;
    for (const QString &id : kBuiltinIds) {
        if (m_themes.contains(id)) {
            result.append(m_themes.value(id));
            seen.insert(id);
        }
    }
    QStringList extra = m_themes.keys();
    extra.removeAll(QStringLiteral("mocha-dark"));
    extra.removeAll(QStringLiteral("latte-light"));
    extra.sort(Qt::CaseInsensitive);
    for (const QString &id : std::as_const(extra)) {
        if (!seen.contains(id)) {
            result.append(m_themes.value(id));
        }
    }
    return result;
}

ThemeFile ThemeRegistry::theme(const QString &id) const
{
    return m_themes.value(id);
}

ThemeFile ThemeRegistry::baseline(const QString &variant) const
{
    if (variant == QStringLiteral("dark")) {
        return m_builtins.value(QStringLiteral("mocha-dark"));
    }
    if (variant == QStringLiteral("light")) {
        return m_builtins.value(QStringLiteral("latte-light"));
    }
    return {};
}

void ThemeRegistry::refresh()
{
    scan();
    armWatchers();
    Q_EMIT changed();
}

void ThemeRegistry::scan()
{
    m_themes = m_builtins;
    m_sources.clear();
    for (const QString &id : kBuiltinIds) {
        m_sources.insert(id, QStringLiteral(":/themes/") + id
                                 + QStringLiteral(".json"));
    }

    const QString userDir = core::Paths::themesDir();
    const QDir dir(userDir);
    const QStringList files = dir.entryList({QStringLiteral("*.json")},
                                            QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        const QString path = dir.filePath(fileName);
        const QJsonObject json = readJson(path);
        if (json.isEmpty()) {
            continue;
        }
        const QString variant =
            json.value(QStringLiteral("variant")).toString();
        ThemeFile file;
        if (!ThemeLoader::parse(json, fileName.section(QLatin1Char('.'), 0, 0),
                                baseline(variant), file)) {
            continue;
        }
        // Same id overrides the built-in.
        m_themes.insert(file.id, file);
        m_sources.insert(file.id, path);
    }
}

void ThemeRegistry::armWatchers()
{
    // Re-arm from scratch: rescanned files may be new, replaced or gone.
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }

    const QString userDir = core::Paths::themesDir();
    m_watcher.addPath(userDir);

    QSet<QString> userFiles;
    for (auto it = m_sources.constBegin(); it != m_sources.constEnd(); ++it) {
        if (it.value().startsWith(userDir)) {
            userFiles.insert(it.value());
        }
    }
    if (!userFiles.isEmpty()) {
        m_watcher.addPaths(userFiles.values());
    }
}

} // namespace awb::theme
