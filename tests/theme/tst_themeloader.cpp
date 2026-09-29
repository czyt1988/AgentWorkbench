#include "awbtest.h"

#include "theme/ThemeLoader.h"
#include "theme/ThemeRegistry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using awb::theme::ThemeFile;
using awb::theme::ThemeLoader;
using awb::theme::ThemeRegistry;

/// 测 theme::ThemeLoader 的主题 JSON 解析：未知键忽略、缺 token 回退同变体
/// 基线、坏颜色回退、id 与文件名必须一致、variant 只认 dark/light，以及内置
/// 主题资源的加载。
class TestThemeLoader : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        // 内置主题来自 :/themes（嵌入在本测试目标里）。
        m_baseline = ThemeRegistry().baseline(QStringLiteral("dark"));
        QVERIFY(m_baseline.isValid());
    }

    // 未知键被忽略并告警，绝不致命。
    void testUnknownKeysIgnored()
    {
        QJsonObject json = baseJson();
        json[QStringLiteral("totallyUnknown")] = 1;
        QJsonObject colors = json[QStringLiteral("colors")].toObject();
        colors[QStringLiteral("notAToken")] = QStringLiteral("#ff0000");
        json[QStringLiteral("colors")] = colors;

        ThemeFile out;
        QVERIFY(ThemeLoader::parse(json, QStringLiteral("test-dark"),
                                   m_baseline, out));
        QVERIFY(out.isValid());
        QVERIFY(!out.colors.contains(QStringLiteral("notAToken")));
        // 真正的 token 照常落位。
        QVERIFY(out.colors.contains(QStringLiteral("accent")));
    }

    // 缺失的 token 回退到同一变体的基线——用户主题只声明自己要改的部分。
    void testMissingTokensFallBackToBaseline()
    {
        QJsonObject json; // 只有 id/name/variant，完全没有 colors/metrics
        json[QStringLiteral("id")] = QStringLiteral("test-dark");
        json[QStringLiteral("name")] = QStringLiteral("Test Dark");
        json[QStringLiteral("variant")] = QStringLiteral("dark");

        ThemeFile out;
        QVERIFY(ThemeLoader::parse(json, QStringLiteral("test-dark"),
                                   m_baseline, out));
        QCOMPARE(out.colors.value(QStringLiteral("accent")),
                 m_baseline.colors.value(QStringLiteral("accent")));
        QCOMPARE(out.metrics.value(QStringLiteral("radiusCard")),
                 m_baseline.metrics.value(QStringLiteral("radiusCard")));
        QCOMPARE(out.fonts.value(QStringLiteral("monoFamily")),
                 m_baseline.fonts.value(QStringLiteral("monoFamily")));
        QCOMPARE(out.agentPalette, m_baseline.agentPalette);
    }

    // 解析不了的颜色回退到基线。
    void testInvalidColorFallsBack()
    {
        QJsonObject json = baseJson();
        QJsonObject colors;
        colors[QStringLiteral("accent")] = QStringLiteral("not-a-color");
        json[QStringLiteral("colors")] = colors;

        ThemeFile out;
        QVERIFY(ThemeLoader::parse(json, QStringLiteral("test-dark"),
                                   m_baseline, out));
        QCOMPARE(out.colors.value(QStringLiteral("accent")),
                 m_baseline.colors.value(QStringLiteral("accent")));
    }

    // `id` 缺失或不等于文件名 → 整个文件被跳过。
    void testIdMustMatchFileName()
    {
        QJsonObject json = baseJson();
        json[QStringLiteral("id")] = QStringLiteral("something-else");
        ThemeFile out;
        QVERIFY(!ThemeLoader::parse(json, QStringLiteral("test-dark"),
                                    m_baseline, out));
        QVERIFY(!out.isValid());

        QJsonObject missing = baseJson();
        missing.remove(QStringLiteral("id"));
        QVERIFY(!ThemeLoader::parse(missing, QStringLiteral("test-dark"),
                                    m_baseline, out));
        QVERIFY(!out.isValid());
    }

    // 未知的 variant 不可用。
    void testVariantMustBeDarkOrLight()
    {
        QJsonObject json = baseJson();
        json[QStringLiteral("variant")] = QStringLiteral("sepia");
        ThemeFile out;
        QVERIFY(!ThemeLoader::parse(json, QStringLiteral("test-dark"),
                                    m_baseline, out));
    }

    void testLoadBuiltinFromResources()
    {
        const ThemeFile dark =
            ThemeLoader::loadFile(QStringLiteral(":/themes/mocha-dark.json"),
                                  ThemeFile());
        QVERIFY(dark.isValid());
        QCOMPARE(dark.id, QStringLiteral("mocha-dark"));
        QCOMPARE(dark.variant, QStringLiteral("dark"));
        QCOMPARE(dark.colors.size(), 33);

        const ThemeFile light =
            ThemeLoader::loadFile(QStringLiteral(":/themes/latte-light.json"),
                                  ThemeFile());
        QVERIFY(light.isValid());
        QCOMPARE(light.variant, QStringLiteral("light"));
    }

private:
    /**
     * @brief 构造最小合法的暗色主题 JSON（id/name/variant 三键）
     *
     * @return 可直接塞进 ThemeLoader::parse 的根对象
     */
    static QJsonObject baseJson()
    {
        QJsonObject json;
        json[QStringLiteral("id")] = QStringLiteral("test-dark");
        json[QStringLiteral("name")] = QStringLiteral("Test Dark");
        json[QStringLiteral("variant")] = QStringLiteral("dark");
        return json;
    }

    ThemeFile m_baseline;  ///< init() 里取到的暗色基线，供回退断言比对
};

#include "tst_themeloader.moc"
AWB_TEST(TestThemeLoader)
