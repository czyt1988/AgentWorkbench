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
/// 测 agentcatalog 的一次性命令执行链（仅 Windows）：install 命令真的经
/// `cmd /c <installCommand>` 运行，且日志带上命令行、退出码与命令自身的输出。
class TestAgentScripts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // 命令日志的端到端检查：launch 一个 install 真的在跑
    // `cmd /c <installCommand>`，日志随后带上命令行、退出码与命令自身的
    // 输出（见 testInstallCommandIsLogged）。
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

        // install 命令是 cmd 内建：不需要任何工具或网络，且有输出可捕获。
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
