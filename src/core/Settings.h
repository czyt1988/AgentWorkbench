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
    bool trayEnabled = true;      ///< 系统托盘图标；系统没有托盘时自动无效
    /// 用户已记住关闭选择时，true = 点窗口 × 收进托盘；false = 直接退出。
    bool closeToTray = false;
    /// 是否跳过关闭选择对话框，直接按 closeToTray 执行。默认 false，每次关闭
    /// 都要求用户选择。
    bool rememberCloseAction = false;
    /// 只允许一个实例：再次启动时唤起已在跑的窗口并退出。关掉它等于允许
    /// 两个进程共用同一批 Web profile，Chromium 的 profile 锁会拒绝后者。
    bool singleInstance = true;
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

/// Markdown 工作台的视图偏好与会话行为。视图键是「显示」偏好；autoOpen 是
/// 给重度用户的开关（默认关：启动不为 WebEngine 交税，决策 D-10）。
struct MarkdownSettings
{
    /// preview | split | source；非法取值回落 preview
    QString mode = QStringLiteral("preview");
    /// 正文像素字号；0 = 用当前主题的 fontSizeBody
    int fontSizePx = 0;
    /// 正文栏宽（像素）；0 = 用内置基准
    int contentWidthPx = 0;
    bool showOutline = true;
    /// 启动时自动打开工作台窗口（恢复上次的 tab 组）；默认关
    bool autoOpenWorkbench = false;
};

/// Markdown 编辑器的输入行为（A 批「输入手感」）：缩进、回车延续、格式
/// 化快捷键与自动配对；另含粘贴图片的落盘目录。视图偏好在
/// MarkdownSettings（"markdown" 组），两者消费方不同——这里喂的是编辑
/// 引擎的动作参数。
struct MarkdownEditorSettings
{
    /// true = Tab 插空格（markdown 嵌套列表的可移植形态）；false = 插 \t
    bool indentWithSpaces = true;
    /// 每级缩进的空格数（indentWithSpaces 时生效），夹取 [1, 8]
    int indentWidth = 2;
    /// 回车延续列表 / 引用 / 任务项（空项回车退出）
    bool smartEnter = true;
    /// 编辑器聚焦时的格式化快捷键（Ctrl+B 加粗等）
    bool formatShortcuts = true;
    /// 输入时自动配对 markdown 标记（`*`、反引号、`~`、`[`、`(`）
    bool autoPair = true;
    /// 粘贴图片的落盘目录（相对工作区根或文档目录，可多级）
    QString imageAssetsDir = QStringLiteral("assets");
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
    const MarkdownSettings &markdownOptions() const { return m_markdown; }
    const MarkdownEditorSettings &markdownEditorOptions() const
    {
        return m_markdownEditor;
    }

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
    void setSidebarWidth(int width);
    void setLastPageId(const QString &pageId);
    void setWebSurface(const QString &surface);
    void setWebChromiumFlags(const QString &flags);
    void setWebHomeUrl(const QString &url);
    void setStartupVersionCheck(bool on);
    void setTrayEnabled(bool on);
    void setCloseToTray(bool on);
    void setRememberCloseAction(bool on);
    void setSingleInstance(bool on);
    void setSkillRoots(const QJsonArray &roots);
    void setPluginsDisabledIds(const QStringList &ids);
    void setPluginsGloballyEnabled(bool enabled);
    void setMarkdownMode(const QString &mode);
    void setMarkdownFontSizePx(int pixels);
    void setMarkdownContentWidthPx(int pixels);
    void setMarkdownShowOutline(bool show);
    // 「启动时自动打开工作台」开关（默认关，决策 D-10）
    void setMarkdownAutoOpenWorkbench(bool open);
    // --- Markdown 编辑器输入行为（设置页写、工作台页读） ---
    void setMarkdownEditorIndentWithSpaces(bool useSpaces);
    void setMarkdownEditorIndentWidth(int spaces);
    void setMarkdownEditorSmartEnter(bool on);
    void setMarkdownEditorFormatShortcuts(bool on);
    void setMarkdownEditorAutoPair(bool on);
    void setMarkdownEditorImageAssetsDir(const QString &dir);

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
    MarkdownSettings m_markdown;
    MarkdownEditorSettings m_markdownEditor;
};

} // namespace awb::core

#endif // AWB_CORE_SETTINGS_H
