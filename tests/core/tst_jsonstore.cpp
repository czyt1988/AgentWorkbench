#include "awbtest.h"

#include "core/JsonStore.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::JsonStore;

class TestJsonStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRoundTrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.path() + QStringLiteral("/cfg.json");

        QJsonObject obj;
        obj[QStringLiteral("name")] = QStringLiteral("AgentWorkbench");
        obj[QStringLiteral("count")] = 42;
        QVERIFY(JsonStore::writeFile(path, obj).ok);

        QCOMPARE(JsonStore::readFile(path), obj);
    }

    // Parent directories are created on write, and the file appears only
    // complete (QSaveFile commits atomically).
    void testCreatesParentDirectories()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path =
            tmp.path() + QStringLiteral("/nested/deep/cfg.json");

        QJsonObject obj;
        obj[QStringLiteral("a")] = 1;
        QVERIFY(JsonStore::writeFile(path, obj).ok);
        QVERIFY(QFile::exists(path));
        QCOMPARE(JsonStore::readFile(path), obj);
    }

    // A missing file is a normal first run: empty object, no crash.
    void testMissingFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(JsonStore::readFile(
                    tmp.path() + QStringLiteral("/nope.json")).isEmpty());
    }

    // Malformed content degrades to an empty object (plus a log line) so
    // one corrupt file cannot take the whole app down.
    void testInvalidJson()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.path() + QStringLiteral("/broken.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{not json at all");
        f.close();

        QVERIFY(JsonStore::readFile(path).isEmpty());
    }

    void testWriteBytes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.path() + QStringLiteral("/verbatim.json");
        const QByteArray bytes = "{\"agents\":[]}\n";
        QVERIFY(JsonStore::writeBytes(path, bytes).ok);

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bytes);
    }

    void testWriteFailure()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // A directory in the file's position makes opening for write fail.
        const QString path = tmp.path() + QStringLiteral("/asdir");
        QVERIFY(QDir().mkpath(path));

        QJsonObject obj;
        obj[QStringLiteral("a")] = 1;
        const auto result = JsonStore::writeFile(path, obj);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }
};

#include "tst_jsonstore.moc"
AWB_TEST(TestJsonStore)
