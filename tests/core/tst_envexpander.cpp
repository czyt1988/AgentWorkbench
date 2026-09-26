#include "awbtest.h"

#include "core/EnvExpander.h"

#include <QDir>
#include <QStandardPaths>
#include <QtTest>

using awb::core::EnvExpander;

class TestEnvExpander : public QObject
{
    Q_OBJECT

private slots:
    // %VAR% (Windows style) is replaced with the environment value.
    void testPercentVarExpansion()
    {
        qputenv("AWB_TEST_EXPAND_VAR", QByteArrayLiteral("expanded-value"));
        QCOMPARE(EnvExpander::expand(QStringLiteral("%AWB_TEST_EXPAND_VAR%/x")),
                 QStringLiteral("expanded-value/x"));
        qunsetenv("AWB_TEST_EXPAND_VAR");

        // An unknown variable is left untouched, not silently dropped.
        QCOMPARE(EnvExpander::expand(QStringLiteral("%AWB_NO_SUCH_VAR_42%/x")),
                 QStringLiteral("%AWB_NO_SUCH_VAR_42%/x"));

        // No variable at all passes through unchanged.
        QCOMPARE(EnvExpander::expand(QStringLiteral("C:/plain/path")),
                 QStringLiteral("C:/plain/path"));
    }

    // "~/" becomes the user's home directory.
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
