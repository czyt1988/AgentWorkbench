#ifndef AWB_CORE_SETTINGS_H
#define AWB_CORE_SETTINGS_H

#include "core/OpResult.h"

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace awb::core {

/// settings.json 的取值结构们：缺键取默认值，未知键记警告后忽略，
/// 刻意没有迁移代码——加一个键，就加一个默认值。
struct WindowSettings
{
    QString title;  ///< 窗口标题；空串 = 应用默认 "AgentWorkbench"
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
    /// UI 全局字体族：主题未覆盖时的应用字体。默认微软雅黑（Win10 的
    /// 宋体回退太丑）；本机没有该字体时 QFont 的族匹配自动回退系统默认。
    /// 空串 = 跟随主题/系统默认。
    QString fontFamily = QStringLiteral("Microsoft YaHei");
};

struct LocaleSettings
{
    QString overrideName;  ///< 语言覆盖；空串 = 跟随系统区域
};

struct LauncherSettings
{
    int healthCheckIntervalMs = 3000;
    bool startupVersionCheck = true;
};

struct WebSettings
{
    QString surface = QStringLiteral("embedded");  ///< embedded | external
    /// 默认关闭：Chromium 本就节流隐藏视图（无 rAF、定时器变慢），Frozen
    /// 还会挂起 JS/websocket——切回标签时 agent WebUI 肉眼可见地"刷新"。
    /// 关着它，内存仍由 LRU 释放（maxLiveTabs）兜住。
    bool freezeInactiveTabs = false;
    int maxLiveTabs = 8;
    QString downloadDir;     ///< 空串 = 平台默认（~/Downloads）
    QString chromiumFlags;
    QString homeUrl;
};

struct SkillsSettings
{
    QJsonArray roots;  ///< 空数组 = 内置默认扫描根列表
    bool includePluginCaches = true;
    int maxDepth = 6;
};

struct LoggingSettings
{
    qint64 maxFileSize = 5 * 1024 * 1024;
    int maxFiles = 3;
    /// 最低落盘级别：debug/info/warning/critical/off，取值由 Logging 校验
    QString level = QStringLiteral("debug");
    /// 是否把日志同时镜像到 stderr（控制台调试用）
    bool mirrorToStderr = true;
};

struct PluginsSettings
{
    bool enabled = false;
    QStringList disabledIds;
};

/// settings.json 的类型化访问层：应用的其它部分不许直接读这个文件。
class Settings : public QObject
{
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr);

    // <dataRoot>/settings.json 的路径
    static QString settingsFilePath();

    // 各配置组的只读访问
    const WindowSettings &window() const { return m_window; }
    const AppearanceSettings &appearance() const { return m_appearance; }
    const LocaleSettings &locale() const { return m_locale; }
    const LauncherSettings &launcherOptions() const { return m_launcher; }
    const WebSettings &webOptions() const { return m_web; }
    const SkillsSettings &skillsOptions() const { return m_skills; }
    const LoggingSettings &loggingOptions() const { return m_logging; }
    const PluginsSettings &pluginsOptions() const { return m_plugins; }

    // 按规范命名的快捷取值
    QString themeId() const;
    QString fontFamily() const;
    QString windowTitle() const;
    QJsonArray skillsRoots() const;

    // UI 用的写入口：各自发 valueChanged(key)；持久化要显式调 save()
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

    // 把整个设置对象持久化（原子写）
    OpResult save();

Q_SIGNALS:
    /**
     * @brief 任一设置值变化时发射
     * @param key 点分键名，如 "appearance.theme"
     */
    void valueChanged(const QString &key);

private:
    // 从 settings.json 读入全部键（缺键取默认值）
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
