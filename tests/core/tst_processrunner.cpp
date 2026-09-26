#include "awbtest.h"

#include "core/ProcessRunner.h"

#include <QTemporaryDir>
#include <QtTest>

using awb::core::ProcessResult;
using awb::core::ProcessRunner;

class TestProcessRunner : public QObject
{
    Q_OBJECT

private slots:
    // findExecutable applies PATHEXT on Windows, which is what makes
    // npm-style shims like "qwen" -> "qwen.cmd" resolvable.
    void testFindExecutable()
    {
        const QString cmd = ProcessRunner::findExecutable(QStringLiteral("cmd"));
        QVERIFY2(!cmd.isEmpty(), "cmd must be on PATH on Windows");
        QVERIFY(cmd.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
                || cmd.endsWith(QStringLiteral("cmd"), Qt::CaseInsensitive));

        QVERIFY(ProcessRunner::findExecutable(QString()).isEmpty());
        QVERIFY(ProcessRunner::findExecutable(
                    QStringLiteral("no-such-program-awb-xyz")).isEmpty());
    }

    void testRunCapturesOutputAndExitCode()
    {
        const ProcessResult result = ProcessRunner::run(
            QStringLiteral("cmd"),
            {QStringLiteral("/c"), QStringLiteral("echo hello-awb")});
        QVERIFY2(result.started, qPrintable(result.error));
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.exitCode, 0);
        QVERIFY(result.stdOut.contains(QStringLiteral("hello-awb")));
    }

    void testRunExitCode()
    {
        const ProcessResult result = ProcessRunner::run(
            QStringLiteral("cmd"),
            {QStringLiteral("/c"), QStringLiteral("exit /b 3")});
        QVERIFY(result.started);
        QCOMPARE(result.exitCode, 3);
    }

    void testRunMissingProgram()
    {
        const ProcessResult result = ProcessRunner::run(
            QStringLiteral("no-such-program-awb-xyz"), {});
        QVERIFY(!result.started);
        QVERIFY(!result.error.isEmpty());
    }

    void testRunTimeout()
    {
        // ~9 seconds of pings, killed after 300 ms.
        const ProcessResult result = ProcessRunner::run(
            QStringLiteral("cmd"),
            {QStringLiteral("/c"), QStringLiteral("ping -n 10 127.0.0.1 >nul")},
            300);
        QVERIFY(result.started);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.error.contains(QStringLiteral("timed out")));
    }

    void testStartDetached()
    {
        qint64 pid = 0;
        QString error;
        // A quick, harmless command that definitely exists.
        const bool ok = ProcessRunner::startDetached(
            ProcessRunner::findExecutable(QStringLiteral("cmd")),
            {QStringLiteral("/c"), QStringLiteral("exit 0")}, &pid, &error);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(pid > 0);

        QVERIFY(!ProcessRunner::startDetached(
            QStringLiteral("no-such-program-awb-xyz"), {}, nullptr, &error));
        QVERIFY(!error.isEmpty());
    }

    void testDecodeOutput()
    {
        QCOMPARE(ProcessRunner::decodeOutput(QByteArrayLiteral("plain ascii")),
                 QStringLiteral("plain ascii"));
        // UTF-8 with a non-ASCII character.
        QCOMPARE(ProcessRunner::decodeOutput("caf\xc3\xa9"),
                 QString::fromUtf8("caf\xc3\xa9"));
        QVERIFY(ProcessRunner::decodeOutput(QByteArray()).isEmpty());
        // Invalid UTF-8 falls back to the local codec instead of failing.
        QVERIFY(!ProcessRunner::decodeOutput(QByteArray("\x81\x81", 2))
                     .isEmpty());
    }
};

#include "tst_processrunner.moc"
AWB_TEST(TestProcessRunner)
