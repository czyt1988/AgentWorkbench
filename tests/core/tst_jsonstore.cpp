#include "awbtest.h"

#include "core/JsonStore.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::JsonStore;

/// 测 core::JsonStore 的 JSON 读写：对象往返、写时创建父目录、缺文件与坏
/// JSON 的降级、字节级原样写入，以及写失败必须带可读错误。
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

    // 写入时创建缺失的父目录；文件只在完整落盘后才出现（QSaveFile 原子提交）。
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

    // 文件缺失是正常的首次运行：返回空对象，不崩溃。
    void testMissingFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(JsonStore::readFile(
                    tmp.path() + QStringLiteral("/nope.json")).isEmpty());
    }

    // 坏 JSON 降级为空对象（另记一条日志），单个损坏文件不能把整个应用拖挂。
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
        // 文件位置上是一个目录，让以写模式打开必然失败。
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
