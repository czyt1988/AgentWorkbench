#include "awbtest.h"

#include "core/LegacyImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::LegacyImport;

class TestLegacyImport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // One-time adoption of the legacy ~/.AgentLauncher directory: files are
    // copied into the new data root, the legacy directory survives, and a
    // second call is a no-op.
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

        // The logger creates the new log directory before the import runs;
        // that alone must not count as "already initialized".
        QVERIFY(QDir().mkpath(newRoot + QStringLiteral("/log")));

        QString notice;
        QVERIFY(LegacyImport::importOnce(newRoot, oldRoot, &notice));
        QVERIFY(!notice.isEmpty());
        QVERIFY(QFile::exists(newRoot + QStringLiteral("/agents.json")));
        QVERIFY(QFile::exists(newRoot + QStringLiteral("/agent_state.json")));
        QVERIFY(
            QFile::exists(newRoot + QStringLiteral("/log/agentlauncher.log")));
        // The legacy directory is never deleted or modified.
        QVERIFY(QFile::exists(oldRoot + QStringLiteral("/agents.json")));
        QVERIFY(
            QFile::exists(oldRoot + QStringLiteral("/log/agentlauncher.log")));

        // Second start: no-op, no second notice.
        notice.clear();
        QVERIFY(!LegacyImport::importOnce(newRoot, oldRoot, &notice));
        QVERIFY(notice.isEmpty());

        // A populated data root is never imported over, even when the legacy
        // directory is still around.
        QVERIFY(write_file(newRoot + QStringLiteral("/settings.json"),
                           QByteArrayLiteral("{}")));
        QVERIFY(write_file(oldRoot + QStringLiteral("/agent_state.json"),
                           QByteArrayLiteral("{\"again\":true}")));
        const QByteArray before =
            QFile(newRoot + QStringLiteral("/agents.json")).readAll();
        QVERIFY(!LegacyImport::importOnce(newRoot, oldRoot, &notice));
        QCOMPARE(QFile(newRoot + QStringLiteral("/agents.json")).readAll(),
                 before);

        // No legacy directory -> nothing happens.
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
