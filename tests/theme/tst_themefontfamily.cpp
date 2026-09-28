#include "awbtest.h"

#include "core/Settings.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

using awb::core::Settings;
using awb::theme::Theme;
using awb::theme::ThemeRegistry;

// Theme::family 的优先级（设置覆盖 → 主题 JSON → 空串）与运行时切换
// （setFontFamily 写设置、changed 信号驱动 QML 令牌重绑）。
class TestThemeFontFamily : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        // 干净的 settings.json：load 取默认值，不读上一个用例的残留。
        QDir().mkpath(QFileInfo(Settings::settingsFilePath()).absolutePath());
        QFile::remove(Settings::settingsFilePath());
    }

    // 默认（无覆盖）：family 取设置的默认值微软雅黑；内置主题 JSON 的
    // fonts.family 是空串，不参与。
    void testDefaultIsYaHei()
    {
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);
        QCOMPARE(settings.fontFamily(), QStringLiteral("Microsoft YaHei"));
        QCOMPARE(theme.family(), QStringLiteral("Microsoft YaHei"));
    }

    // 优先级：非空设置覆盖一切；清空后回退到主题 JSON（内置为空）。
    void testOverridePriority()
    {
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);
        QSignalSpy changed(&theme, &Theme::changed);

        settings.setFontFamily(QStringLiteral("SimSun"));
        QCOMPARE(theme.family(), QStringLiteral("SimSun"));
        QVERIFY(changed.count() >= 1);

        settings.setFontFamily(QString());
        // 内置主题没有声明 family：空 = 跟随系统默认。
        QVERIFY(theme.family().isEmpty());
    }

    // setFontFamily：写设置 + 落盘，重启（新 Settings 实例）后仍是新值。
    void testSetFontFamilyPersists()
    {
        {
            Settings settings;
            ThemeRegistry registry;
            Theme theme(&settings, &registry);
            theme.setFontFamily(QStringLiteral("Segoe UI"));
            QCOMPARE(settings.fontFamily(), QStringLiteral("Segoe UI"));
            QCOMPARE(theme.family(), QStringLiteral("Segoe UI"));
        }
        Settings reloaded;
        QCOMPARE(reloaded.fontFamily(), QStringLiteral("Segoe UI"));

        // fontFamilies() 不在此断言：QFontDatabase 要走平台集成，
        // 本套件的 runner 只有 QCoreApplication（见 awbtest_runner）；
        // 该属性由应用的设置页绑定覆盖（QGuiApplication 之下）。
    }
};

AWB_TEST(TestThemeFontFamily)
#include "tst_themefontfamily.moc"
