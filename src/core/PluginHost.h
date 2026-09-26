#ifndef AWB_CORE_PLUGINHOST_H
#define AWB_CORE_PLUGINHOST_H

#include "plugin_api/PluginApi.h"

#include <QList>
#include <QObject>
#include <QString>

class QLibrary;

namespace awb::core {

// Plugin discovery and loading (01-architecture.md §4.1, §9). Scans
// <dataRoot>/plugins/*/plugin.json, validates the API version, and loads
// the entry dynamic library through the two exported C symbols.
//
// Safety rules (§9.3): a broken manifest, a version mismatch or a failed
// load is LOGGED and skipped — a plugin can never keep the app from
// starting. Everything runs in the host process; there is no sandbox, so
// plugins are disabled by default and users opt in per plugin.
class PluginHost : public QObject
{
    Q_OBJECT

public:
    // A discovered (manifest-only) plugin, for the settings list.
    struct Manifest
    {
        QString id;
        QString name;
        QString version;
        int apiVersion = 0;
        QString description;
        QString author;
        QString entry; // dynamic library file name inside the plugin dir
        QString dir;   // <dataRoot>/plugins/<id>
        QList<plugin::PageDescriptor> pages;
        bool enabled = false; // resolved against settings at load time
    };

    explicit PluginHost(QObject *parent = nullptr);

    // Scan pluginsDir for manifests (never loads a library).
    QList<Manifest> discover() const;

    // Load every enabled plugin. `services` is the host-side bridge
    // (implemented in awb_workbench); failures are logged, never thrown.
    // Returns the number of plugins that registered successfully.
    int loadEnabled(const QList<Manifest> &manifests,
                    plugin::Services *services);

    // Keep the libraries alive for the process lifetime.
    void shutdown();

private:
    QList<QLibrary *> m_loaded;
};

} // namespace awb::core

#endif // AWB_CORE_PLUGINHOST_H
