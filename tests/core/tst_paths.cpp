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

    // setDataRootForTesting wins over everything else.
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
        // DownloadLocation 跟随系统的已知文件夹：名字与位置都随用户配置
        // 变化（本机就重定向到了自定义名字的目录），不能按名字断言；只验
        // 证拿到的是一条非空绝对路径。另注意 Qt 5.15 的实现不做测试模式
        // 重定向，返回的就是真实目录。
        const QString downloads = Paths::downloadsDir();
        QVERIFY(!downloads.isEmpty());
        QVERIFY(QDir::isAbsolutePath(downloads));
    }
};

#include "tst_paths.moc"
AWB_TEST(TestPaths)
