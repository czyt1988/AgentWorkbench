#include "awbtest.h"

#include "core/Paths.h"

#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::Paths;

class TestPaths : public QObject
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

    // Test mode must keep the data root inside the sandbox: HomeLocation is
    // NOT redirected by QStandardPaths, so deriving from it made every test
    // read and rewrite the developer's real config.
    void testDataRootStaysInTestSandbox()
    {
        const QString realDir =
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QStringLiteral("/.AgentWorkbench");
        QVERIFY(!Paths::dataRoot().startsWith(realDir));
        QVERIFY(QStandardPaths::isTestModeEnabled());
        QCOMPARE(Paths::dataRoot(),
                 QStandardPaths::writableLocation(
                     QStandardPaths::AppConfigLocation));
    }

    // setDataRootForTesting() wins over everything else (specs/01 §4.1).
    void testSetDataRootForTesting()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString before = Paths::dataRoot();

        Paths::setDataRootForTesting(tmp.path());
        QVERIFY(Paths::isDataRootOverridden());
        QCOMPARE(Paths::dataRoot(), tmp.path());

        Paths::setDataRootForTesting(QString());
        QVERIFY(!Paths::isDataRootOverridden());
        QCOMPARE(Paths::dataRoot(), before);
    }

    // Every sub-directory hangs off the data root.
    void testSubdirectories()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());

        QCOMPARE(Paths::themesDir(), tmp.path() + QStringLiteral("/themes"));
        QCOMPARE(Paths::pluginsDir(), tmp.path() + QStringLiteral("/plugins"));
        QCOMPARE(Paths::logsDir(), tmp.path() + QStringLiteral("/log"));
        QCOMPARE(Paths::webProfilesDir(),
                 tmp.path() + QStringLiteral("/webprofiles"));
        QVERIFY(Paths::downloadsDir().endsWith(QStringLiteral("Downloads"))
                || Paths::downloadsDir().endsWith(QStringLiteral("下载")));
    }
};

#include "tst_paths.moc"
AWB_TEST(TestPaths)
