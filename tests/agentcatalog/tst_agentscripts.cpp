#include "awbtest.h"

#include "agentcatalog/AgentRepository.h"
#include "agentcatalog/AgentsFacade.h"
#include "core/Logging.h"
#include "core/Settings.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using awb::agentcatalog::AgentRepository;
using awb::agentcatalog::AgentsFacade;

#ifdef Q_OS_WIN
class TestAgentScripts : public QObject
{
    Q_OBJECT

private slots:
    // End-to-end check of the command log: launching an install really runs
    // `cmd /c <installCommand>`, and the log then carries the command line,
    // the exit code and the command's own output (see
    // testInstallCommandIsLogged).
    void testInstallCommandIsLogged()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        QTemporaryDir logDir;
        QVERIFY(logDir.isValid());

        {
            AgentRepository seed(dataRoot.path());
            seed.load();
        }
        awb::core::Settings settings;
        AgentsFacade facade(&settings, dataRoot.path());

        // The install command is a cmd builtin: no tooling or network needed,
        // and it prints something to capture.
        QVariantMap fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("Log Probe"));
        fields.insert(QStringLiteral("command"),
                      QStringLiteral("logprobe serve"));
        fields.insert(QStringLiteral("webUrl"),
                      QStringLiteral("http://127.0.0.1:9"));
        fields.insert(QStringLiteral("installCommand"),
                      QStringLiteral("echo install-finished"));
        QVERIFY(facade.addAgent(fields));
        const QString id = QStringLiteral("log-probe");

        QSignalSpy finished(&facade, &AgentsFacade::installFinished);
        awb::core::Logging::install(logDir.path());
        facade.install(id);
        QVERIFY(finished.wait(15000));
        awb::core::Logging::uninstall();

        QFile log(logDir.filePath(QStringLiteral("agentworkbench.log")));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(log.readAll());
        QVERIFY2(
            text.contains(QStringLiteral(
                "[cmd] install \"%1\": "
                "running: cmd /c echo install-finished").arg(id)),
            qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("done, exit=0")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("install-finished")),
                 qPrintable(text));
    }
};

#include "tst_agentscripts.moc"
AWB_TEST(TestAgentScripts)
#endif
