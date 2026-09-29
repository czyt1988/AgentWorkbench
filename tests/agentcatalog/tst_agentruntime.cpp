#include "awbtest.h"

#include "agentcatalog/AgentDefinition.h"
#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentRuntime.h"
#include "core/ProcessRunner.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QtTest>

using awb::agentcatalog::AgentDefinition;
using awb::agentcatalog::AgentModel;
using awb::agentcatalog::AgentRuntime;

/// 测 agentcatalog::AgentRuntime 的启动/停止/强制停止簿记：空命令与 PATH 解析
/// 失败的启动报错、无 PID 时的停止提示、端口→PID 解析、真实进程的启动与杀树、
/// 会话 URL 的捕获与随停止丢弃。
class TestAgentRuntime : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        // launch() 把子进程输出重定向到 Paths::logsDir() 之下——那是测试模式
        // 的数据根，绝不能是开发者真实的数据目录。
        QStandardPaths::setTestModeEnabled(true);
    }

    // 启动命令为空时以 launchFailed 失败，且不记录任何 PID。
    void testLaunchEmptyCommandFails()
    {
        AgentModel model;
        AgentRuntime runtime(&model);
        QSignalSpy spy(&runtime, &AgentRuntime::launchFailed);

        AgentDefinition def;
        def.id = QStringLiteral("a1");
        runtime.launch(def, QString());

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("a1"));
        QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("empty")));
        QVERIFY(!runtime.hasLaunchedAgents());
    }

    // PATH 解析不到的程序在任何东西孵化之前就失败。
    void testLaunchUnresolvableProgramFails()
    {
        AgentModel model;
        AgentRuntime runtime(&model);
        QSignalSpy spy(&runtime, &AgentRuntime::launchFailed);

        AgentDefinition def;
        def.id = QStringLiteral("a2");
        def.command = QStringLiteral("awb-missing-program-xyz --flag");
        runtime.launch(def, QString());

        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("PATH")));
        QVERIFY(!runtime.hasLaunchedAgents());
    }

    // 没有已跟踪 PID 的 stop()（agent 在别处启动）要说明原因。
    void testStopWithoutTrackedPidFails()
    {
        AgentModel model;
        AgentRuntime runtime(&model);
        QSignalSpy spy(&runtime, &AgentRuntime::launchFailed);

        QVERIFY(!runtime.stop(QStringLiteral("ghost")));
        QCOMPARE(spy.count(), 1);
        QVERIFY(!runtime.hasLaunchedAgents());
    }

    void testStopAllWithoutLaunchesReturnsZero()
    {
        AgentModel model;
        AgentRuntime runtime(&model);
        QCOMPARE(runtime.stopAll(), 0);
        QVERIFY(!runtime.hasLaunchedAgents());
    }

    // webUrl 没有可用端口时 forceStop 早早失败——在任何进程列表运行之前。
    void testForceStopWithoutPortFails()
    {
        AgentModel model;
        AgentDefinition def;
        def.id = QStringLiteral("no-port");
        def.webUrl = QStringLiteral("not a url");
        model.insertAgent(0, def);

        AgentRuntime runtime(&model);
        QSignalSpy spy(&runtime, &AgentRuntime::launchFailed);
        runtime.forceStop(QStringLiteral("no-port"));

        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("port")));
    }

    // 端口→PID 解析要能看见真实的监听者：绑定本地端口后，进程列表
    // （netstat/lsof）里应出现本进程自己的 PID。
    void testFindPidsForPortSeesListener()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const int port = server.serverPort();

        const QList<qint64> pids = AgentRuntime::findPidsForPort(port);
        QVERIFY2(pids.contains(QCoreApplication::applicationPid()),
                 "the listening test process must appear for its own port");

        // 无人持有的端口列不出任何 PID（forceStop 的空操作守卫）。
        server.close();
        QVERIFY(AgentRuntime::findPidsForPort(port).isEmpty());
    }

    // launch 真的孵化进程、stop 真的杀掉会话跟踪的进程树（PID 簿记所要求的）。
    void testLaunchAndStopRealProcess()
    {
#ifdef Q_OS_WIN
        const QString command = QStringLiteral("cmd /c ping -n 30 127.0.0.1");
#else
        const QString command = QStringLiteral("sleep 30");
#endif
        if (awb::core::ProcessRunner::findExecutable(
                command.section(QLatin1Char(' '), 0, 0))
                .isEmpty()) {
            QSKIP("no shell available to spawn a long-running process");
        }

        AgentModel model;
        AgentRuntime runtime(&model);
        AgentDefinition def;
        def.id = QStringLiteral("real");
        def.command = command;

        runtime.launch(def, QString());
        // startDetached 是同步的：成功即意味着 PID 已被跟踪。
        QVERIFY2(runtime.hasLaunchedAgents(),
                 "a started process must be tracked for this session");
        QVERIFY(runtime.stop(QStringLiteral("real")));
        QVERIFY(!runtime.hasLaunchedAgents());
    }

    // launch 重定向 agent 输出并捕获其中打印的带鉴权 URL（dsh 会打印每进程
    // 随机的 token URL）：经 sessionUrlChanged 上报，stop 后丢弃。
    void testSessionUrlCapture()
    {
#ifdef Q_OS_WIN
        const QString command =
            QStringLiteral("cmd /c echo dsh web: "
                           "http://127.0.0.1:39877/?token=abc123");
#else
        const QString command =
            QStringLiteral("echo dsh web: http://127.0.0.1:39877/?token=abc123");
#endif
        if (awb::core::ProcessRunner::findExecutable(
                command.section(QLatin1Char(' '), 0, 0))
                .isEmpty()) {
            QSKIP("no shell available to print the URL");
        }

        AgentModel model;
        AgentRuntime runtime(&model);
        QSignalSpy spy(&runtime, &AgentRuntime::sessionUrlChanged);

        AgentDefinition def;
        def.id = QStringLiteral("dsh-test");
        def.command = command;
        def.webUrl = QStringLiteral("http://127.0.0.1:39877");
        runtime.launch(def, QString());

        // 监视每 500 ms 轮询一次输出文件。
        QVERIFY2(spy.wait(10000), "the printed URL must be captured");
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("dsh-test"));
        QCOMPARE(spy.first().at(1).toString(),
                 QStringLiteral("http://127.0.0.1:39877/?token=abc123"));
        QCOMPARE(runtime.sessionUrl(QStringLiteral("dsh-test")),
                 QStringLiteral("http://127.0.0.1:39877/?token=abc123"));

        // 停止即丢弃 URL——每进程的 token 已随进程消亡。
        runtime.stop(QStringLiteral("dsh-test"));
        QVERIFY(runtime.sessionUrl(QStringLiteral("dsh-test")).isEmpty());
    }

    // 不打印 URL 的 agent 让监视安静收场（永不发信号）。
    void testSessionUrlWatchGivesUpQuietly()
    {
#ifdef Q_OS_WIN
        const QString command = QStringLiteral("cmd /c ping -n 30 127.0.0.1");
#else
        const QString command = QStringLiteral("sleep 30");
#endif
        if (awb::core::ProcessRunner::findExecutable(
                command.section(QLatin1Char(' '), 0, 0))
                .isEmpty()) {
            QSKIP("no shell available to spawn a long-running process");
        }

        AgentModel model;
        AgentRuntime runtime(&model);
        QSignalSpy spy(&runtime, &AgentRuntime::sessionUrlChanged);

        AgentDefinition def;
        def.id = QStringLiteral("quiet");
        def.command = command;
        def.webUrl = QStringLiteral("http://127.0.0.1:39878");
        runtime.launch(def, QString());

        // 输出里没有 URL 出现；监视绝不能触发。
        QVERIFY(!spy.wait(2000));
        QVERIFY(runtime.sessionUrl(QStringLiteral("quiet")).isEmpty());

        runtime.stop(QStringLiteral("quiet"));
    }
};

AWB_TEST(TestAgentRuntime)

#include "tst_agentruntime.moc"
