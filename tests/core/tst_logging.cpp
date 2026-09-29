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

// 本文件专用的日志分类，用于验证按分类写前缀的可过滤性
Q_LOGGING_CATEGORY(lcAwbTest, "awb.test")

/// 测 core::Logging：命令行按需加引号的格式化与输出截断、日志按大小轮转并
/// 限文件数、分类前缀、级别过滤、AWB_* 事件宏、非 ASCII 路径与多线程并发写。
class TestLogging : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // 命令行只在必要处加引号：日志里看到的是真实命令，且能原样粘回 cmd.exe。
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
        // 空参数要保持可见，不能凭空消失。
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

    // 日志在大小上限处轮转且至多保留这么多个文件，话痨安装也填不满磁盘。
    void testLogRotation()
    {
        QCOMPARE(Logging::DEFAULT_MAX_FILES, 3);
        QCOMPARE(Logging::DEFAULT_MAX_FILE_SIZE, qint64(5 * 1024 * 1024));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // 文件设得很小，不写几 MB 也能触发轮转；一行就已超限，所以每次
        // 写入都会轮转。
        Logging::install(dir.path(), 128, 3);
        for (int i = 0; i < 20; ++i) {
            qInfo().noquote() << QStringLiteral("rotation line %1").arg(i);
        }
        Logging::uninstall(); // 把消息处理器交还给 QTest

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
        // 最新的行活了下来（最后一次写入可能已把它轮转进 .1），
        // 最老的行被彻底丢弃。
        QVERIFY(logged.contains(QStringLiteral("rotation line 19")));
        QVERIFY(!logged.contains(QStringLiteral("rotation line 0")));
    }

    // 各模块经自己的 Qt 分类（"awb.<module>"）打日志，处理器把它写成行前缀，
    // 日志才能按模块过滤。
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
                for (int i = 0; i < kPerThread; ++i) {
                    qInfo().noquote()
                        << QStringLiteral("concurrent line %1/%2").arg(t).arg(i);
                }
            });
        }
        for (std::thread &producer : producers) {
            producer.join();
        }
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
