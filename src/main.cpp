#include "agents/AgentsFacade.h"
#include "core/LegacyImport.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
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
    QGuiApplication::setApplicationName(QStringLiteral("AgentWorkbench"));

    // 1) Logging first: any later failure must be on disk (specs/01 §4.9).
    awb::core::Logging::install();

    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-icon.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Load locale-appropriate translation from embedded :/i18n/ resources.
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("agentworkbench"),
                        QStringLiteral("_"), QStringLiteral(":/i18n")))
        app.installTranslator(&translator);

    // Adopt a pre-0.4 ~/.AgentLauncher data directory on the first start
    // after the upgrade, before anything else touches the data root
    // (01-architecture.md §7.3). Runs at most once; the notice is shown
    // once by the UI below.
    QString legacyNotice;
    const bool legacyImported = awb::core::LegacyImport::runOnce(
        awb::core::Paths::dataRoot(), &legacyNotice);

    // Typed settings.json access; written with defaults on the very first
    // start so the file exists for the UI and for the legacy-title hint in
    // AgentRepository::load().
    awb::core::Settings settings;
    if (!QFile::exists(awb::core::Settings::settingsFilePath()))
        settings.save();

    // Semantic tokens as the QML global `theme` (specs/01 §8.2: C++
    // globals live on the AgentWorkbench.App URI, never on the qml_module
    // URI itself). Qt requires singleton type names to start uppercase, so
    // the C++ registration uses "Theme" and the QML-facing name stays
    // lowercase through the root alias in main.qml (spec amendment noted in
    // 01 §8.2).
    awb::theme::ThemeRegistry themeRegistry;
    awb::theme::Theme theme(&settings, &themeRegistry);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme", &theme);

    // The agents feature: repository (agents.json), model, runtime, health
    // monitor — everything QML reaches through the `agents` global.
    awb::agents::AgentsFacade agents(&settings, awb::core::Paths::dataRoot(),
                                     &theme);
    agents.start();

    QQmlApplicationEngine engine;
    // Prefer the QML modules deployed next to the executable. Without this,
    // a QML2_IMPORT_PATH/QML_IMPORT_PATH environment variable (or a Qt
    // installation found via library paths) would shadow the bundled modules
    // on machines that have their own Qt, which can break the UI with
    // "module ... is not installed".
    engine.addImportPath(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("agents"), &agents);
    // One-shot notice: non-empty only on the first start after adopting the
    // legacy AgentLauncher data directory.
    engine.rootContext()->setContextProperty(
        QStringLiteral("legacyImportNotice"),
        legacyImported ? legacyNotice : QString());

    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        // The UI failed to load. Log enough context (import search paths and
        // whether the bundled modules are actually on disk) so deployment
        // problems on end-user machines are diagnosable from the log file
        // alone, and tell the user what happened — a portable install that
        // dies silently is indistinguishable from a crash.
        const QString bundledQml = QCoreApplication::applicationDirPath()
            + QStringLiteral("/qml/QtQuick");
        const bool layoutsPresent = QDir(bundledQml + QStringLiteral("/Layouts")).exists();
        const bool controlsImplPresent =
            QDir(bundledQml + QStringLiteral("/Controls/impl")).exists();
        qWarning() << "QML import paths:" << engine.importPathList();
        qWarning() << "Bundled qml modules present:" << layoutsPresent
                   << controlsImplPresent;
        // QMessageBox lives in QtWidgets, which this QtGui-only app does not
        // link; use the native message box instead so the failure is visible.
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
