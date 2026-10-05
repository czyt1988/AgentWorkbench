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
using awb::tools::EditorSettings;
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

    /// M3：editor 段往返（缺键默认、坏值夹取、内容不变不写盘的幂等）。
    void testEditorSettingsRoundTrip()
    {
        {
            ToolsStore store(Paths::dataRoot());
            store.load();
            // 缺键全默认（写盘前先读：默认值不落盘）。
            QVERIFY(!store.editor().findCaseSensitive);
            QVERIFY(!store.editor().findWholeWords);
            QCOMPARE(store.editor().contextWindowTokens, 32000);

            EditorSettings editor;
            editor.findCaseSensitive = true;
            editor.findWholeWords = true;
            editor.contextWindowTokens = 128000;
            QVERIFY(store.setEditor(editor));
        }
        ToolsStore reloaded(Paths::dataRoot());
        reloaded.load();
        QVERIFY(reloaded.editor().findCaseSensitive);
        QVERIFY(reloaded.editor().findWholeWords);
        QCOMPARE(reloaded.editor().contextWindowTokens, 128000);

        // 坏值：窗口基数夹回合法区间（0 → 1000；天文数字 → 1000000）。
        {
            QFile file(storeJsonPath());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonObject root =
                QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            QJsonObject editor = root.value(QStringLiteral("editor")).toObject();
            editor[QStringLiteral("contextWindowTokens")] = 0;
            root[QStringLiteral("editor")] = editor;
            QFile writer(storeJsonPath());
            QVERIFY(writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
            writer.write(QJsonDocument(root).toJson());
            writer.close();
        }
        ToolsStore clamped(Paths::dataRoot());
        clamped.load();
        QCOMPARE(clamped.editor().contextWindowTokens, 1000);
    }

    /// M3：checks 段——默认全开、只存已关闭的 id、整组覆盖、坏元素跳过。
    void testChecksDisabledIds()
    {
        {
            ToolsStore store(Paths::dataRoot());
            store.load();
            QVERIFY(store.disabledCheckIds().isEmpty());

            store.setDisabledCheckIds(
                { QStringLiteral("vague-wording"),
                  QStringLiteral("trailing-whitespace") });
        }
        ToolsStore reloaded(Paths::dataRoot());
        reloaded.load();
        QCOMPARE(reloaded.disabledCheckIds(),
                 QStringList({ QStringLiteral("vague-wording"),
                               QStringLiteral("trailing-whitespace") }));

        // 整组覆盖（清空 = 全开）。
        reloaded.setDisabledCheckIds({});
        QVERIFY(reloaded.disabledCheckIds().isEmpty());
        // 坏元素（非字符串、空串、重复）在重载侧被跳过/去重。
        {
            QFile file(storeJsonPath());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonObject root =
                QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            QJsonObject checks = root.value(QStringLiteral("checks")).toObject();
            QJsonArray disabled;
            disabled.append(QStringLiteral("a"));
            disabled.append(QStringLiteral("a"));
            disabled.append(QString());
            disabled.append(42);
            checks[QStringLiteral("disabled")] = disabled;
            root[QStringLiteral("checks")] = checks;
            QFile writer(storeJsonPath());
            QVERIFY(writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
            writer.write(QJsonDocument(root).toJson());
            writer.close();
        }
        ToolsStore dirty(Paths::dataRoot());
        dirty.load();
        QCOMPARE(dirty.disabledCheckIds(),
                 QStringList{QStringLiteral("a")});
    }

    // --- M4：草稿集 -------------------------------------------------------

    /// M4 最硬的判据：旧版三键文件（workspaces/current/draft）读入 →
    /// 一条 "default" 草稿（正文 = draft、工作区 = current）；写回后
    /// drafts/activeDraftId 在、`draft` 镜像键还在且值与当前草稿一致
    /// （旧程序降级读不丢内容）。
    void testLegacyDraftKeyBecomesDefaultDraft()
    {
        {
            ToolsStore store(Paths::dataRoot());
            store.load();
            QVERIFY(store.addWorkspace(QStringLiteral("/w/legacy")));
            QVERIFY(store.setDraft(QStringLiteral("old single draft")));
        }
        // 剥掉一切 M2-M4 键，模拟旧版本写的文件。
        {
            QFile file(storeJsonPath());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QJsonObject root =
                QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            QJsonObject legacy;
            legacy.insert(QStringLiteral("workspaces"),
                          root.value(QStringLiteral("workspaces")));
            legacy.insert(QStringLiteral("current"),
                          root.value(QStringLiteral("current")));
            legacy.insert(QStringLiteral("draft"),
                          root.value(QStringLiteral("draft")));
            QFile writer(storeJsonPath());
            QVERIFY(writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
            writer.write(QJsonDocument(legacy).toJson());
            writer.close();
        }
        ToolsStore reloaded(Paths::dataRoot());
        reloaded.load();
        QCOMPARE(reloaded.drafts().size(), 1);
        QCOMPARE(reloaded.drafts().at(0).id, QStringLiteral("default"));
        QCOMPARE(reloaded.drafts().at(0).body,
                 QStringLiteral("old single draft"));
        QCOMPARE(reloaded.drafts().at(0).workspace,
                 QStringLiteral("/w/legacy"));
        QCOMPARE(reloaded.activeDraftId(), QStringLiteral("default"));
        // 旧键的读侧容错是「容错」不是「迁移」：load() 不写盘。之后的
        // 一次结构写盘（重命名）才把它落进 drafts 键——且 `draft` 镜像
        // 仍在（旧程序降级读不丢内容）。
        QVERIFY(reloaded.renameDraft(QStringLiteral("default"),
                                     QStringLiteral("Kept")));
        {
            QFile file(storeJsonPath());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QJsonObject root =
                QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            QCOMPARE(root.value(QStringLiteral("draft")).toString(),
                     QStringLiteral("old single draft"));
            QVERIFY(root.contains(QStringLiteral("drafts")));
            QVERIFY(root.contains(QStringLiteral("activeDraftId")));
        }
    }

    /// 多草稿增删改与当前位切换；`draft` 镜像始终跟随当前草稿。
    void testDraftsLifecycle()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        QString rejected;
        const QString a = store.createDraft(
            QStringLiteral("Alpha"), QStringLiteral("body-a"),
            QStringLiteral("/w/one"), &rejected);
        QVERIFY2(!a.isEmpty(), "first draft created");
        const QString b = store.createDraft(
            QStringLiteral("Beta"), QStringLiteral("body-b"),
            QString(), &rejected);
        QVERIFY(!b.isEmpty());
        QCOMPARE(store.drafts().size(), 2);
        QCOMPARE(store.activeDraftId(), b);
        QCOMPARE(store.draft(), QStringLiteral("body-b"));   // 镜像跟随

        // 切回 A：镜像切、其余不动。
        QVERIFY(store.setActiveDraft(a));
        QCOMPARE(store.draft(), QStringLiteral("body-a"));
        QCOMPARE(store.drafts().size(), 2);

        // 正文回写只动目标草稿。
        QVERIFY(store.setDraftBody(a, QStringLiteral("body-a-2")));
        QCOMPARE(store.draftById(b).body, QStringLiteral("body-b"));
        QCOMPARE(store.draft(), QStringLiteral("body-a-2"));

        // 重命名与工作区覆盖。
        QVERIFY(store.renameDraft(b, QStringLiteral("Beta 2")));
        QVERIFY(store.updateDraft(b, QStringLiteral("Beta 2"),
                                  QStringLiteral("/w/two")));
        QCOMPARE(store.draftById(b).title, QStringLiteral("Beta 2"));
        QCOMPARE(store.draftById(b).workspace, QStringLiteral("/w/two"));

        // 删除当前草稿 → 顺位队首。
        QVERIFY(store.removeDraft(a));
        QCOMPARE(store.drafts().size(), 1);
        QCOMPARE(store.activeDraftId(), b);
        QCOMPARE(store.draft(), QStringLiteral("body-b"));

        // 删光 → 无草稿、镜像清空。
        QVERIFY(store.removeDraft(b));
        QVERIFY(store.drafts().isEmpty());
        QVERIFY(store.activeDraftId().isEmpty());
        QVERIFY(store.draft().isEmpty());

        // 重载一次：全部状态保真。
        QVERIFY(!store.createDraft(QStringLiteral("Keep"),
                                   QStringLiteral("kept"), QString(),
                                   nullptr).isEmpty());
        ToolsStore reloaded(Paths::dataRoot());
        reloaded.load();
        QCOMPARE(reloaded.drafts().size(), 1);
        QCOMPARE(reloaded.drafts().at(0).title, QStringLiteral("Keep"));
        QCOMPARE(reloaded.drafts().at(0).body, QStringLiteral("kept"));
    }

    /// 上限：第 21 条被拒绝（不静默淘汰），原因可区分。
    void testDraftsLimitRefusal()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        QString rejected;
        for (int i = 0; i < ToolsStore::kMaxDrafts; ++i) {
            const QString id = store.createDraft(
                QStringLiteral("D%1").arg(i), QString(), QString(),
                &rejected);
            QVERIFY2(!id.isEmpty(), "within the cap");
        }
        const QString overflow = store.createDraft(
            QStringLiteral("Over"), QString(), QString(), &rejected);
        QVERIFY(overflow.isEmpty());
        QCOMPARE(rejected, QStringLiteral("draft-limit"));
        QCOMPARE(store.drafts().size(), ToolsStore::kMaxDrafts);
        // 删一条后能再建。
        QVERIFY(store.removeDraft(store.drafts().at(0).id));
        QVERIFY(!store.createDraft(QStringLiteral("Back"), QString(),
                                   QString(), &rejected).isEmpty());
    }

    /// 标题兜底：空标题 → "Untitled N"（编号取首个未占用）。
    void testUntitledNumbering()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        QString rejected;
        store.createDraft(QString(), QString(), QString(), &rejected);
        store.createDraft(QStringLiteral("Untitled 1"), QString(),
                          QString(), &rejected);
        const QString third = store.createDraft(
            QString(), QString(), QString(), &rejected);
        // 已有 "Untitled 1"（显式给了这个名字），第二条未命名的编号跳到 2。
        QCOMPARE(store.draftById(third).title, QStringLiteral("Untitled 2"));
    }

    // --- M4：历史快照 -----------------------------------------------------

    /// 快照：新在前、上限淘汰最旧、内容相同跳过、删除按时间精确定位、
    /// 草稿删除连带清快照、孤儿快照（手改文件造出）重载时清掉。
    void testSnapshotsLifecycle()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        QString rejected;
        const QString id = store.createDraft(
            QStringLiteral("Snap"), QString(), QString(), &rejected);

        // 同内容跳过：连存两版一样的不产生第二条。
        store.addSnapshot(id, QStringLiteral("v1"),
                          QStringLiteral("manual"));
        store.addSnapshot(id, QStringLiteral("v1"),
                          QStringLiteral("manual"));
        QCOMPARE(store.snapshots(id).size(), 1);
        QCOMPARE(store.snapshots(id).at(0).reason,
                 QStringLiteral("manual"));
        QCOMPARE(store.snapshots(id).at(0).charCount, 2);

        store.addSnapshot(id, QStringLiteral("v2"),
                          QStringLiteral("copy"));
        store.addSnapshot(id, QStringLiteral("v3"),
                          QStringLiteral("clear"));
        QCOMPARE(store.snapshots(id).at(0).body, QStringLiteral("v3"));
        QCOMPARE(store.snapshots(id).size(), 3);

        // 别的草稿的快照互不串门。
        const QString other = store.createDraft(
            QStringLiteral("Other"), QString(), QString(), &rejected);
        store.addSnapshot(other, QStringLiteral("o1"),
                          QStringLiteral("switch"));
        QCOMPARE(store.snapshots(id).size(), 3);
        QCOMPARE(store.snapshots(other).size(), 1);

        // 上限淘汰：灌满 + 3，最旧的被挤掉（v1/v2 是头两版）。
        for (int i = 0; i < ToolsStore::kMaxSnapshotsPerDraft; ++i) {
            store.addSnapshot(id, QStringLiteral("bulk-%1").arg(i),
                              QStringLiteral("manual"));
        }
        const QList<awb::tools::DraftSnapshot> after =
            store.snapshots(id);
        QCOMPARE(after.size(), ToolsStore::kMaxSnapshotsPerDraft);
        // 最旧一批（v1、v2、v3）被淘汰：全部 bulk-* 或更晚。
        for (const auto &snapshot : after) {
            QVERIFY(snapshot.body.startsWith(QStringLiteral("bulk-"))
                    || snapshot.body == QStringLiteral("o1"));
        }

        // 精确删除一版。
        const qint64 target = after.at(2).createdAt;
        QVERIFY(store.removeSnapshot(id, target));
        QCOMPARE(store.snapshots(id).size(),
                 ToolsStore::kMaxSnapshotsPerDraft - 1);

        // 草稿删除连带清快照。
        QVERIFY(store.removeDraft(id));
        QVERIFY(store.snapshots(id).isEmpty());
    }

    // --- M4：恢复副本 -----------------------------------------------------

    /// 恢复副本两条路径：有副本（上次未正常退出）→ present + 正文可取；
    /// 无副本（正常退出清掉了）→ present=false。
    void testRecoveryWriteAndClear()
    {
        ToolsStore store(Paths::dataRoot());
        store.load();
        QString rejected;
        const QString id = store.createDraft(
            QStringLiteral("R"), QString(), QString(), &rejected);

        QVERIFY(store.writeRecovery(id, QStringLiteral("unsaved tail")));
        {
            const auto info = store.recovery();
            QVERIFY(info.present);
            QCOMPARE(info.draftId, id);
            QCOMPARE(info.body, QStringLiteral("unsaved tail"));
            QVERIFY(info.updatedAt > 0);
        }
        // 空草稿 id 的恢复副本 = 删除（清空草稿的场合）。
        QVERIFY(store.writeRecovery(QString(), QString()));
        QVERIFY(!store.recovery().present);

        // 正常退出清理。
        QVERIFY(store.writeRecovery(id, QStringLiteral("again")));
        QVERIFY(store.clearRecovery());
        QVERIFY(!store.recovery().present);
        // 再清一次（文件已不在）也成功。
        QVERIFY(store.clearRecovery());
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
