#include "awbtest.h"

#include "core/Paths.h"
#include "tools/ToolsStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

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

    /// M2 兼容红线：旧版三键文件（workspaces/current/draft）读入 →
    /// 草稿与工作区记忆完整保留；写回后旧键原样在。
    void testOldThreeKeyFileKeepsDraft()
    {
        {
            ToolsStore store(Paths::dataRoot());
            store.load();
            store.addWorkspace(QStringLiteral("/w/keep"));
            QVERIFY(store.setDraft(QStringLiteral("precious draft")));
        }
        // 模拟旧版本写的文件：手工把 favorites/ribbon 键剥掉再读。
        {
            QFile file(storeJsonPath());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QJsonObject root =
                QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            QJsonObject stripped;
            stripped.insert(QStringLiteral("workspaces"),
                            root.value(QStringLiteral("workspaces")));
            stripped.insert(QStringLiteral("current"),
                            root.value(QStringLiteral("current")));
            stripped.insert(QStringLiteral("draft"),
                            root.value(QStringLiteral("draft")));
            QFile writer(storeJsonPath());
            QVERIFY(writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
            writer.write(QJsonDocument(stripped).toJson());
            writer.close();
        }
        ToolsStore reloaded(Paths::dataRoot());
        reloaded.load();
        QCOMPARE(reloaded.draft(), QStringLiteral("precious draft"));
        QCOMPARE(reloaded.workspaces(),
                 QStringList{QStringLiteral("/w/keep")});
        QVERIFY(reloaded.favorites().isEmpty());
        // 写回（例如加一条收藏）后旧键仍在——键位是磁盘格式。
        reloaded.addFavorite(QStringLiteral("T"), QStringLiteral("B"), {});
        QFile file(storeJsonPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root =
            QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QCOMPARE(root.value(QStringLiteral("draft")).toString(),
                 QStringLiteral("precious draft"));
        QVERIFY(root.contains(QStringLiteral("favorites")));
    }

    /// 收藏：新增幂等（同标题同全文重存）、上限淘汰最旧、按内容精确移除。
    void testFavoritesLifecycle()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        QVERIFY(store.favorites().isEmpty());

        store.addFavorite(QStringLiteral("First"), QStringLiteral("body-1"),
                          { QStringLiteral("a") });
        store.addFavorite(QStringLiteral("Second"), QStringLiteral("body-2"),
                          {});
        QCOMPARE(store.favorites().size(), 2);
        QCOMPARE(store.favorites().at(0).title,
                 QStringLiteral("Second"));   // 新在前

        // 同标题同全文 = 幂等重存（挪到队首，不占新位）。
        store.addFavorite(QStringLiteral("First"), QStringLiteral("body-1"),
                          {});
        QCOMPARE(store.favorites().size(), 2);
        QCOMPARE(store.favorites().at(0).title, QStringLiteral("First"));

        // 同标题不同全文 = 两条（改了一个字的成稿是另一份草稿）。
        store.addFavorite(QStringLiteral("First"), QStringLiteral("body-1b"),
                          {});
        QCOMPARE(store.favorites().size(), 3);

        // 上限淘汰：灌满到 kMaxFavorites + 5，最先存的（body-2）被挤掉。
        for (int i = 0; i < ToolsStore::kMaxFavorites + 5; ++i) {
            store.addFavorite(QStringLiteral("F%1").arg(i),
                              QStringLiteral("body-%1").arg(i), {});
        }
        QCOMPARE(store.favorites().size(), ToolsStore::kMaxFavorites);
        bool sawBody2 = false;
        for (const auto &entry : store.favorites()) {
            if (entry.body == QStringLiteral("body-2")) {
                sawBody2 = true;
            }
        }
        QVERIFY(!sawBody2);
        // 最新的还在队首。
        QCOMPARE(store.favorites().at(0).title,
                 QStringLiteral("F%1").arg(ToolsStore::kMaxFavorites + 4));

        // 按标题 + 全文精确移除。
        store.removeFavorite(
            store.favorites().at(0).title, store.favorites().at(0).body);
        QCOMPARE(store.favorites().size(), ToolsStore::kMaxFavorites - 1);
    }

private:
    // <dataRoot>/tools.json 的绝对路径（与被测的 store 同一棵树）。
    QString storeJsonPath() const
    {
        return Paths::dataRoot() + QStringLiteral("/tools.json");
    }

    std::unique_ptr<QTemporaryDir> m_dir;  ///< 每个用例独立的临时数据根
};

AWB_TEST(TestToolsStore)
#include "tst_toolsstore.moc"
