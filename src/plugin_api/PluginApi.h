#ifndef AWB_PLUGIN_API_PLUGINAPI_H
#define AWB_PLUGIN_API_PLUGINAPI_H

#include <QString>
#include <QStringList>

// The plugin ABI (specs/01-architecture.md §9). Rules:
//  - only Qt types cross this boundary — never a host C++ class;
//  - every plugin exports the two extern "C" symbols below;
//  - `apiVersion` must match ApiVersion or the host refuses to load.
//
// Plugins run IN the host process (same trust level as the application
// itself — there is no sandbox; users enable them explicitly in Settings).

#if defined(WIN32) || defined(_WIN32)
#define AWB_PLUGIN_EXPORT __declspec(dllexport)
#else
#define AWB_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace awb::plugin {

// The ABI version this header describes. Bump on any breaking change.
constexpr int ApiVersion = 1;

// One sidebar page contributed by a plugin (specs/01 §9.2). Plain Qt value
// types only — no host classes across the boundary.
struct PageDescriptor
{
    QString id;
    QString title;
    QString icon;    // qrc:/… URL (the plugin's own resource)
    QString source;  // qrc:/… QML URL
    QString section; // "main" | "extensions" | "system"
    int order = 50;
};

// Host services handed to the plugin at registration time
// (specs/01 §9.3). Pure virtual: the host implements, the plugin calls.
// The same registration path serves built-in pages and plugins
// (specs/01 §9.1) — there is no separate "plugin mode".
class Services
{
public:
    virtual ~Services() = default;

    // Page registration; duplicate ids are rejected by the host (logged).
    virtual void registerPage(const PageDescriptor &page) = 0;
    virtual void unregisterPage(const QString &id) = 0;

    // Register an additional Web surface kind (component QML URL).
    virtual void addWebSurface(const QString &kind,
                               const QString &componentUrl) = 0;

    // A writable directory private to this plugin
    // (<dataRoot>/plugins/<pluginId>/data).
    virtual QString dataDir(const QString &pluginId) = 0;

    // level: 0 = info, 1 = warning, 2 = error.
    virtual void log(int level, const QString &message) = 0;

    // Show a toast (same levels as log).
    virtual void notify(int level, const QString &title,
                        const QString &text) = 0;

    // Read-only theme access: "#rrggbb" for a token, empty when unknown.
    virtual QString themeColor(const QString &token) = 0;

    // Read-only settings access (missing key -> empty).
    virtual QString settingsValue(const QString &key) = 0;
};

} // namespace awb::plugin

// --- C entry points ----------------------------------------------------------
extern "C" {
// Returns the API version the plugin was compiled against.
AWB_PLUGIN_EXPORT int awb_plugin_api_version();
// Registration entry. Return 0 on success; anything else makes the host
// log the failure and ignore the plugin.
AWB_PLUGIN_EXPORT int awb_plugin_register(awb::plugin::Services *services);
}

#endif // AWB_PLUGIN_API_PLUGINAPI_H
