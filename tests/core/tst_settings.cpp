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
// Capture warnings so "invalid value → default + warn" can be asserted.
QStringList g_captured;
void captureMessage(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    g_captured.append(msg);
}

void writeSettingsFile(const QByteArray &json)
{
    QDir().mkpath(QFileInfo(Settings::settingsFilePath()).absolutePath());
    QFile f(Settings::settingsFilePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(json);
}
} // namespace

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
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

    // No file yet: every value is the documented default,
    // and save() writes a complete file.
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

        // Reading the saved file back yields the same values.
        Settings reloaded;
        QCOMPARE(reloaded.window().width, 1440);
        QCOMPARE(reloaded.themeId(), QStringLiteral("mocha-dark"));
    }

    // Setters + save() round-trip through the file.
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

    // Missing keys take defaults, values of the wrong type take defaults and
    // are reported.
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
                || msg.contains(QStringLiteral("web.surface")))
                warned = true;
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
            if (msg.contains(QStringLiteral("logging.level")))
                warned = true;
        }
        QVERIFY2(warned, "an invalid log level must be logged as a warning");
    }

    // Unknown keys are ignored with a warning, never fatal.
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
            if (msg.contains(QStringLiteral("bogusSection")))
                warned = true;
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
