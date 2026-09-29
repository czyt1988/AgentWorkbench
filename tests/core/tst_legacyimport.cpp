#include "awbtest.h"

#include "core/LegacyImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::LegacyImport;

/// 测 core::LegacyImport 的旧目录一次性导入：文件复制（非搬移）、旧目录保持
/// 原样、二次运行不重复导入，以及已有数据的新目录绝不覆盖导入。
class TestLegacyImport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // 旧 ~/.AgentLauncher 目录的一次性接管：文件复制进新数据根、旧目录保留
    // 不动，第二次调用是 no-op。
    void testImportOnce()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString oldRoot = tmp.path() + QStringLiteral("/.AgentLauncher");
        const QString newRoot = tmp.path() + QStringLiteral("/.AgentWorkbench");

        auto write_file = [](const QString &path, const QByteArray &bytes) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                return false;
            }
            return f.write(bytes) == bytes.size();
        };
        QVERIFY(write_file(oldRoot + QStringLiteral("/agents.json"),
                           QByteArrayLiteral("{\"agents\":[]}")));
        QVERIFY(write_file(oldRoot + QStringLiteral("/agent_state.json"),
                           QByteArrayLiteral("{}")));
        QVERIFY(write_file(oldRoot + QStringLiteral("/log/agentlauncher.log"),
                           QByteArrayLiteral("old log")));

        // logger 会在导入运行前先建好新的日志目录；仅凭这一点不能判定
        // 「已初始化过」。
        QVERIFY(QDir().mkpath(newRoot + QStringLiteral("/log")));

        QString notice;
        QVERIFY(LegacyImport::importOnce(newRoot, oldRoot, &notice));
        QVERIFY(!notice.isEmpty());
        QVERIFY(QFile::exists(newRoot + QStringLiteral("/agents.json")));
        QVERIFY(QFile::exists(newRoot + QStringLiteral("/agent_state.json")));
        QVERIFY(
            QFile::exists(newRoot + QStringLiteral("/log/agentlauncher.log")));
        // 旧目录绝不删除或改动。
        QVERIFY(QFile::exists(oldRoot + QStringLiteral("/agents.json")));
        QVERIFY(
            QFile::exists(oldRoot + QStringLiteral("/log/agentlauncher.log")));

        // 第二次启动：no-op，不再弹出提示。
        notice.clear();
        QVERIFY(!LegacyImport::importOnce(newRoot, oldRoot, &notice));
        QVERIFY(notice.isEmpty());

        // 已有内容的数据根绝不覆盖导入，哪怕旧目录还在。
        QVERIFY(write_file(newRoot + QStringLiteral("/settings.json"),
                           QByteArrayLiteral("{}")));
        QVERIFY(write_file(oldRoot + QStringLiteral("/agent_state.json"),
                           QByteArrayLiteral("{\"again\":true}")));
        const QByteArray before =
            QFile(newRoot + QStringLiteral("/agents.json")).readAll();
        QVERIFY(!LegacyImport::importOnce(newRoot, oldRoot, &notice));
        QCOMPARE(QFile(newRoot + QStringLiteral("/agents.json")).readAll(),
                 before);

        // 没有旧目录 → 什么都不发生。
        QTemporaryDir solo;
        QVERIFY(solo.isValid());
        QVERIFY(!LegacyImport::importOnce(solo.path() + QStringLiteral("/new"),
                                          solo.path()
                                              + QStringLiteral("/missing-old"),
                                          &notice));
        QVERIFY(notice.isEmpty());
    }
};

#include "tst_legacyimport.moc"
AWB_TEST(TestLegacyImport)
