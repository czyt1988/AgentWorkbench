#ifndef AWB_CORE_SETTINGS_H
#define AWB_CORE_SETTINGS_H

#include "core/OpResult.h"

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace awb::core {

// settings.json value types. Defaults come from 01-architecture.md §7.2;
// missing keys take them, unknown keys are ignored with a warning, and
// there is deliberately no migration code — add a key, add its default.
struct WindowSettings
{
    QString title; // empty = application default "AgentWorkbench"
    int width = 1440;
    int height = 900;
    int sidebarWidth = 240;
    bool sidebarCollapsed = false;
    QString lastPageId = QStringLiteral("agents");
};

struct AppearanceSettings
{
    QString theme = QStringLiteral("mocha-dark");
    bool followSystem = false;
};

struct LocaleSettings
{
    QString overrideName; // empty = follow the system locale
};

struct LauncherSettings
{
    int healthCheckIntervalMs = 3000;
    bool startupVersionCheck = true;
};

struct WebSettings
{
    QString surface = QStringLiteral("embedded"); // embedded | external
    bool freezeInactiveTabs = true;
    int maxLiveTabs = 8;
    QString downloadDir; // empty = platform default (~/Downloads)
    QString chromiumFlags;
    QString homeUrl;
};

struct SkillsSettings
{
    QJsonArray roots; // empty = built-in default root list
    bool includePluginCaches = true;
    int maxDepth = 6;
};

struct LoggingSettings
{
    qint64 maxFileSize = 5 * 1024 * 1024;
    int maxFiles = 3;
};

struct PluginsSettings
{
    bool enabled = false;
    QStringList disabledIds;
};

// Typed access layer for settings.json (01-architecture.md §4.1/§7.2).
// The application must not read settings.json anywhere else.
class Settings : public QObject
{
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr);

    // <dataRoot>/settings.json.
    static QString settingsFilePath();

    const WindowSettings &window() const { return m_window; }
    const AppearanceSettings &appearance() const { return m_appearance; }
    const LocaleSettings &locale() const { return m_locale; }
    const LauncherSettings &launcherOptions() const { return m_launcher; }
    const WebSettings &webOptions() const { return m_web; }
    const SkillsSettings &skillsOptions() const { return m_skills; }
    const LoggingSettings &loggingOptions() const { return m_logging; }
    const PluginsSettings &pluginsOptions() const { return m_plugins; }

    // Spec-named shortcuts (01-architecture.md §4.1).
    QString themeId() const;
    QString windowTitle() const;
    QJsonArray skillsRoots() const;

    // Setters used by the UI. Each emits valueChanged(key); persistence is
    // explicit via save().
    void setWindowTitle(const QString &title);
    void setThemeId(const QString &id);
    void setFollowSystem(bool on);
    void setWindowSize(int width, int height);
    void setSidebarCollapsed(bool collapsed);
    void setLastPageId(const QString &pageId);
    void setWebSurface(const QString &surface);
    void setWebChromiumFlags(const QString &flags);

    // Persist the whole settings object (atomic write).
    OpResult save();

signals:
    // Emitted by setters with a dotted key, e.g. "appearance.theme".
    void valueChanged(const QString &key);

private:
    void load();

    WindowSettings m_window;
    AppearanceSettings m_appearance;
    LocaleSettings m_locale;
    LauncherSettings m_launcher;
    WebSettings m_web;
    SkillsSettings m_skills;
    LoggingSettings m_logging;
    PluginsSettings m_plugins;
};

} // namespace awb::core

#endif // AWB_CORE_SETTINGS_H
