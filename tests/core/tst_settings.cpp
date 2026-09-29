#include "awbtest.h"

#include "core/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

using awb::core::Settings;

namespace {

/// 捕获到的 Qt 日志消息，配合 qInstallMessageHandler 断言「非法值 → 取默认 + 告警」。
QStringList g_captured;

/**
 * @brief 安装给 qInstallMessageHandler 的消息处理器，把日志追加进 g_captured
 *
 * @param type 消息级别（本测试不区分，忽略）
 * @param context 日志上下文（忽略）
 * @param msg 日志正文
 */
void captureMessage(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    g_captured.append(msg);
}

/**
 * @brief 用给定 JSON 覆盖当前的 settings.json
 *
 * 测试「从文件读配置」的用例需要精确控制文件内容，而 Settings 的路径来自
 * 测试模式下的标准位置，这里直接往该路径写文件（先建父目录再截断重写）。
 *
 * @param json 完整的 settings.json 文件内容
 */
void writeSettingsFile(const QByteArray &json)
{
    QDir().mkpath(QFileInfo(Settings::settingsFilePath()).absolutePath());
    QFile f(Settings::settingsFilePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(json);
}
} // namespace

/// 测 core::Settings：无文件时的默认值、setter 经 save() 的文件往返、缺键与
/// 非法值的降级告警、未知键忽略、valueChanged 信号，以及日志选项的读写。
class TestSettings : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir().mkpath(QFileInfo(Settings::settingsFilePath()).absolutePath());
        QFile::remove(Settings::settingsFilePath());
    }

    void cleanup()
    {
        QFile::remove(Settings::settingsFilePath());
    }

    // 文件尚不存在：每个值都是文档默认值，且 save() 写出完整文件。
    void testDefaults()
    {
        Settings s;
        QCOMPARE(s.window().width, 1440);
        QCOMPARE(s.window().height, 900);
        QCOMPARE(s.window().sidebarWidth, 240);
        QVERIFY(!s.window().sidebarCollapsed);
        QCOMPARE(s.window().lastPageId, QStringLiteral("agents"));
        QCOMPARE(s.themeId(), QStringLiteral("mocha-dark"));
        QCOMPARE(s.fontFamily(), QStringLiteral("Microsoft YaHei"));
        QCOMPARE(s.launcherOptions().healthCheckIntervalMs, 3000);
        QVERIFY(s.launcherOptions().startupVersionCheck);
        QCOMPARE(s.webOptions().surface, QStringLiteral("embedded"));
        QVERIFY(!s.webOptions().freezeInactiveTabs);
        QCOMPARE(s.webOptions().maxLiveTabs, 8);
        QVERIFY(s.skillsOptions().includePluginCaches);
        QCOMPARE(s.skillsOptions().maxDepth, 6);
        QCOMPARE(s.loggingOptions().maxFileSize, qint64(5 * 1024 * 1024));
        QCOMPARE(s.loggingOptions().maxFiles, 3);
        QCOMPARE(s.loggingOptions().level, QStringLiteral("debug"));
        QVERIFY(s.loggingOptions().mirrorToStderr);
        QVERIFY(!s.pluginsOptions().enabled);

        QVERIFY(s.save().ok);
        QVERIFY(QFile::exists(Settings::settingsFilePath()));

        // 把保存后的文件重新读入，值应与写入前一致。
        Settings reloaded;
        QCOMPARE(reloaded.window().width, 1440);
        QCOMPARE(reloaded.themeId(), QStringLiteral("mocha-dark"));
    }

    // setter 经 save() 写文件再读回，验证整条往返链。
    void testRoundTrip()
    {
        {
            Settings s;
            s.setThemeId(QStringLiteral("latte-light"));
            s.setFontFamily(QStringLiteral("SimSun"));
            s.setWindowTitle(QStringLiteral("My Bench"));
            s.setWindowSize(1024, 768);
            s.setSidebarCollapsed(true);
            s.setLastPageId(QStringLiteral("web"));
            QVERIFY(s.save().ok);
        }
        Settings again;
        QCOMPARE(again.themeId(), QStringLiteral("latte-light"));
        QCOMPARE(again.fontFamily(), QStringLiteral("SimSun"));
        QCOMPARE(again.windowTitle(), QStringLiteral("My Bench"));
        QCOMPARE(again.window().width, 1024);
        QCOMPARE(again.window().height, 768);
        QVERIFY(again.window().sidebarCollapsed);
        QCOMPARE(again.window().lastPageId, QStringLiteral("web"));
    }

    // 缺键取默认值；类型不对的值取默认并记告警。
    void testPartialAndInvalidValues()
    {
        writeSettingsFile(R"({
            "window": { "width": "abc", "height": 800 },
            "appearance": { "theme": 123 },
            "web": { "surface": "quantum" }
        })");

        g_captured.clear();
        const QtMessageHandler previous =
            qInstallMessageHandler(&captureMessage);
        Settings s;
        qInstallMessageHandler(previous);

        QCOMPARE(s.window().width, 1440);          // invalid -> default
        QCOMPARE(s.window().height, 800);          // valid   -> kept
        QCOMPARE(s.themeId(), QStringLiteral("mocha-dark")); // invalid -> default
        QCOMPARE(s.webOptions().surface, QStringLiteral("embedded"));

        bool warned = false;
        for (const QString &msg : std::as_const(g_captured)) {
            if (msg.contains(QStringLiteral("window.width"))
                || msg.contains(QStringLiteral("web.surface"))) {
                warned = true;
            }
        }
        QVERIFY2(warned, "invalid values must be logged as warnings");
    }

    // 日志选项：自定义的级别与镜像开关读得进、save() 存得出；非法级别
    // 告警后回默认值。
    void testLoggingOptions()
    {
        writeSettingsFile(R"({ "logging": { "level": "warning",
                                            "mirrorToStderr": false } })");
        Settings s;
        QCOMPARE(s.loggingOptions().level, QStringLiteral("warning"));
        QVERIFY(!s.loggingOptions().mirrorToStderr);
        QVERIFY(s.save().ok);

        // save() 把两个新键原样写回了文件。
        QFile f(Settings::settingsFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonObject logging =
            root.value(QStringLiteral("logging")).toObject();
        QCOMPARE(logging.value(QStringLiteral("level")).toString(),
                 QStringLiteral("warning"));
        QVERIFY(!logging.value(QStringLiteral("mirrorToStderr")).toBool());

        // 非法级别：告警并回默认。
        writeSettingsFile(R"({ "logging": { "level": "loud" } })");
        g_captured.clear();
        const QtMessageHandler previous =
            qInstallMessageHandler(&captureMessage);
        Settings invalid;
        qInstallMessageHandler(previous);
        QCOMPARE(invalid.loggingOptions().level, QStringLiteral("debug"));
        bool warned = false;
        for (const QString &msg : std::as_const(g_captured)) {
            if (msg.contains(QStringLiteral("logging.level"))) {
                warned = true;
            }
        }
        QVERIFY2(warned, "an invalid log level must be logged as a warning");
    }

    // 未知键被忽略并告警，绝不致命。
    void testUnknownKeyWarns()
    {
        writeSettingsFile(R"({ "bogusSection": 1 })");

        g_captured.clear();
        const QtMessageHandler previous =
            qInstallMessageHandler(&captureMessage);
        Settings s;
        qInstallMessageHandler(previous);
        Q_UNUSED(s)

        bool warned = false;
        for (const QString &msg : std::as_const(g_captured)) {
            if (msg.contains(QStringLiteral("bogusSection"))) {
                warned = true;
            }
        }
        QVERIFY2(warned, "an unknown key must be logged as a warning");
    }

    void testValueChangedSignal()
    {
        Settings s;
        QSignalSpy spy(&s, &Settings::valueChanged);
        s.setThemeId(QStringLiteral("latte-light"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(),
                 QStringLiteral("appearance.theme"));

        s.setSidebarCollapsed(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(),
                 QStringLiteral("window.sidebarCollapsed"));
    }
};

#include "tst_settings.moc"
AWB_TEST(TestSettings)
