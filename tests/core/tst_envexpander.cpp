#include "awbtest.h"

#include "core/EnvExpander.h"

#include <QDir>
#include <QStandardPaths>
#include <QtTest>

using awb::core::EnvExpander;

/// 测 core::EnvExpander 的环境变量与 ~ 展开：%VAR% 的替换、未知变量与无变量
/// 文本的透传，以及 ~/ 前缀到用户主目录的展开。
class TestEnvExpander : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // %VAR%（Windows 风格）被替换为环境变量的值。
    void testPercentVarExpansion()
    {
        qputenv("AWB_TEST_EXPAND_VAR", QByteArrayLiteral("expanded-value"));
        QCOMPARE(EnvExpander::expand(QStringLiteral("%AWB_TEST_EXPAND_VAR%/x")),
                 QStringLiteral("expanded-value/x"));
        qunsetenv("AWB_TEST_EXPAND_VAR");

        // 未定义的变量必须原样保留，不能被静默丢弃。
        QCOMPARE(EnvExpander::expand(QStringLiteral("%AWB_NO_SUCH_VAR_42%/x")),
                 QStringLiteral("%AWB_NO_SUCH_VAR_42%/x"));

        // 文本里根本没有变量时按原文透传。
        QCOMPARE(EnvExpander::expand(QStringLiteral("C:/plain/path")),
                 QStringLiteral("C:/plain/path"));
    }

    // "~/" 展开为用户主目录。
    void testTildeExpansion()
    {
        const QString home =
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        const QString expanded =
            EnvExpander::expand(QStringLiteral("~/data/file.json"));
        QVERIFY2(expanded.startsWith(home), qPrintable(expanded));
        QVERIFY(!expanded.startsWith(QStringLiteral("~/")));
    }
};

#include "tst_envexpander.moc"
AWB_TEST(TestEnvExpander)
