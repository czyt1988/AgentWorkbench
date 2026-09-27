#include "awbtest.h"

#include "core/Paths.h"
#include "theme/ThemeRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::Paths;
using awb::theme::ThemeFile;
using awb::theme::ThemeRegistry;

class TestThemeRegistry : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        Paths::setDataRootForTesting(QString());
    }

    void cleanup()
    {
        Paths::setDataRootForTesting(QString());
    }

    // Both built-ins are listed, in a stable order.
    void testBuiltinsListed()
    {
        ThemeRegistry registry;
        const QList<ThemeFile> themes = registry.themes();
        QVERIFY(themes.size() >= 2);
        QCOMPARE(themes.at(0).id, QStringLiteral("mocha-dark"));
        QCOMPARE(themes.at(1).id, QStringLiteral("latte-light"));
        QVERIFY(registry.baseline(QStringLiteral("dark")).isValid());
        QVERIFY(registry.baseline(QStringLiteral("light")).isValid());
    }

    // A user theme file with a built-in id replaces the built-in values;
    // a new id adds a theme; a file whose name != id is skipped.
    void testUserOverrideAndNewTheme()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());
        QVERIFY(QDir().mkpath(tmp.path() + QStringLiteral("/themes")));

        // Override the built-in dark theme's accent only.
        QJsonObject overrideJson;
        overrideJson[QStringLiteral("id")] = QStringLiteral("mocha-dark");
        overrideJson[QStringLiteral("name")] = QStringLiteral("My Mocha");
        overrideJson[QStringLiteral("variant")] = QStringLiteral("dark");
        QJsonObject colors;
        colors[QStringLiteral("accent")] = QStringLiteral("#ff0000");
        overrideJson[QStringLiteral("colors")] = colors;
        writeFile(tmp.path() + QStringLiteral("/themes/mocha-dark.json"),
                  overrideJson);

        // A brand-new light theme.
        QJsonObject custom;
        custom[QStringLiteral("id")] = QStringLiteral("custom-light");
        custom[QStringLiteral("name")] = QStringLiteral("Custom Light");
        custom[QStringLiteral("variant")] = QStringLiteral("light");
        writeFile(tmp.path() + QStringLiteral("/themes/custom-light.json"),
                  custom);

        // Name mismatch — must be skipped entirely.
        QJsonObject mismatch;
        mismatch[QStringLiteral("id")] = QStringLiteral("not-the-file-name");
        mismatch[QStringLiteral("name")] = QStringLiteral("Mismatch");
        mismatch[QStringLiteral("variant")] = QStringLiteral("dark");
        writeFile(tmp.path() + QStringLiteral("/themes/sneaky.json"),
                  mismatch);

        ThemeRegistry registry;
        const ThemeFile overridden =
            registry.theme(QStringLiteral("mocha-dark"));
        QVERIFY(overridden.isValid());
        QCOMPARE(overridden.name, QStringLiteral("My Mocha"));
        QCOMPARE(overridden.colors.value(QStringLiteral("accent")),
                 QColor(QStringLiteral("#ff0000")));
        // Untouched tokens still come from the built-in baseline.
        QVERIFY(overridden.colors.contains(QStringLiteral("danger")));

        const ThemeFile added = registry.theme(QStringLiteral("custom-light"));
        QVERIFY(added.isValid());

        QVERIFY(!registry.theme(QStringLiteral("not-the-file-name")).isValid());

        bool sawCustom = false;
        for (const ThemeFile &t : registry.themes()) {
            if (t.id == QLatin1String("custom-light"))
                sawCustom = true;
        }
        QVERIFY(sawCustom);

        Paths::setDataRootForTesting(QString());
    }

private:
    static void writeFile(const QString &path, const QJsonObject &json)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(json).toJson());
    }
};

#include "tst_themeregistry.moc"
AWB_TEST(TestThemeRegistry)
