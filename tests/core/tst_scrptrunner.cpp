#include "awbtest.h"

#include "core/ScriptRunner.h"

#include <QSignalSpy>
#include <QTest>
#include <QtTest>

using awb::core::ScriptRunner;

/// 测 core::ScriptRunner 的脚本执行：输出按序流式到达且 finished 报全文、同键
/// 新运行作废旧运行（epoch 机制）、超时杀进程并报错、启动失败与批处理路径。
class TestScriptRunner : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // 输出按序分块到达，finished() 报完整文本（readyRead 从 QProcess 消费，
    // 由 runner 负责累积）。
    void testStreamingAndFullOutput()
    {
        ScriptRunner runner;
        QSignalSpy chunks(&runner, &ScriptRunner::outputChunk);
        QSignalSpy done(&runner, &ScriptRunner::finished);

        runner.runShell(QStringLiteral("k1"),
                        QStringLiteral("echo alpha&echo beta"), 0, true);
        QVERIFY(done.wait(10000));

        const QList<QVariant> row = done.takeFirst();
        QCOMPARE(row.at(0).toString(), QStringLiteral("k1"));
        QVERIFY(row.at(1).toBool());          // ok
        QCOMPARE(row.at(2).toInt(), 0);        // exit code
        const QString out = row.at(3).toString(); // stdOut
        QVERIFY2(out.contains(QStringLiteral("alpha")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("beta")), qPrintable(out));
        QVERIFY(row.at(5).toString().isEmpty()); // error

        // 分块增量携带同样的内容，顺序一致。
        QString streamed;
        for (const QList<QVariant> &chunk : std::as_const(chunks)) {
            streamed += chunk.at(1).toString();
        }
        QVERIFY(streamed.contains(QStringLiteral("alpha")));
        QVERIFY(streamed.contains(QStringLiteral("beta")));
        QVERIFY(streamed.indexOf(QLatin1Char('a')) >= 0
                && streamed.indexOf(QStringLiteral("alpha"))
                       <= streamed.indexOf(QStringLiteral("beta")));

        // 此后不再出现第二次完成信号。
        QTest::qWait(100);
        QCOMPARE(done.count(), 0);
    }

    // 同键下发起的新运行会作废前一个：旧进程被杀、回调被丢弃——即 epoch 机制。
    void testStaleRunIsInvalidated()
    {
        ScriptRunner runner;
        QSignalSpy done(&runner, &ScriptRunner::finished);

        // 第一次运行挂起约 30 s。
        runner.runShell(QStringLiteral("same"),
                        QStringLiteral("ping -n 30 127.0.0.1 >nul"), 0, true);
        QTest::qWait(200); // 等它先启动起来
        QVERIFY(runner.isRunning(QStringLiteral("same")));

        // 第二次运行接管该键。
        runner.runShell(QStringLiteral("same"),
                        QStringLiteral("echo takeover"), 0, true);
        QVERIFY(done.wait(10000));
        QCOMPARE(done.count(), 1);
        const QList<QVariant> row = done.takeFirst();
        QCOMPARE(row.at(3).toString().contains(QStringLiteral("takeover")),
                 true);

        // 被杀掉的第一次运行绝不再回报。
        QTest::qWait(300);
        QCOMPARE(done.count(), 0);
        QVERIFY(!runner.isRunning(QStringLiteral("same")));
    }

    // 挂起的命令在超时处被杀，并以带可读原因的失败回报。
    void testTimeout()
    {
        ScriptRunner runner;
        QSignalSpy done(&runner, &ScriptRunner::finished);

        runner.runShell(QStringLiteral("slow"),
                        QStringLiteral("ping -n 10 127.0.0.1 >nul"), 300, true);
        QVERIFY(done.wait(10000));
        const QList<QVariant> row = done.takeFirst();
        QVERIFY(!row.at(1).toBool()); // not ok
        QVERIFY2(row.at(5).toString().contains(QStringLiteral("timed out")),
                 qPrintable(row.at(5).toString()));
    }

    // 根本没启动起来的命令报启动失败，而不是超时。
    void testStartFailure()
    {
        ScriptRunner runner;
        QSignalSpy done(&runner, &ScriptRunner::finished);

        runner.run(QStringLiteral("bad"), QStringLiteral("no-such-prog-awb-xyz"),
                   {}, 0, true);
        // FailedToStart 可能从 run() 内部同步上报，早于 wait() 进入事件循环
        // ——改用轮询计数来等。
        QTRY_VERIFY_WITH_TIMEOUT(done.count() >= 1, 10000);
        const QList<QVariant> row = done.takeFirst();
        QVERIFY(!row.at(1).toBool());
        QCOMPARE(row.at(2).toInt(), -1);
        QVERIFY2(row.at(5).toString().contains(QStringLiteral("failed to start")),
                 qPrintable(row.at(5).toString()));
    }

    // runBatch 写一个临时 .cmd（绕开 cmd.exe 的引号转义）再执行它。
    void testBatch()
    {
        ScriptRunner runner;
        QSignalSpy done(&runner, &ScriptRunner::finished);

        runner.runBatch(QStringLiteral("batch"), QStringLiteral("echo batch-ok"),
                        10000);
        QVERIFY(done.wait(10000));
        const QList<QVariant> row = done.takeFirst();
        QVERIFY2(row.at(1).toBool(), qPrintable(row.at(5).toString()));
        QVERIFY(row.at(3).toString().contains(QStringLiteral("batch-ok")));
    }
};

#include "tst_scrptrunner.moc"
#include <utility>
AWB_TEST(TestScriptRunner)
