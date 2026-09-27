#include "awbtest.h"

#include "core/IconResolver.h"

#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

using awb::core::IconResolver;

namespace {
const QString kFallback = QStringLiteral("qrc:/icons/default.svg");
} // namespace

class TestIconResolver : public QObject
{
    Q_OBJECT

private slots:
    // Regression: file:// URLs must pass through unchanged, otherwise a
    // resolved local-file icon degrades to default.svg after save+reload.
    void testPassthrough()
    {
        QCOMPARE(IconResolver::resolve(QStringLiteral("file:///C:/icons/a.svg"),
                                       kFallback),
                 QStringLiteral("file:///C:/icons/a.svg"));
        QCOMPARE(IconResolver::resolve(QStringLiteral("qrc:/icons/bot.svg"),
                                       kFallback),
                 QStringLiteral("qrc:/icons/bot.svg"));
        QCOMPARE(IconResolver::resolve(QStringLiteral("http://example.com/i.svg"),
                                       kFallback),
                 QStringLiteral("http://example.com/i.svg"));
        QCOMPARE(IconResolver::resolve(QStringLiteral("https://example.com/i.svg"),
                                       kFallback),
                 QStringLiteral("https://example.com/i.svg"));
    }

    // Empty or unresolvable input falls back to the caller-provided icon —
    // core never hardcodes an application resource path.
    void testFallbackIsCallerProvided()
    {
        QCOMPARE(IconResolver::resolve(QString(), kFallback), kFallback);
        QCOMPARE(IconResolver::resolve(QStringLiteral("does/not/exist.svg"),
                                       kFallback),
                 kFallback);
        QCOMPARE(IconResolver::resolve(QString(),
                                       QStringLiteral("qrc:/icons/bot.svg")),
                 QStringLiteral("qrc:/icons/bot.svg"));
    }

    void testExistingLocalFile()
    {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        const QString url = IconResolver::resolve(tmp.fileName(), kFallback);
        QVERIFY2(url.startsWith(QStringLiteral("file:///")), qPrintable(url));
    }

    // %VAR% and ~ are expanded before the existence check, so users can
    // write "%USERPROFILE%/icons/my-agent.svg".
    void testEnvironmentExpansion()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        qputenv("AWB_TEST_ICON_DIR", tmp.path().toUtf8());

        QFile icon(tmp.path() + QStringLiteral("/x.svg"));
        QVERIFY(icon.open(QIODevice::WriteOnly));
        icon.write("<svg/>");
        icon.close();

        const QString url = IconResolver::resolve(
            QStringLiteral("%AWB_TEST_ICON_DIR%/x.svg"), kFallback);
        QVERIFY2(url.startsWith(QStringLiteral("file:///")), qPrintable(url));

        qunsetenv("AWB_TEST_ICON_DIR");
    }
};

#include "tst_iconresolver.moc"
AWB_TEST(TestIconResolver)
