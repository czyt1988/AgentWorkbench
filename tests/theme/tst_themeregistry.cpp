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

/// 测 theme::ThemeRegistry 的主题清单：内置主题的稳定排序、用户主题按内置
/// id 覆盖、新 id 追加，以及文件名与 id 不符的文件被整体跳过。
class TestThemeRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        Paths::setDataRootForTesting(QString());
    }

    void cleanup()
    {
        Paths::setDataRootForTesting(QString());
    }

    // 两个内置都列出来，顺序稳定。
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

    // 用户主题文件用内置 id 时替换内置值；新 id 追加一个主题；
    // 文件名 != id 的文件被跳过。
    void testUserOverrideAndNewTheme()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());
        QVERIFY(QDir().mkpath(tmp.path() + QStringLiteral("/themes")));

        // 只覆盖内置暗色主题的 accent 一个 token。
        QJsonObject overrideJson;
        overrideJson[QStringLiteral("id")] = QStringLiteral("mocha-dark");
        overrideJson[QStringLiteral("name")] = QStringLiteral("My Mocha");
        overrideJson[QStringLiteral("variant")] = QStringLiteral("dark");
        QJsonObject colors;
        colors[QStringLiteral("accent")] = QStringLiteral("#ff0000");
        overrideJson[QStringLiteral("colors")] = colors;
        writeFile(tmp.path() + QStringLiteral("/themes/mocha-dark.json"),
                  overrideJson);

        // 全新的亮色主题。
        QJsonObject custom;
        custom[QStringLiteral("id")] = QStringLiteral("custom-light");
        custom[QStringLiteral("name")] = QStringLiteral("Custom Light");
        custom[QStringLiteral("variant")] = QStringLiteral("light");
        writeFile(tmp.path() + QStringLiteral("/themes/custom-light.json"),
                  custom);

        // 名字对不上——必须被整体跳过。
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
        // 没动过的 token 仍来自内置基线。
        QVERIFY(overridden.colors.contains(QStringLiteral("danger")));

        const ThemeFile added = registry.theme(QStringLiteral("custom-light"));
        QVERIFY(added.isValid());

        QVERIFY(!registry.theme(QStringLiteral("not-the-file-name")).isValid());

        bool sawCustom = false;
        for (const ThemeFile &t : registry.themes()) {
            if (t.id == QStringLiteral("custom-light")) {
                sawCustom = true;
            }
        }
        QVERIFY(sawCustom);

        Paths::setDataRootForTesting(QString());
    }

private:
    /**
     * @brief 把主题 JSON 对象写进指定路径
     *
     * @param path 目标文件路径（父目录需已存在）
     * @param json 完整的主题根对象
     */
    static void writeFile(const QString &path, const QJsonObject &json)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(json).toJson());
    }
};

#include "tst_themeregistry.moc"
AWB_TEST(TestThemeRegistry)
