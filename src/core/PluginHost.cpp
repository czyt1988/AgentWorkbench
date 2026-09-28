#include "core/PluginHost.h"

#include "core/Paths.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QJsonObject>

namespace awb::core {

namespace {

// Parse one plugin.json; invalid manifests return an invalid Manifest
// (empty id) — the caller logs and skips.
PluginHost::Manifest parseManifest(const QString &path)
{
    PluginHost::Manifest manifest;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral(
            "PluginHost: cannot read %1: %2").arg(path, file.errorString());
        return manifest;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning().noquote() << QStringLiteral(
            "PluginHost: %1 is not a JSON object; skipping it").arg(path);
        return manifest;
    }
    const QJsonObject root = doc.object();
    manifest.id = root.value(QStringLiteral("id")).toString();
    manifest.name = root.value(QStringLiteral("name")).toString();
    manifest.version = root.value(QStringLiteral("version")).toString();
    manifest.apiVersion = root.value(QStringLiteral("apiVersion")).toInt();
    manifest.description = root.value(QStringLiteral("description")).toString();
    manifest.author = root.value(QStringLiteral("author")).toString();
    manifest.entry = root.value(QStringLiteral("entry")).toString();
    manifest.dir = QFileInfo(path).absolutePath();

    if (manifest.id.isEmpty() || manifest.entry.isEmpty()) {
        qWarning().noquote() << QStringLiteral(
            "PluginHost: %1 is missing id/entry; skipping it").arg(path);
        return PluginHost::Manifest();
    }

    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    for (const QJsonValue &value : pages) {
        const QJsonObject o = value.toObject();
        plugin::PageDescriptor page;
        page.id = o.value(QStringLiteral("id")).toString();
        page.title = o.value(QStringLiteral("title")).toString();
        page.icon = o.value(QStringLiteral("icon")).toString();
        page.source = o.value(QStringLiteral("source")).toString();
        page.section = o.value(QStringLiteral("section"))
                           .toString(QStringLiteral("extensions"));
        page.order = o.value(QStringLiteral("order")).toInt(50);
        if (!page.id.isEmpty() && !page.source.isEmpty())
            manifest.pages.append(page);
    }
    return manifest;
}

} // namespace

PluginHost::PluginHost(QObject *parent)
    : QObject(parent)
{
}

QList<PluginHost::Manifest> PluginHost::discover() const
{
    QList<Manifest> manifests;
    const QDir root(Paths::pluginsDir());
    if (!root.exists())
        return manifests;

    const QFileInfoList dirs = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &dir : dirs) {
        const QString manifestPath =
            dir.absoluteFilePath() + QStringLiteral("/plugin.json");
        if (!QFile::exists(manifestPath))
            continue;
        Manifest manifest = parseManifest(manifestPath);
        if (!manifest.id.isEmpty())
            manifests.append(manifest);
    }
    return manifests;
}

int PluginHost::loadEnabled(const QList<Manifest> &manifests,
                            plugin::Services *services)
{
    if (!services)
        return 0;

    int loadedCount = 0;
    for (const Manifest &manifest : manifests) {
        if (!manifest.enabled)
            continue;

        if (manifest.apiVersion != plugin::ApiVersion) {
            qWarning().noquote() << QStringLiteral(
                "PluginHost: plugin \"%1\" targets API version %2, the host "
                "provides %3 — refusing to load it")
                .arg(manifest.id)
                .arg(manifest.apiVersion)
                .arg(plugin::ApiVersion);
            continue;
        }

        const QString libraryPath =
            manifest.dir + QLatin1Char('/') + manifest.entry;
        auto *library = new QLibrary(libraryPath, this);
        if (!library->load()) {
            qWarning().noquote() << QStringLiteral(
                "PluginHost: cannot load %1: %2 — the plugin is ignored")
                .arg(libraryPath, library->errorString());
            delete library;
            continue;
        }

        // Re-check the version through the actual binary: a stale manifest
        // must not smuggle an incompatible implementation past us.
        using ApiVersionFn = int (*)();
        auto versionFn = reinterpret_cast<ApiVersionFn>(
            library->resolve("awb_plugin_api_version"));
        using RegisterFn = int (*)(plugin::Services *);
        auto registerFn = reinterpret_cast<RegisterFn>(
            library->resolve("awb_plugin_register"));

        if (!versionFn || !registerFn) {
            qWarning().noquote() << QStringLiteral(
                "PluginHost: %1 does not export the awb_plugin_* entry "
                "points — the plugin is ignored").arg(libraryPath);
            library->unload();
            delete library;
            continue;
        }
        if (versionFn() != plugin::ApiVersion) {
            qWarning().noquote() << QStringLiteral(
                "PluginHost: %1 was built against API version %2, the host "
                "provides %3 — refusing it")
                .arg(libraryPath).arg(versionFn()).arg(plugin::ApiVersion);
            library->unload();
            delete library;
            continue;
        }

        const int result = registerFn(services);
        if (result != 0) {
            qWarning().noquote() << QStringLiteral(
                "PluginHost: awb_plugin_register of \"%1\" returned %2 — "
                "the plugin is ignored").arg(manifest.id).arg(result);
            library->unload();
            delete library;
            continue;
        }

        m_loaded.append(library);
        ++loadedCount;
        qInfo().noquote() << QStringLiteral(
            "PluginHost: loaded plugin \"%1\" %2").arg(manifest.id,
                                                       manifest.version);
    }
    return loadedCount;
}

void PluginHost::shutdown()
{
    for (QLibrary *library : m_loaded)
        library->unload();
    m_loaded.clear();
}

} // namespace awb::core
