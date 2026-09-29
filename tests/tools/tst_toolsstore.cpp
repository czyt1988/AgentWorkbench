#include "awbtest.h"

#include "core/Paths.h"
#include "tools/ToolsStore.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::Paths;
using awb::tools::ToolsStore;

/// 测 tools::ToolsStore 的 tools.json 状态语义：MRU 换序、20 上限淘汰、
/// current 顺延、草稿往返。
class TestToolsStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY2(m_dir->isValid(), "temporary data root");
        Paths::setDataRootForTesting(m_dir->path());
    }

    void cleanup()
    {
        Paths::setDataRootForTesting(QString());
        m_dir.reset();
    }

    void testRoundTrip()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        QVERIFY(store.addWorkspace(QStringLiteral("/w/one")));
        QVERIFY(store.setDraft(QStringLiteral("hello draft")));

        // 一个新实例读到的必须是刚写下的全部状态。
        ToolsStore reloaded(Paths::dataRoot());
        reloaded.load();
        QCOMPARE(reloaded.workspaces(), QStringList{QStringLiteral("/w/one")});
        QCOMPARE(reloaded.currentWorkspace(), QStringLiteral("/w/one"));
        QCOMPARE(reloaded.draft(), QStringLiteral("hello draft"));
    }

    void testMruOrdering()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        store.addWorkspace(QStringLiteral("/w/a"));
        store.addWorkspace(QStringLiteral("/w/b"));
        store.addWorkspace(QStringLiteral("/w/c"));

        // 切换 a 必须把它挪到队首。
        store.setCurrentWorkspace(QStringLiteral("/w/a"));
        QCOMPARE(store.workspaces(),
                 (QStringList{QStringLiteral("/w/a"), QStringLiteral("/w/c"),
                              QStringLiteral("/w/b")}));

        // 已存在的路径再 add 同样是一次 MRU 触碰，不产生重复。
        store.addWorkspace(QStringLiteral("/w/c"));
        QCOMPARE(store.workspaces(),
                 (QStringList{QStringLiteral("/w/c"), QStringLiteral("/w/a"),
                              QStringLiteral("/w/b")}));
        QCOMPARE(store.currentWorkspace(), QStringLiteral("/w/c"));
    }

    void testMaxTwentyEviction()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        for (int i = 0; i < ToolsStore::kMaxWorkspaces + 5; ++i) {
            store.addWorkspace(QStringLiteral("/w/%1").arg(i));
        }

        // 最早加入的 5 个被淘汰；队首是最后触碰的 19 号。
        QCOMPARE(store.workspaces().size(), ToolsStore::kMaxWorkspaces);
        QCOMPARE(store.workspaces().constFirst(), QStringLiteral("/w/24"));
        QVERIFY(!store.workspaces().contains(QStringLiteral("/w/0")));
        QVERIFY(!store.workspaces().contains(QStringLiteral("/w/4")));
        QVERIFY(store.workspaces().contains(QStringLiteral("/w/5")));
    }

    void testRemoveCurrentFallsBackToHead()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        store.addWorkspace(QStringLiteral("/w/a"));
        store.addWorkspace(QStringLiteral("/w/b"));

        // 移除当前项 b，current 顺延为队首 a；全删光后 current 为空。
        store.removeWorkspace(QStringLiteral("/w/b"));
        QCOMPARE(store.workspaces(), QStringList{QStringLiteral("/w/a")});
        QCOMPARE(store.currentWorkspace(), QStringLiteral("/w/a"));

        store.removeWorkspace(QStringLiteral("/w/a"));
        QVERIFY(store.workspaces().isEmpty());
        QVERIFY(store.currentWorkspace().isEmpty());
    }

    void testCurrentNotInListIsDropped()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        store.addWorkspace(QStringLiteral("/w/a"));

        // 手工造一个 current 指向不存在项的文件：load 必须把它清掉，
        // 否则门面会给文件树设一个不存在的根。
        const QString path = store.storeFilePath();
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArrayLiteral(
                "{\"workspaces\":[\"/w/a\"],\"current\":\"/w/gone\"}"));
        file.close();

        ToolsStore reloaded(Paths::dataRoot());
        reloaded.load();
        QCOMPARE(reloaded.workspaces(), QStringList{QStringLiteral("/w/a")});
        QVERIFY(reloaded.currentWorkspace().isEmpty());
    }

    void testSetCurrentRejectsUnknownPath()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        store.addWorkspace(QStringLiteral("/w/a"));

        // 列表外路径不接受：接受会让 current 与 workspaces 失配。
        store.setCurrentWorkspace(QStringLiteral("/w/unknown"));
        QCOMPARE(store.currentWorkspace(), QStringLiteral("/w/a"));

        // 空串是合法目标（清空当前工作区）。
        store.setCurrentWorkspace(QString());
        QVERIFY(store.currentWorkspace().isEmpty());
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;  ///< 每个用例独立的临时数据根
};

AWB_TEST(TestToolsStore)
#include "tst_toolsstore.moc"
