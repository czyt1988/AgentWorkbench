#include "agentcatalog/AgentsFacade.h"
#include "core/LegacyImport.h"
#include "core/PluginHost.h"
#include "core/Logging.h"
#include "core/OpResult.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "skillcatalog/SkillsFacade.h"
#include "shell/Notifications.h"
#include "shell/ShellController.h"
#include "shell/UiServices.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
#include "tools/MarkdownEdit.h"
#include "tools/ToolsFacade.h"
#include "web/WebTabsFacade.h"
#include "workbench/BuiltinPages.h"
#include "workbench/PluginServices.h"
#include "workbench/EnvironmentService.h"
#include "workbench/WorkbenchContext.h"

#ifdef AWB_ENABLE_WEBENGINE
#include "web/webengine/WebEngineCompat.h"
#include "web/webengine/WebEngineProfileStore.h"
#include "web/webengine/WebEngineSurfaceProvider.h"
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtWebEngineQuick>
#else
#include <QtWebEngine>
#endif
#endif

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QTranslator>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief 应用入口：装配整个对象图并启动 QML 引擎
 *
 * 启动顺序是本文件的核心契约，不能随意调换：
 * 1) 日志最先装（之后任何失败都有磁盘记录）；
 * 2) 设置在 QGuiApplication 之前读（Chromium flags 必须在
 *    QtWebEngineQuick::initialize() 之前注入，而后者又必须先于
 *    QGuiApplication）；
 * 3) 自底向上装配各域（core → theme → shell → agentcatalog →
 *    workbench），插件在页面恢复之前加载；
 * 4) QML 全局注册（AgentWorkbench.App，类型名大写）之后加载主窗口。
 *
 * UI 加载失败时打出足够排查的上下文（QML import 路径），Windows 上再
 * 弹一个说明弹窗，排空日志队列后以非零码退出。
 *
 * @param argc 参数个数（未使用）
 * @param argv 参数数组（未使用）
 * @return app.exec() 的退出码；UI 加载失败返回 -1
 */
int main(int argc, char *argv[])
{
    // 1) 日志最先装：之后的任何失败都必须落在磁盘上
    QGuiApplication::setApplicationName(QStringLiteral("AgentWorkbench"));
    awb::core::Logging::install();

    // 设置要在 QGuiApplication 之前读：用户配的 Chromium flags 必须在
    // QtWebEngineQuick::initialize() 之前注入，而后者又必须先于
    // QGuiApplication。首次运行的 save() 推迟到 LegacyImport::runOnce
    // 之后——过早写 settings.json 会让它的「数据根只有 log/」未触碰
    // 判定永远失败。
    awb::core::Settings settings;
    // 应用配置的日志选项（轮转策略、级别、stderr 镜像）。上面第一次
    // install() 必须先用默认值跑——Settings 构造期间的告警要落盘——所以
    // 只在用户真改过某一项时才二次 install。
    const awb::core::LoggingSettings logOpts = settings.loggingOptions();
    const awb::core::LoggingSettings logDefaults;
    if (logOpts.maxFileSize != logDefaults.maxFileSize
        || logOpts.maxFiles != logDefaults.maxFiles
        || logOpts.level != logDefaults.level
        || logOpts.mirrorToStderr != logDefaults.mirrorToStderr) {
        awb::core::Logging::install(QString(), logOpts.maxFileSize,
                                    logOpts.maxFiles, logOpts.level,
                                    logOpts.mirrorToStderr);
    }
    const QByteArray chromiumFlags =
        settings.webOptions().chromiumFlags.toUtf8();
    if (!chromiumFlags.isEmpty()) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);
    }
#ifdef AWB_ENABLE_WEBENGINE
    // GPU/驱动问题经 web.chromiumFlags 绕过；此处硬失败只记日志、
    // 降级继续。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QtWebEngineQuick::initialize();
#else
    QtWebEngine::initialize();
#endif
#endif

    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral("0.4.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-icon.png")));
    // 扁平的兜底样式：Qt 6 把 "Default" 改名成了 "Basic"。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QQuickStyle::setStyle(QStringLiteral("Basic"));
#else
    QQuickStyle::setStyle(QStringLiteral("Default"));
#endif

    // 按系统区域加载内嵌 :/i18n/ 里的翻译；locale.override 强制区域，
    // 空串跟随系统。
    QTranslator translator;
    const QString forcedLocale = settings.locale().overrideName;
    const QLocale locale = forcedLocale.isEmpty() ? QLocale()
                                                  : QLocale(forcedLocale);
    if (translator.load(locale, QStringLiteral("agentworkbench"),
                        QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        app.installTranslator(&translator);
    }

    // 全局 UI 字体，在引擎创建之前应用：只设 family，平台字号保留。
    // 缺字体（比如非 Windows 上的 Microsoft YaHei）经 QFont 匹配退回
    // 系统默认；运行期换字体由 MainWindow 的 font.family 绑定
    // theme.family 驱动。
    {
        const QString fontFamily = settings.appearance().fontFamily;
        if (!fontFamily.isEmpty()) {
            QFont font = app.font();
            font.setFamily(fontFamily);
            app.setFont(font);
        }
    }

    // 升级后首次启动时收编升级前的 ~/.AgentLauncher 数据目录，赶在
    // 其它任何东西碰数据根之前
    QString legacyNotice;
    const bool legacyImported = awb::core::LegacyImport::runOnce(
        awb::core::Paths::dataRoot(), &legacyNotice);
    // 首次运行：现在才落默认 settings.json——在此之前数据根必须保持
    // 空置（除 log/），上面的 legacy 收编判定才成立。
    if (!QFile::exists(awb::core::Settings::settingsFilePath())) {
        settings.save();
    }

    // 3) 装配：依赖顺序自底向上——core → theme → shell →
    //    agentcatalog → workbench。
    awb::theme::ThemeRegistry themeRegistry;
    awb::theme::Theme theme(&settings, &themeRegistry);

    awb::shell::NavigationModel nav;
    awb::shell::ShellController shell(&settings);
    awb::shell::UiServices ui;
    awb::shell::Notifications notifications;

    awb::agentcatalog::AgentsFacade agents(&settings, awb::core::Paths::dataRoot(),
                                     &theme);
    agents.start();

    awb::web::WebTabsFacade webTabs(&settings);
    awb::skillcatalog::SkillsFacade skills(&settings);
    // 启动即就绪：同步恢复上次扫描的 JSON 缓存（页面一打开就有数据），
    // 同时派一次后台真扫描，结果落地后刷新界面并固化缓存。点击 Skills
    // 页不再触发任何扫描。
    skills.start();
    awb::tools::ToolsFacade tools(awb::core::Paths::dataRoot());
    // Agent Tools 编辑器的 markdown 支持：语法高亮 + 右键菜单的格式化动作。
    // 配色跟 Theme 走（切换主题即时重扫）。
    awb::tools::MarkdownEdit markdownEdit(&theme);

    // 构造只恢复上次的探测结果（首屏立刻有版本可显示），start() 才派
    // 后台那一轮真探测——探测要等子进程，不占装配期。首轮延迟 4 s：
    // agent 版本探测正在错峰起子进程，全会话没有比启动瞬间更堵的窗口，
    // 探测空闲时只要零点几秒，晚走几秒换来的是两边都别撞车。
    awb::workbench::EnvironmentService environment;
    environment.start(4000);
    awb::workbench::WorkbenchContext workbench(&nav, &ui, &notifications,
                                               &agents, &webTabs, &settings);
    workbench.setLegacyImportNotice(legacyImported ? legacyNotice
                                                   : QString());
    // 插件：manifest 总是发现（设置页列表要用），库只在用户开启总开关后
    // 才装载——失败一律记日志、绝不阻塞启动。这段在 BuiltinPages 恢复
    // 上次页面之前跑，插件页面 id 才能活过一次重启。
    awb::core::PluginHost pluginHost;
    const QList<awb::core::PluginHost::Manifest> manifests =
        pluginHost.discover();
    QVariantList pluginEntries;
    QStringList enabledIds;
    const bool pluginsOn = settings.pluginsOptions().enabled;
    for (const awb::core::PluginHost::Manifest &manifest : manifests) {
        const bool enabled = pluginsOn
            && !settings.pluginsOptions().disabledIds.contains(manifest.id);
        QVariantMap entry;
        entry[QStringLiteral("id")] = manifest.id;
        entry[QStringLiteral("name")] = manifest.name;
        entry[QStringLiteral("version")] = manifest.version;
        entry[QStringLiteral("description")] = manifest.description;
        entry[QStringLiteral("enabled")] = enabled;
        pluginEntries.append(entry);
        if (enabled) {
            enabledIds.append(manifest.id);
        }
    }
    workbench.setDiscoveredPlugins(pluginEntries);

    awb::workbench::PluginServices pluginServices(
        &nav, &ui, &notifications, &theme, &webTabs, &settings);
    if (pluginsOn) {
        QList<awb::core::PluginHost::Manifest> enabled;
        for (const awb::core::PluginHost::Manifest &manifest : manifests) {
            if (!enabledIds.contains(manifest.id)) {
                continue;
            }
            // resolve() 翻的是 loadEnabled() 过滤的那个标志——discover()
            // 留它为 false（用户没开启之前不装载）。
            awb::core::PluginHost::Manifest copy = manifest;
            copy.enabled = true;
            enabled.append(copy);
        }
        pluginHost.loadEnabled(enabled, &pluginServices);
    }

    awb::workbench::BuiltinPages builtinPages(&nav, &shell, &agents, &webTabs,
                                              &notifications, &skills, &tools);


#ifdef AWB_ENABLE_WEBENGINE
    // 内嵌表面把自己注册进 web 域；profile 经 QML 暴露给每个 agent 的
    // 视图。WebEngineCompat 弥合 Qt 5 / Qt 6 的成员名与枚举形状差异。
    awb::web::WebEngineSurfaceProvider webSurface(&webTabs);
    awb::web::WebEngineProfileStore profileStore;
    awb::web::WebEngineCompat webEngineCompat;
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "WebProfiles",
                                 &profileStore);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "WebEngineCompat",
                                 &webEngineCompat);
#endif

    // 4) 注册 QML 全局：AgentWorkbench.App URI 上一律大写类型名；
    //    QML 侧的小写名字是 MainWindow.qml 根部的别名。
    qRegisterMetaType<awb::core::OpResult>();
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme", &theme);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Nav", &nav);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Shell", &shell);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Ui", &ui);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Notifications",
                                 &notifications);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Agents", &agents);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Web", &webTabs);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Skills", &skills);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Tools", &tools);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "MarkdownEdit",
                                 &markdownEdit);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Workbench",
                                 &workbench);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Environment",
                                 &environment);

    QQmlApplicationEngine engine;
    // 优先用部署在可执行文件旁边的 QML 模块（终端用户机器上是随包
    // 部署的 Qt 运行时）。
    engine.addImportPath(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/qml"));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 5 无 qt_add_qml_module，AgentWorkbench 模块经 qrc:/qt/qml/ 下的
    // 生成 qmldir 注册（见 cmake/AwbQtCompat.cmake），导入路径补上它。
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif

    engine.load(QUrl(
        QStringLiteral("qrc:/qt/qml/AgentWorkbench/shell/MainWindow.qml")));
    if (engine.rootObjects().isEmpty()) {
        // UI 加载失败。打出足够的上下文，让终端用户机器上的部署问题
        // 光靠日志文件就能排查；再告知用户发生了什么。
        qWarning() << "QML import paths:" << engine.importPathList();
#ifdef Q_OS_WIN
        ::MessageBoxW(
            nullptr,
            reinterpret_cast<const wchar_t *>(
                QCoreApplication::translate(
                    "main",
                    "The user interface failed to load. The Qt runtime files "
                    "shipped next to the application seem to be missing or "
                    "incomplete.\n\n"
                    "Please re-extract the whole application folder from the "
                    "zip archive (especially the \"qml\" subfolder) and make "
                    "sure your antivirus did not quarantine any files.\n\n"
                    "Details were written to:\n%1")
                    .arg(awb::core::Logging::logFilePath())
                    .utf16()),
            L"AgentWorkbench", MB_ICONERROR | MB_OK);
#endif
        // 排空异步日志队列再退：弹窗期间后台可能还有未写盘的尾部消息。
        awb::core::Logging::uninstall();
        return -1;
    }

    const int exitCode = app.exec();
    // 同上：不先 uninstall，队列里没写盘的尾部日志会随进程消失。
    awb::core::Logging::uninstall();
    return exitCode;
}
