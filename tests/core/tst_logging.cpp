#include "awbtest.h"

#include "core/Logging.h"
#include "core/TextUtils.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QtTest>

#include <thread>
#include <vector>

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

    // 级别过滤：低于配置级别的消息不进文件，其余照常。
    void testLevelFiltering()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        Logging::install(dir.path(), Logging::DEFAULT_MAX_FILE_SIZE,
                         Logging::DEFAULT_MAX_FILES, QStringLiteral("warning"));
        qInfo().noquote() << QStringLiteral("filtered info line");
        qWarning().noquote() << QStringLiteral("kept warning line");
        qCritical().noquote() << QStringLiteral("kept critical line");
        Logging::uninstall();

        QFile f(dir.path() + QStringLiteral("/agentworkbench.log"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains(QStringLiteral("kept warning line")),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("kept critical line")),
                 qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("filtered info line")),
                 qPrintable(text));
    }

    // 事件宏：AWB_* 绑定 awb.event 分类（写进行前缀），级别由宏名给出。
    void testEventMacros()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        Logging::install(dir.path());
        AWB_INFO << QStringLiteral("an event happened");
        AWB_WARNING << QStringLiteral("a worrying event");
        Logging::uninstall();

        QFile f(dir.path() + QStringLiteral("/agentworkbench.log"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains(QStringLiteral("[awb.event]")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("an event happened")));
        QVERIFY(text.contains(QStringLiteral("a worrying event")));
        // 级别由宏名给出：AWB_INFO 落成 [INFO]，AWB_WARNING 落成 [WARNING]。
        QVERIFY2(text.contains(QStringLiteral("[INFO] [awb.event]")),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("[WARNING] [awb.event]")),
                 qPrintable(text));
    }

    // 非 ASCII 路径回归：中文目录下的日志文件必须能打开并落盘——当年否决
    // spdlog 的原因就是窄字符 fopen 打不开这类路径（SPDLOG_WCHAR_FILENAMES
    // 是这里能过的前提）。
    void testNonAsciiLogPath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString logDir = dir.path() + QStringLiteral("/日志目录");
        QVERIFY(QDir().mkpath(logDir));

        Logging::install(logDir);
        qInfo().noquote() << QStringLiteral("written under a chinese path");
        Logging::uninstall();

        QFile f(logDir + QStringLiteral("/agentworkbench.log"));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains(QStringLiteral("written under a chinese path")),
                 qPrintable(text));
    }

    // 并发生产者：多线程同时写日志，uninstall 排空后一条不丢、一条不重。
    // 4×500=2000 条远小于 8192 的队列深度，不会触发丢最旧的溢出策略，
    // 所以行数是确定值。
    void testConcurrentProducers()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Logging::install(dir.path());

        constexpr int kThreads = 4;
        constexpr int kPerThread = 500;
        std::vector<std::thread> producers;
        for (int t = 0; t < kThreads; ++t) {
            producers.emplace_back([t, kPerThread] {
                for (int i = 0; i < kPerThread; ++i)
                    qInfo().noquote()
                        << QStringLiteral("concurrent line %1/%2").arg(t).arg(i);
            });
        }
        for (std::thread &producer : producers)
            producer.join();
        Logging::uninstall();

        QFile f(dir.path() + QStringLiteral("/agentworkbench.log"));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
        const QString text = QString::fromUtf8(f.readAll());
        int count = 0;
        int from = 0;
        const QString marker = QStringLiteral("concurrent line ");
        while ((from = text.indexOf(marker, from)) != -1) {
            ++count;
            from += marker.size();
        }
        QCOMPARE(count, kThreads * kPerThread);
    }
};

#include "tst_logging.moc"
AWB_TEST(TestLogging)
