#include "awbtest.h"

#include "agents/AgentDefinition.h"
#include "agents/AgentModel.h"
#include "agents/AgentRuntime.h"
#include "core/ProcessRunner.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

using awb::agents::AgentDefinition;
using awb::agents::AgentModel;
using awb::agents::AgentRuntime;

// Launch/stop/force-stop bookkeeping and the port→PID parsing behind
// force-stop (specs/03 S2-T4: the logic is kept AND has cases).
class TestAgentRuntime : public QObject
{
    Q_OBJECT

private slots:
    // An empty startup command fails with launchFailed and tracks no PID.
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

    // A program that PATH cannot resolve fails before anything spawns.
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

    // stop() without a tracked PID (agent started elsewhere) reports why.
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

    // forceStop on a webUrl without a usable port fails early — before any
    // process listing runs.
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

    // The port→PID parser sees a real listener: bind a local port, expect
    // this process's own PID from the listing (netstat/lsof).
    void testFindPidsForPortSeesListener()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const int port = server.serverPort();

        const QList<qint64> pids = AgentRuntime::findPidsForPort(port);
        QVERIFY2(pids.contains(QCoreApplication::applicationPid()),
                 "the listening test process must appear for its own port");

        // A port nobody holds lists nothing (forceStop's no-op guard).
        server.close();
        QVERIFY(AgentRuntime::findPidsForPort(port).isEmpty());
    }

    // Launch really spawns and stop really kills the session-tracked tree
    // (the PID bookkeeping S2-T4 requires).
    void testLaunchAndStopRealProcess()
    {
#ifdef Q_OS_WIN
        const QString command = QStringLiteral("cmd /c ping -n 30 127.0.0.1");
#else
        const QString command = QStringLiteral("sleep 30");
#endif
        if (awb::core::ProcessRunner::findExecutable(
                command.section(QLatin1Char(' '), 0, 0))
                .isEmpty())
            QSKIP("no shell available to spawn a long-running process");

        AgentModel model;
        AgentRuntime runtime(&model);
        AgentDefinition def;
        def.id = QStringLiteral("real");
        def.command = command;

        runtime.launch(def, QString());
        // startDetached is synchronous: success means the PID is tracked.
        QVERIFY2(runtime.hasLaunchedAgents(),
                 "a started process must be tracked for this session");
        QVERIFY(runtime.stop(QStringLiteral("real")));
        QVERIFY(!runtime.hasLaunchedAgents());
    }
};

AWB_TEST(TestAgentRuntime)

#include "tst_agentruntime.moc"
