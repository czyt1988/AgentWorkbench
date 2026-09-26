#include "awbtest.h"

#include "core/ScriptRunner.h"

#include <QSignalSpy>
#include <QTest>
#include <QtTest>

using awb::core::ScriptRunner;

class TestScriptRunner : public QObject
{
    Q_OBJECT

private slots:
    // Output arrives as chunks in order, and finished() reports the complete
    // text (readyRead consumes from QProcess, so the runner accumulates).
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

        // Chunks carry the same bytes incrementally, in order.
        QString streamed;
        for (const QList<QVariant> &chunk : chunks)
            streamed += chunk.at(1).toString();
        QVERIFY(streamed.contains(QStringLiteral("alpha")));
        QVERIFY(streamed.contains(QStringLiteral("beta")));
        QVERIFY(streamed.indexOf(QLatin1Char('a')) >= 0
                && streamed.indexOf(QStringLiteral("alpha"))
                       <= streamed.indexOf(QStringLiteral("beta")));

        // No second completion shows up afterwards.
        QTest::qWait(100);
        QCOMPARE(done.count(), 0);
    }

    // Starting a new run under the same key invalidates the previous one:
    // its process is killed and its callbacks are dropped — the "epoch"
    // mechanism (specs/01 §4.1).
    void testStaleRunIsInvalidated()
    {
        ScriptRunner runner;
        QSignalSpy done(&runner, &ScriptRunner::finished);

        // First run hangs for ~30 s.
        runner.runShell(QStringLiteral("same"),
                        QStringLiteral("ping -n 30 127.0.0.1 >nul"), 0, true);
        QTest::qWait(200); // let it start
        QVERIFY(runner.isRunning(QStringLiteral("same")));

        // Second run takes over the key.
        runner.runShell(QStringLiteral("same"),
                        QStringLiteral("echo takeover"), 0, true);
        QVERIFY(done.wait(10000));
        QCOMPARE(done.count(), 1);
        const QList<QVariant> row = done.takeFirst();
        QCOMPARE(row.at(3).toString().contains(QStringLiteral("takeover")),
                 true);

        // The killed first run never reports back.
        QTest::qWait(300);
        QCOMPARE(done.count(), 0);
        QVERIFY(!runner.isRunning(QStringLiteral("same")));
    }

    // A hung command is killed at the timeout and reported as failed with a
    // readable error.
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

    // A command that never starts reports a start failure, not a timeout.
    void testStartFailure()
    {
        ScriptRunner runner;
        QSignalSpy done(&runner, &ScriptRunner::finished);

        runner.run(QStringLiteral("bad"), QStringLiteral("no-such-prog-awb-xyz"),
                   {}, 0, true);
        // FailedToStart can be reported synchronously from within run(),
        // before wait() would start its event loop — poll the count instead.
        QTRY_VERIFY_WITH_TIMEOUT(done.count() >= 1, 10000);
        const QList<QVariant> row = done.takeFirst();
        QVERIFY(!row.at(1).toBool());
        QCOMPARE(row.at(2).toInt(), -1);
        QVERIFY2(row.at(5).toString().contains(QStringLiteral("failed to start")),
                 qPrintable(row.at(5).toString()));
    }

    // runBatch writes a temp .cmd (sidestepping cmd.exe quoting) and runs it.
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
AWB_TEST(TestScriptRunner)
