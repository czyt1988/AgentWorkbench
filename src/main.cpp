#include "AgentConfig.h"
#include "AgentLauncher.h"
#include "AgentModel.h"
#include "Logger.h"
#include "core/LegacyImport.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTranslator>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
    QGuiApplication::setApplicationName(QStringLiteral("AgentWorkbench"));

    Logger::install();

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
        AgentConfig::userDataDir(), &legacyNotice);

    AgentConfig config;
    config.load(); // reads agents.json, re-applying the bundled default launchers

    AgentModel model;
    model.setAgents(config.agents());

    AgentLauncher launcher(&model);
    // Deletion records for built-in agents, persisted in agents.json so the
    // shipped definition is not re-applied to them on the next start.
    launcher.setRemovedIds(config.removedIds());
    launcher.start();

    QQmlApplicationEngine engine;
    // Prefer the QML modules deployed next to the executable. Without this,
    // a QML2_IMPORT_PATH/QML_IMPORT_PATH environment variable (or a Qt
    // installation found via library paths) would shadow the bundled modules
    // on machines that have their own Qt, which can break the UI with
    // "module ... is not installed".
    engine.addImportPath(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("agentModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("launcher"), &launcher);
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
                    .arg(Logger::logFilePath())
                    .utf16()),
            L"AgentWorkbench", MB_ICONERROR | MB_OK);
#endif
        return -1;
    }

    return app.exec();
}
