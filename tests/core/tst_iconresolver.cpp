#include "awbtest.h"

#include "core/IconResolver.h"

#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

using awb::core::IconResolver;

namespace {

/// 各用例共用的兜底图标 URL，模拟调用方给出的 fallback
const QString kFallback = QStringLiteral("qrc:/icons/default.svg");
} // namespace

/// 测 core::IconResolver 的图标地址解析：URL 透传、本地文件转 file:///、
/// %VAR% 与 ~ 的展开，以及不可解析输入回落到调用方提供的兜底图标。
class TestIconResolver : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // 回归：file:// 等 URL 必须原样透传，否则解析好的本地文件图标会在
    // 保存 + 重新加载之后退化成 default.svg。
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

    // 空值与解析不了的输入回落到调用方给的图标——core 从不写死应用资源路径。
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

    // %VAR% 与 ~ 在存在性检查之前展开，用户才能写
    // "%USERPROFILE%/icons/my-agent.svg"。
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
