#include "awbtest.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentRepository.h"
#include "agentcatalog/AgentScripts.h"
#include "agentcatalog/AgentStateStore.h"
#include "agentcatalog/AgentsFacade.h"
#include "core/Logging.h"
#include "core/Settings.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using awb::agentcatalog::AgentDefinition;
using awb::agentcatalog::AgentModel;
using awb::agentcatalog::AgentScripts;
using awb::agentcatalog::AgentStateStore;
using awb::agentcatalog::AgentRepository;
using awb::agentcatalog::AgentsFacade;

#ifdef Q_OS_WIN
/// 测 agentcatalog 的一次性命令执行链（仅 Windows）：install 命令真的经
/// `cmd /c <installCommand>` 运行，且日志带上命令行、退出码与命令自身的
/// 输出；版本探测的三态结论（已装/未装/不知道）、瞬时失败重试与启动错峰。
class TestAgentScripts : public QObject
{
    Q_OBJECT

private:
    // 造一个只配了 versionCommand 的定义，id 为 "v-probe" 的变体。
    AgentDefinition probeDefinition(const QString &suffix,
                                    const QString &versionCommand)
    {
        AgentDefinition d;
        d.id = QStringLiteral("v-probe-%1").arg(suffix);
        d.name = QStringLiteral("Probe %1").arg(suffix);
        d.command = QStringLiteral("noop serve");
        d.webUrl = QStringLiteral("http://127.0.0.1:9");
        d.versionCommand = versionCommand;
        return d;
    }

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

    // 版本探测的权威结论：echo 的输出解析出版本、installed/known 翻真。
    void testVersionCheckResolvesVersion()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        AgentModel model;
        AgentStateStore stateStore(dataRoot.path());
        AgentScripts scripts(&model, &stateStore);
        const AgentDefinition d = probeDefinition(
            QStringLiteral("ok"), QStringLiteral("echo 9.9.9"));
        model.setDefinitions({d});

        QSignalSpy resolved(&scripts, &AgentScripts::versionResolved);
        scripts.checkVersion(d.id);
        QVERIFY(resolved.wait(15000));
        QCOMPARE(resolved.at(0).at(1).toString(), QStringLiteral("9.9.9"));
        QVERIFY(model.state(d.id).installed);
        QVERIFY(model.state(d.id).versionKnown);
        QCOMPARE(model.state(d.id).version, QStringLiteral("9.9.9"));
    }

    // 命令跑完、退出非零且没有版本输出：权威的「未安装」——installed
    // 翻假且 known 为真；此时没有 versionResolved 信号。
    void testVersionCheckMissingVerdict()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        AgentModel model;
        AgentStateStore stateStore(dataRoot.path());
        AgentScripts scripts(&model, &stateStore);
        const AgentDefinition d = probeDefinition(
            QStringLiteral("missing"), QStringLiteral("exit 3"));
        model.setDefinitions({d});

        QSignalSpy resolved(&scripts, &AgentScripts::versionResolved);
        scripts.checkVersion(d.id);
        // 失败没有信号可等：spinner 清除（探测结束后 ≤500 ms）是唯一的
        // 完成标志，且本用例只有一轮检查、不存在中途回落的窗口。
        QTRY_VERIFY_WITH_TIMEOUT(!model.state(d.id).checkingVersion, 15000);
        QCOMPARE(resolved.count(), 0);
        QVERIFY(!model.state(d.id).installed);
        QVERIFY(model.state(d.id).versionKnown);
        QCOMPARE(model.state(d.id).version, QString());
    }

    // 超时是「不知道」而不是「未安装」：先让探测拿到 9.9.9，再把命令换
    // 成挂死的 ping——超时 + 一轮自动重试全部失败后，上次的结论原样保留，
    // versionKnown 翻假（卡片据此隐藏版本徽标，而不是显示「未安装」）。
    void testVersionTimeoutKeepsPreviousVerdict()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        AgentModel model;
        AgentStateStore stateStore(dataRoot.path());
        AgentScripts scripts(&model, &stateStore);
        // 毫秒级时序：300 ms 超时 / 120 ms 后重试，全链路 ~1 s 内走完。
        scripts.setVersionProbeTimingForTesting(300, 120, 0);

        const AgentDefinition quick = probeDefinition(
            QStringLiteral("kept"), QStringLiteral("echo 9.9.9"));
        model.setDefinitions({quick});
        QSignalSpy resolved(&scripts, &AgentScripts::versionResolved);
        scripts.checkVersion(quick.id);
        QVERIFY(resolved.wait(15000));
        QVERIFY(model.state(quick.id).installed);
        QCOMPARE(model.state(quick.id).version, QStringLiteral("9.9.9"));

        // 换成约 30 s 才结束的命令：必超时；重试一轮后预算用尽。
        AgentDefinition hang = quick;
        hang.versionCommand = QStringLiteral("ping -n 30 127.0.0.1");
        QVERIFY(model.replaceDefinition(hang));
        scripts.checkVersion(quick.id);
        // 首轮 300 + 重试间隔 120 + 重试 300 + spinner 收尾 500 余量；
        // 2 s 有近一倍余量，避开对瞬时调度的敏感。
        QTest::qWait(2000);

        QVERIFY2(model.state(quick.id).installed,
                 "timeout must not clear the previous verdict");
        QCOMPARE(model.state(quick.id).version, QStringLiteral("9.9.9"));
        QVERIFY(!model.state(quick.id).versionKnown);
        // 预算用尽后 spinner 也必须收掉，不能挂着转圈过整个会话。
        QTRY_VERIFY_WITH_TIMEOUT(!model.state(quick.id).checkingVersion, 5000);
    }

    // 瞬时失败的重试能自愈：首轮超时后、重试出发前把命令换回能跑的，
    // 重试拿到版本并翻回 known——启动拥堵场景（错峰后第二轮成功）的最小
    // 复现。
    void testVersionRetryRecoversAfterSlowStart()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        AgentModel model;
        AgentStateStore stateStore(dataRoot.path());
        AgentScripts scripts(&model, &stateStore);
        // 首轮 300 ms 必超时；重试留 800 ms 窗口，期间换掉命令。
        scripts.setVersionProbeTimingForTesting(300, 800, 0);

        AgentDefinition hang = probeDefinition(
            QStringLiteral("recover"), QStringLiteral("ping -n 30 127.0.0.1"));
        model.setDefinitions({hang});
        scripts.checkVersion(hang.id);
        QTest::qWait(450);

        AgentDefinition quick = hang;
        quick.versionCommand = QStringLiteral("echo 3.1.4");
        QVERIFY(model.replaceDefinition(quick));
        QSignalSpy resolved(&scripts, &AgentScripts::versionResolved);
        QVERIFY(resolved.wait(15000));
        QCOMPARE(resolved.at(0).at(1).toString(), QStringLiteral("3.1.4"));
        QVERIFY(model.state(hang.id).installed);
        QVERIFY(model.state(hang.id).versionKnown);
    }

    // 启动错峰：checkVersions 给每个 agent 排开出发时刻，没有一个被
    // 丢下（三个探测全部解析出版本）。
    void testStartupChecksAreStaggeredButComplete()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        AgentModel model;
        AgentStateStore stateStore(dataRoot.path());
        AgentScripts scripts(&model, &stateStore);
        scripts.setVersionProbeTimingForTesting(15000, 200, 120);

        const AgentDefinition a = probeDefinition(
            QStringLiteral("s1"), QStringLiteral("echo 1.0.0"));
        const AgentDefinition b = probeDefinition(
            QStringLiteral("s2"), QStringLiteral("echo 1.0.1"));
        const AgentDefinition c = probeDefinition(
            QStringLiteral("s3"), QStringLiteral("echo 1.0.2"));
        model.setDefinitions({a, b, c});

        QSignalSpy resolved(&scripts, &AgentScripts::versionResolved);
        scripts.checkVersions();
        QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 3, 15000);
        QVERIFY(model.state(a.id).installed);
        QVERIFY(model.state(b.id).installed);
        QVERIFY(model.state(c.id).installed);
    }
};

#include "tst_agentscripts.moc"
AWB_TEST(TestAgentScripts)
#endif
