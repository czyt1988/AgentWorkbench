#include "awbtest.h"

#include "theme/ThemeLoader.h"
#include "theme/ThemeRegistry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using awb::theme::ThemeFile;
using awb::theme::ThemeLoader;
using awb::theme::ThemeRegistry;

class TestThemeLoader : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        // Builtin themes come from :/themes (embedded in this test target).
        m_baseline = ThemeRegistry().baseline(QStringLiteral("dark"));
        QVERIFY(m_baseline.isValid());
    }

    // Unknown keys are ignored with a warning, never fatal.
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
        // Real tokens still land.
        QVERIFY(out.colors.contains(QStringLiteral("accent")));
    }

    // Missing tokens fall back to the baseline of the SAME variant — a user
    // theme only declares what it changes.
    void testMissingTokensFallBackToBaseline()
    {
        QJsonObject json; // only id/name/variant, no colors/metrics at all
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

    // An unparseable color falls back to the baseline.
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

    // `id` missing or != file name -> the whole file is skipped.
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

    // An unknown variant is unusable.
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
        QCOMPARE(dark.colors.size(), 32);

        const ThemeFile light =
            ThemeLoader::loadFile(QStringLiteral(":/themes/latte-light.json"),
                                  ThemeFile());
        QVERIFY(light.isValid());
        QCOMPARE(light.variant, QStringLiteral("light"));
    }

private:
    static QJsonObject baseJson()
    {
        QJsonObject json;
        json[QStringLiteral("id")] = QStringLiteral("test-dark");
        json[QStringLiteral("name")] = QStringLiteral("Test Dark");
        json[QStringLiteral("variant")] = QStringLiteral("dark");
        return json;
    }

    ThemeFile m_baseline;
};

#include "tst_themeloader.moc"
AWB_TEST(TestThemeLoader)
