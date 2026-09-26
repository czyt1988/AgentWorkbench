#include "agents/AgentsFacade.h"
#include "core/LegacyImport.h"
#include "core/Logging.h"
#include "core/OpResult.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/ShellController.h"
#include "shell/UiServices.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
#include "web/WebTabsFacade.h"
#include "workbench/BuiltinPages.h"
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
    //    (01-architecture.md §4.9).
    QGuiApplication::setApplicationName(QStringLiteral("AgentWorkbench"));
    awb::core::Logging::install();

    // Settings are read before QGuiApplication: the user's Chromium flags
    // must be injected BEFORE QtWebEngineQuick::initialize(), which itself
    // has to run before QGuiApplication (specs/01 §4.9).
    awb::core::Settings settings;
    if (!QFile::exists(awb::core::Settings::settingsFilePath()))
        settings.save();
    const QByteArray chromiumFlags =
        settings.webOptions().chromiumFlags.toUtf8();
    if (!chromiumFlags.isEmpty())
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);
#ifdef AWB_ENABLE_WEBENGINE
    // GPU/driver problems are worked around through web.chromiumFlags
    // (02 §6.7); a hard failure logs and continues degraded.
    QtWebEngineQuick::initialize();
#endif

    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral("0.4.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-icon.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Load locale-appropriate translation from embedded :/i18n/ resources.
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("agentworkbench"),
                        QStringLiteral("_"), QStringLiteral(":/i18n")))
        app.installTranslator(&translator);

    // Adopt a pre-0.4 ~/.AgentLauncher data directory on the first start
    // after the upgrade, before anything else touches the data root
    // (01-architecture.md §7.3).
    QString legacyNotice;
    const bool legacyImported = awb::core::LegacyImport::runOnce(
        awb::core::Paths::dataRoot(), &legacyNotice);

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

    awb::workbench::EnvironmentService environment;
    awb::workbench::WorkbenchContext workbench(&nav, &ui, &notifications,
                                               &agents, &webTabs);
    workbench.setLegacyImportNotice(legacyImported ? legacyNotice
                                                   : QString());
    awb::workbench::BuiltinPages builtinPages(&nav, &shell, &agents, &webTabs,
                                              &notifications);

#ifdef AWB_ENABLE_WEBENGINE
    // The embedded surface registers itself with the web domain; profiles
    // are exposed to QML for the per-agent views (specs/01 §4.6).
    awb::web::WebEngineSurfaceProvider webSurface(&webTabs);
    awb::web::WebEngineProfileStore profileStore;
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "WebProfiles",
                                 &profileStore);
#endif

    // 4) Register the QML globals (specs/01 §8.2): uppercase type names on
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
