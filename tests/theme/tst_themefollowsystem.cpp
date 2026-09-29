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

/// 测 appearance.followSystem 的跟随语义：系统深浅色 -> 对应变体的内置
/// 基线主题（dark -> mocha-dark、light -> latte-light）、深浅未知回退
/// appearance.theme、关闭后恢复显式选择、setFollowSystem 落盘。系统
/// 深浅色经 setSystemVariantForTesting 注入——本套件的 runner 只有
/// QCoreApplication，真实探测永远不可用。
class TestThemeFollowSystem : public QObject
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
        // 静态注入是进程级的，不清理会漏进同目标的其它用例。
        Theme::setSystemVariantForTesting(QString());
    }

    // 跟随时当前主题由系统深浅色决定（显式选择被搁置）；系统翻转即时
    // 换主题；关闭后恢复 appearance.theme 的显式选择。
    void testFollowsSystemVariant()
    {
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);
        QSignalSpy changed(&theme, &Theme::changed);

        // 显式选浅色，跟随前它生效。
        settings.setThemeId(QStringLiteral("latte-light"));
        QCOMPARE(theme.themeId(), QStringLiteral("latte-light"));

        // 开启跟随：系统是深色 -> mocha-dark（与显式选择不同，证明当前
        // 主题确由系统决定）。
        Theme::setSystemVariantForTesting(QStringLiteral("dark"));
        settings.setFollowSystem(true);
        QCOMPARE(theme.themeId(), QStringLiteral("mocha-dark"));
        QCOMPARE(theme.variant(), QStringLiteral("dark"));
        QVERIFY(changed.count() >= 1);

        // 跟随期间显式选择被搁置：换 appearance.theme 不换当前主题。
        settings.setThemeId(QStringLiteral("mocha-dark"));
        QCOMPARE(theme.themeId(), QStringLiteral("mocha-dark"));
        Theme::setSystemVariantForTesting(QStringLiteral("light"));
        // 系统翻到浅色。真实的触发是 QStyleHints::colorSchemeChanged，
        // 测试里没有 GUI 应用对象，用同为 loadCurrent 入口的
        // appearance.theme 变化触发重算（Settings::setThemeId 无同值短路，
        // 传原值也会重发 valueChanged）。
        settings.setThemeId(QStringLiteral("mocha-dark"));
        QCOMPARE(theme.themeId(), QStringLiteral("latte-light"));
        QCOMPARE(theme.variant(), QStringLiteral("light"));

        // 关闭跟随：回到显式选择（上一步已改成 mocha-dark）。
        settings.setFollowSystem(false);
        QCOMPARE(theme.themeId(), QStringLiteral("mocha-dark"));
    }

    // 深浅不可知（Qt 5、无 QGuiApplication、系统回报 Unknown）时空串，
    // 跟随被降级为回退 appearance.theme——行为与关闭跟随时一致。
    void testUnknownVariantFallsBack()
    {
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);

        Theme::setSystemVariantForTesting(QString());
        settings.setFollowSystem(true);
        // 默认显式选择就是 mocha-dark；先换掉它才能证明回退取的是
        // appearance.theme 而不是深色基线。
        settings.setThemeId(QStringLiteral("latte-light"));
        QCOMPARE(theme.themeId(), QStringLiteral("latte-light"));
    }

    // setFollowSystem 写设置并落盘：新 Settings 实例读得回。
    void testSetFollowSystemPersists()
    {
        {
            Settings settings;
            ThemeRegistry registry;
            Theme theme(&settings, &registry);
            theme.setFollowSystem(true);
            QVERIFY(settings.appearance().followSystem);
            QVERIFY(theme.followSystem());
        }
        Settings reloaded;
        QVERIFY(reloaded.appearance().followSystem);
        QFile::remove(Settings::settingsFilePath());
    }
};

AWB_TEST(TestThemeFollowSystem)
#include "tst_themefollowsystem.moc"
