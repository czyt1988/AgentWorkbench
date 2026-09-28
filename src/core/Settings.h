#ifndef AWB_CORE_SETTINGS_H
#define AWB_CORE_SETTINGS_H

#include "core/OpResult.h"

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace awb::core {

// settings.json value types. Missing keys take these defaults, unknown
// keys are ignored with a warning, and
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
    // UI 全局字体族：主题未覆盖时的应用字体。默认微软雅黑（Win10 的
    // 宋体回退太丑）；本机没有该字体时 QFont 的族匹配自动回退系统默认。
    // 空串 = 跟随主题/系统默认。
    QString fontFamily = QStringLiteral("Microsoft YaHei");
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
    // Off by default: Chromium already throttles hidden views (no rAF,
    // slowed timers), while Frozen additionally suspends JS/websockets —
    // agent WebUIs visibly "refresh" when resumed on tab switch. The LRU
    // release (maxLiveTabs) still bounds memory with this off.
    bool freezeInactiveTabs = false;
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
    // 最低落盘级别：debug/info/warning/critical/off，取值由 Logging 校验
    QString level = QStringLiteral("debug");
    // 是否把日志同时镜像到 stderr（控制台调试用）
    bool mirrorToStderr = true;
};

struct PluginsSettings
{
    bool enabled = false;
    QStringList disabledIds;
};

// Typed access layer for settings.json.
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

    // Spec-named shortcuts.
    QString themeId() const;
    QString fontFamily() const;
    QString windowTitle() const;
    QJsonArray skillsRoots() const;

    // Setters used by the UI. Each emits valueChanged(key); persistence is
    // explicit via save().
    void setWindowTitle(const QString &title);
    void setThemeId(const QString &id);
    void setFollowSystem(bool on);
    void setFontFamily(const QString &family);
    void setWindowSize(int width, int height);
    void setSidebarCollapsed(bool collapsed);
    void setLastPageId(const QString &pageId);
    void setWebSurface(const QString &surface);
    void setWebChromiumFlags(const QString &flags);
    void setSkillRoots(const QJsonArray &roots);
    void setPluginsDisabledIds(const QStringList &ids);
    void setPluginsGloballyEnabled(bool enabled);

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
