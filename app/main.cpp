#include "agents/AgentsFacade.h"
#include "core/LegacyImport.h"
#include "core/PluginHost.h"
#include "core/Logging.h"
#include "core/OpResult.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "skills/SkillsFacade.h"
#include "shell/Notifications.h"
#include "shell/ShellController.h"
#include "shell/UiServices.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
#include "web/WebTabsFacade.h"
#include "workbench/BuiltinPages.h"
#include "workbench/PluginServices.h"
#include "workbench/EnvironmentService.h"
#include "workbench/WorkbenchContext.h"

#ifdef AWB_ENABLE_WEBENGINE
#include "web/webengine/WebEngineProfileStore.h"
#include "web/webengine/WebEngineSurfaceProvider.h"
#include <QtWebEngineQuick>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

int main(int argc, char *argv[])
{
    // 1) Logging first: any later failure must be on disk
    QGuiApplication::setApplicationName(QStringLiteral("AgentWorkbench"));
    awb::core::Logging::install();

    // Settings are read before QGuiApplication: the user's Chromium flags
    // must be injected BEFORE QtWebEngineQuick::initialize(), which itself
    // has to run before QGuiApplication. The first-run save
    // is deferred until after LegacyImport::runOnce — writing settings.json
    // early would make its untouched-check (data root holds nothing but
    // log/) fail forever.
    awb::core::Settings settings;
    // Apply the configured rotation policy (logging.maxFileSize/maxFiles).
    // The install() above must run first with the defaults — Settings may
    // warn and those warnings belong on disk — so re-install only when the
    // user actually customized the policy.
    const awb::core::LoggingSettings logOpts = settings.loggingOptions();
    if (logOpts.maxFileSize != awb::core::Logging::DEFAULT_MAX_FILE_SIZE
        || logOpts.maxFiles != awb::core::Logging::DEFAULT_MAX_FILES)
        awb::core::Logging::install(QString(), logOpts.maxFileSize,
                                    logOpts.maxFiles);
    const QByteArray chromiumFlags =
        settings.webOptions().chromiumFlags.toUtf8();
    if (!chromiumFlags.isEmpty())
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);
#ifdef AWB_ENABLE_WEBENGINE
    // GPU/driver problems are worked around through web.chromiumFlags
    // a hard failure logs and continues degraded.
    QtWebEngineQuick::initialize();
#endif

    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral("0.4.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-icon.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Load locale-appropriate translation from embedded:/i18n/ resources.
    // locale.override forces a locale; empty follows the system.
    QTranslator translator;
    const QString forcedLocale = settings.locale().overrideName;
    const QLocale locale = forcedLocale.isEmpty() ? QLocale()
                                                  : QLocale(forcedLocale);
    if (translator.load(locale, QStringLiteral("agentworkbench"),
                        QStringLiteral("_"), QStringLiteral(":/i18n")))
        app.installTranslator(&translator);

    // Adopt a pre-0.4 ~/.AgentLauncher data directory on the first start
    // after the upgrade, before anything else touches the data root
    QString legacyNotice;
    const bool legacyImported = awb::core::LegacyImport::runOnce(
        awb::core::Paths::dataRoot(), &legacyNotice);
    // First run: materialize default settings.json only now — before this,
    // the data root had to stay empty (except log/) for the legacy adoption
    // check above.
    if (!QFile::exists(awb::core::Settings::settingsFilePath()))
        settings.save();

    // 3) Assembly, dependency order from the bottom up:
    //    core -> theme -> shell -> agents -> workbench.
    awb::theme::ThemeRegistry themeRegistry;
    awb::theme::Theme theme(&settings, &themeRegistry);

    awb::shell::NavigationModel nav;
    awb::shell::ShellController shell(&settings);
    awb::shell::UiServices ui;
    awb::shell::Notifications notifications;

    awb::agents::AgentsFacade agents(&settings, awb::core::Paths::dataRoot(),
                                     &theme);
    agents.start();

    awb::web::WebTabsFacade webTabs(&settings);
    awb::skills::SkillsFacade skills(&settings);

    awb::workbench::EnvironmentService environment;
    awb::workbench::WorkbenchContext workbench(&nav, &ui, &notifications,
                                               &agents, &webTabs, &settings);
    workbench.setLegacyImportNotice(legacyImported ? legacyNotice
                                                   : QString());
    // Plugins: discover manifests always (for the settings
    // list), load libraries only when the user opted in — failures log and
    // never block startup. This runs BEFORE BuiltinPages restores the last
    // page, so a plugin page id survives a restart.
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
        if (enabled)
            enabledIds.append(manifest.id);
    }
    workbench.setDiscoveredPlugins(pluginEntries);

    awb::workbench::PluginServices pluginServices(
        &nav, &ui, &notifications, &theme, &webTabs, &settings);
    if (pluginsOn) {
        QList<awb::core::PluginHost::Manifest> enabled;
        for (const awb::core::PluginHost::Manifest &manifest : manifests) {
            if (!enabledIds.contains(manifest.id))
                continue;
            // resolve() flips the flag loadEnabled() filters on — discover()
            // leaves it false (disabled until the user opts in).
            awb::core::PluginHost::Manifest copy = manifest;
            copy.enabled = true;
            enabled.append(copy);
        }
        pluginHost.loadEnabled(enabled, &pluginServices);
    }

    awb::workbench::BuiltinPages builtinPages(&nav, &shell, &agents, &webTabs,
                                              &notifications, &skills);


#ifdef AWB_ENABLE_WEBENGINE
    // The embedded surface registers itself with the web domain; profiles
    // are exposed to QML for the per-agent views.
    awb::web::WebEngineSurfaceProvider webSurface(&webTabs);
    awb::web::WebEngineProfileStore profileStore;
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "WebProfiles",
                                 &profileStore);
#endif

    // 4) Register the QML globals: uppercase type names on
    //    the AgentWorkbench.App URI; the QML-facing lowercase names are
    //    root aliases in MainWindow.qml.
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
    // First visit scans once the page opens (async-shaped refresh()).
    QObject::connect(&nav, &awb::shell::NavigationModel::pagesChanged,
                     &skills, [&skills, &nav]() {
                         if (nav.currentPageId() == QLatin1String("skills")
                             && skills.model()->rowCount() == 0
                             && !skills.scanning())
                             skills.refresh();
                     });
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Workbench",
                                 &workbench);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Environment",
                                 &environment);

    QQmlApplicationEngine engine;
    // Prefer the QML modules deployed next to the executable (deployed Qt
    // runtime on end-user machines).
    engine.addImportPath(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/qml"));

    engine.load(QUrl(
        QStringLiteral("qrc:/qt/qml/AgentWorkbench/shell/MainWindow.qml")));
    if (engine.rootObjects().isEmpty()) {
        // The UI failed to load. Log enough context so deployment problems
        // on end-user machines are diagnosable from the log file alone, and
        // tell the user what happened.
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
        return -1;
    }

    return app.exec();
}
