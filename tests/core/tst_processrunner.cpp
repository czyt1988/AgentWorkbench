#include "awbtest.h"

#include "core/ProcessRunner.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::ProcessResult;
using awb::core::ProcessRunner;

/// 测 core::ProcessRunner：findExecutable 的 PATHEXT 解析、同步 run 的输出与
/// 退出码捕获、超时与缺程序失败、startDetached 及其输出重定向、输出解码回退。
class TestProcessRunner : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // findExecutable 在 Windows 上要应用 PATHEXT——正是它让 npm 风格的
    // 垫片 "qwen" → "qwen.cmd" 能被解析到。
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
        // 约 9 秒的 ping，300 ms 后被杀掉。
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
        // 一条快而无害、且必然存在的命令。
        const bool ok = ProcessRunner::startDetached(
            ProcessRunner::findExecutable(QStringLiteral("cmd")),
            {QStringLiteral("/c"), QStringLiteral("exit 0")}, &pid, &error);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(pid > 0);

        QVERIFY(!ProcessRunner::startDetached(
            QStringLiteral("no-such-program-awb-xyz"), {}, nullptr, &error));
        QVERIFY(!error.isEmpty());
    }

    // 指定 outputFile 时，分离子进程的 stdout（含合并的 stderr）落到该文件
    // ——agents 域就是读这份捕获来提取会话 URL 的。
    void testStartDetachedRedirectsOutput()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString outFile = tmp.path() + QStringLiteral("/out.log");
        qint64 pid = 0;
        QString error;
        const bool ok = ProcessRunner::startDetached(
            ProcessRunner::findExecutable(QStringLiteral("cmd")),
            {QStringLiteral("/c"), QStringLiteral("echo hello-redirect")},
            &pid, &error, QString(), QProcessEnvironment(), outFile);
        QVERIFY2(ok, qPrintable(error));

        // 分离子进程是异步写的——短暂轮询等待内容出现。
        QString text;
        for (int i = 0; i < 100 && !text.contains(QStringLiteral("hello-redirect"));
             ++i) {
            QFile f(outFile);
            if (f.open(QIODevice::ReadOnly)) {
                text = ProcessRunner::decodeOutput(f.readAll());
            }
            if (!text.contains(QStringLiteral("hello-redirect"))) {
                QTest::qWait(50);
            }
        }
        QVERIFY(text.contains(QStringLiteral("hello-redirect")));
        // 让子进程彻底退出后 QTemporaryDir 再清理。
        QTest::qWait(300);
    }

    void testDecodeOutput()
    {
        QCOMPARE(ProcessRunner::decodeOutput(QByteArrayLiteral("plain ascii")),
                 QStringLiteral("plain ascii"));
        // 带非 ASCII 字符的 UTF-8。
        QCOMPARE(ProcessRunner::decodeOutput("caf\xc3\xa9"),
                 QString::fromUtf8("caf\xc3\xa9"));
        QVERIFY(ProcessRunner::decodeOutput(QByteArray()).isEmpty());
        // 非法 UTF-8 回退到本地编码而不是失败。
        QVERIFY(!ProcessRunner::decodeOutput(QByteArray("\x81\x81", 2))
                     .isEmpty());
    }
};

#include "tst_processrunner.moc"
AWB_TEST(TestProcessRunner)
