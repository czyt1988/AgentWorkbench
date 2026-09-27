#include "awbtest.h"

#include "core/Logging.h"
#include "core/TextUtils.h"

#include <QDir>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::Logging;

Q_LOGGING_CATEGORY(lcAwbTest, "awb.test")

class TestLogging : public QObject
{
    Q_OBJECT

private slots:
    // A command line is quoted only where it has to be, so the log shows the
    // real thing and it can still be pasted back into cmd.exe.
    void testFormatCommandLine()
    {
        QCOMPARE(Logging::formatCommandLine(QStringLiteral("qwen"),
                                            {QStringLiteral("serve")}),
                 QStringLiteral("qwen serve"));
        QCOMPARE(Logging::formatCommandLine(
                     QStringLiteral("cmd"),
                     {QStringLiteral("/c"),
                      QStringLiteral("C:/Program Files/qwen.cmd"),
                      QStringLiteral("serve")}),
                 QStringLiteral("cmd /c \"C:/Program Files/qwen.cmd\" serve"));
        // An empty argument stays visible instead of collapsing into nothing.
        QCOMPARE(Logging::formatCommandLine(QStringLiteral("x"), {QString()}),
                 QStringLiteral("x \"\""));
    }

    void testClampOutput()
    {
        const QString text(100, QLatin1Char('a'));
        QCOMPARE(Logging::clampOutput(text, 200), text);

        const QString clamped = Logging::clampOutput(text, 10);
        QVERIFY(clamped.startsWith(QStringLiteral("aaaaaaaaaa")));
        QVERIFY(clamped.contains(QStringLiteral("90")));
    }

    // The log rotates at the size limit and keeps at most that many files, so
    // a chatty install can never fill the disk.
    void testLogRotation()
    {
        QCOMPARE(Logging::DEFAULT_MAX_FILES, 3);
        QCOMPARE(Logging::DEFAULT_MAX_FILE_SIZE, qint64(5 * 1024 * 1024));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Tiny files so rotation happens without writing megabytes; one line
        // is already bigger than the limit, so every write rotates.
        Logging::install(dir.path(), 128, 3);
        for (int i = 0; i < 20; ++i)
            qInfo().noquote() << QStringLiteral("rotation line %1").arg(i);
        Logging::uninstall(); // hand the message handler back to QTest

        const QDir logDir(dir.path());
        const QStringList files = logDir.entryList(
            {QStringLiteral("agentworkbench.log*")}, QDir::Files, QDir::Name);
        QCOMPARE(files, QStringList({QStringLiteral("agentworkbench.log"),
                                     QStringLiteral("agentworkbench.log.1"),
                                     QStringLiteral("agentworkbench.log.2")}));

        QString logged;
        for (const QString &name : files) {
            QFile f(logDir.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            logged += QString::fromUtf8(f.readAll());
        }
        // The newest line survived (the last write may have rotated it into
        // .1 already), and the oldest ones were dropped for good.
        QVERIFY(logged.contains(QStringLiteral("rotation line 19")));
        QVERIFY(!logged.contains(QStringLiteral("rotation line 0")));
    }

    // Modules log through their own Qt category ("awb.<module>") and the
    // handler writes it as a line prefix so the log is filterable per module
    void testCategoryPrefix()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        Logging::install(dir.path());
        qCInfo(lcAwbTest) << "categorized hello";
        qInfo().noquote() << "plain hello";
        Logging::uninstall();

        QFile f(dir.path() + QStringLiteral("/agentworkbench.log"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains(QStringLiteral("[awb.test]")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("categorized hello")));
        QVERIFY(text.contains(QStringLiteral("plain hello")));
    }
};

#include "tst_logging.moc"
AWB_TEST(TestLogging)
