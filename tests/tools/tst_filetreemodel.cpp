#include "awbtest.h"

#include "tools/FileTreeModel.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using awb::tools::FileTreeModel;

/// 测 tools::FileTreeModel 的懒加载文件树契约：roles、目录优先排序、fetch
/// 行为、增量刷新（不 reset、未变的节点原地保留）、隐藏项排除。夹具结构：
///   root/ alpha.txt  beta.txt  Beta/inner.txt  Zeta/
class TestFileTreeModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY2(m_dir->isValid(), "temporary workspace");
        m_root = m_dir->path();
        makeDir(QStringLiteral("Beta"));
        makeDir(QStringLiteral("Zeta"));
        makeFile(QStringLiteral("alpha.txt"));
        makeFile(QStringLiteral("beta.txt"));
        makeFile(QStringLiteral("Beta/inner.txt"));
    }

    void testRolesAndOrdering()
    {
        FileTreeModel model;
        model.setRootPath(m_root);

        // 目录在前（大小写不敏感），文件在后；顶层一层立即读取。
        QCOMPARE(model.rowCount(QModelIndex()), 4);
        QCOMPARE(model.data(model.index(0, 0), FileTreeModel::NameRole),
                 QStringLiteral("Beta"));
        QCOMPARE(model.data(model.index(1, 0), FileTreeModel::NameRole),
                 QStringLiteral("Zeta"));
        QCOMPARE(model.data(model.index(2, 0), FileTreeModel::NameRole),
                 QStringLiteral("alpha.txt"));
        QCOMPARE(model.data(model.index(3, 0), FileTreeModel::NameRole),
                 QStringLiteral("beta.txt"));

        const QModelIndex alpha = model.index(2, 0);
        QCOMPARE(model.data(alpha, FileTreeModel::PathRole).toString(),
                 QDir(m_root).filePath(QStringLiteral("alpha.txt")));
        QCOMPARE(model.data(alpha, FileTreeModel::RelativePathRole).toString(),
                 QStringLiteral("alpha.txt"));
        QCOMPARE(model.data(alpha, FileTreeModel::IsDirRole).toBool(), false);
        QCOMPARE(model.data(alpha, FileTreeModel::SuffixRole).toString(),
                 QStringLiteral("txt"));
        QCOMPARE(model.data(alpha, Qt::DisplayRole).toString(),
                 QStringLiteral("alpha.txt"));

        const QModelIndex beta = model.index(0, 0);
        QCOMPARE(model.data(beta, FileTreeModel::IsDirRole).toBool(), true);
        QVERIFY(model.data(beta, FileTreeModel::SuffixRole).toString().isEmpty());
    }

    void testIconRoleFollowsNameAndSuffix()
    {
        makeFile(QStringLiteral("notes.md"));
        makeDir(QStringLiteral("docs"));
        FileTreeModel model;
        model.setRootPath(m_root);

        // role 名是 QML 的契约：delegate 的 required property 必须叫 iconSource。
        QCOMPARE(model.roleNames().value(FileTreeModel::IconRole),
                 QByteArrayLiteral("iconSource"));
        QCOMPARE(model.data(childIndex(model, QModelIndex(),
                                       QStringLiteral("notes.md")),
                            FileTreeModel::IconRole).toString(),
                 QStringLiteral("qrc:/icons/filetypes/markdown.svg"));
        QCOMPARE(model.data(childIndex(model, QModelIndex(),
                                       QStringLiteral("docs")),
                            FileTreeModel::IconRole).toString(),
                 QStringLiteral("qrc:/icons/foldertypes/docs.svg"));
        // 没进映射表的名字走默认图标。
        QCOMPARE(model.data(childIndex(model, QModelIndex(),
                                       QStringLiteral("Beta")),
                            FileTreeModel::IconRole).toString(),
                 QStringLiteral("qrc:/icons/folder.svg"));
        QCOMPARE(model.data(childIndex(model, QModelIndex(),
                                       QStringLiteral("beta.txt")),
                            FileTreeModel::IconRole).toString(),
                 QStringLiteral("qrc:/icons/filetypes/text.svg"));
    }

    void testLazyFetch()
    {
        FileTreeModel model;
        model.setRootPath(m_root);

        // 未 fetch 的目录：rowCount 为 0 但 hasChildren 恒真（箭头的唯一来源）。
        const QModelIndex beta = model.index(0, 0);
        QCOMPARE(model.rowCount(beta), 0);
        QVERIFY(model.hasChildren(beta));
        QVERIFY(model.canFetchMore(beta));

        model.fetchMore(beta);
        QCOMPARE(model.rowCount(beta), 1);
        QVERIFY(!model.canFetchMore(beta));
        const QModelIndex inner = model.index(0, 0, beta);
        QCOMPARE(model.data(inner, FileTreeModel::RelativePathRole).toString(),
                 QStringLiteral("Beta/inner.txt"));
        QCOMPARE(model.parent(inner), beta);

        // 文件节点不可展开、不可 fetch。
        const QModelIndex alpha = model.index(2, 0);
        QVERIFY(!model.hasChildren(alpha));
        QVERIFY(!model.canFetchMore(alpha));
    }

    void testFetchChildrenIsIdempotent()
    {
        FileTreeModel model;
        model.setRootPath(m_root);
        const QModelIndex beta = model.index(0, 0);

        // QML 兜底入口连调两次不能翻倍插入。
        model.fetchChildren(beta);
        model.fetchChildren(beta);
        QCOMPARE(model.rowCount(beta), 1);
    }

    void testEmptyDirHasNoArrowAfterFetch()
    {
        makeDir(QStringLiteral("Empty"));
        FileTreeModel model;
        model.setRootPath(m_root);

        // fetch 前箭头存在（懒加载无法预知），fetch 后如实消失。
        // 目录按大小写不敏感排序，Empty 落在 Beta 与 Zeta 之间，靠名字找。
        QModelIndex empty;
        for (int row = 0; row < model.rowCount(QModelIndex()); ++row) {
            if (model.data(model.index(row, 0), FileTreeModel::NameRole)
                    == QStringLiteral("Empty")) {
                empty = model.index(row, 0);
                break;
            }
        }
        QVERIFY(empty.isValid());
        QVERIFY(model.hasChildren(empty));
        model.fetchMore(empty);
        QVERIFY(!model.hasChildren(empty));
    }

    void testRefreshIsIncrementalAndKeepsNodes()
    {
        FileTreeModel model;
        model.setRootPath(m_root);
        const QModelIndex beta = childIndex(model, QModelIndex(),
                                            QStringLiteral("Beta"));
        model.fetchChildren(beta);
        QCOMPARE(model.rowCount(beta), 1);

        makeFile(QStringLiteral("new.txt"));
        makeFile(QStringLiteral("Beta/extra.txt"));

        QSignalSpy refreshed(&model, &FileTreeModel::refreshed);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        model.refresh();

        QCOMPARE(refreshed.count(), 1);
        // 绝不 reset：TreeView 收到 modelReset 会销毁全部 delegate 并把整棵树
        // 收起，用户看到的就是「闪一下」。
        QCOMPARE(reset.count(), 0);
        QCOMPARE(removed.count(), 0);
        QVERIFY(inserted.count() >= 2);

        // Beta 节点被复用（同一个指针、同一行），它的子行与视图里的展开状态
        // 因此都还在。
        QVERIFY(model.index(0, 0) == beta);
        QCOMPARE(model.rowCount(beta), 2);
        QVERIFY(childIndex(model, beta, QStringLiteral("extra.txt")).isValid());

        QCOMPARE(model.rowCount(QModelIndex()), 5);
        QVERIFY(childIndex(model, QModelIndex(), QStringLiteral("new.txt")).isValid());
    }

    void testRefreshWithoutChangesIsSilent()
    {
        FileTreeModel model;
        model.setRootPath(m_root);
        model.fetchChildren(childIndex(model, QModelIndex(), QStringLiteral("Beta")));

        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy refreshed(&model, &FileTreeModel::refreshed);
        model.refresh();

        // 目录没变 = 一个信号都不发，视图一个 delegate 都不用碰。
        QCOMPARE(refreshed.count(), 1);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(changed.count(), 0);
    }

    void testRefreshRemovesDeletedEntries()
    {
        FileTreeModel model;
        model.setRootPath(m_root);
        QVERIFY(QFile::remove(QDir(m_root).filePath(QStringLiteral("alpha.txt"))));

        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.refresh();

        QCOMPARE(model.rowCount(QModelIndex()), 3);
        QCOMPARE(inserted.count(), 0);
        // alpha.txt 排在文件组首位（目录在前），即顶层第 2 行。
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed.at(0).at(1).toInt(), 2);
        QCOMPARE(removed.at(0).at(2).toInt(), 2);
        QVERIFY(!childIndex(model, QModelIndex(), QStringLiteral("alpha.txt")).isValid());
    }

    void testRefreshKeepsSortOrder()
    {
        FileTreeModel model;
        model.setRootPath(m_root);

        // 一次刷新里同时出现「最前面插一个目录」与「最后面插一个文件」，
        // 覆盖删除/插入区间的两端。
        makeDir(QStringLiteral("Abc"));
        makeFile(QStringLiteral("zz.txt"));
        model.refresh();

        QStringList names;
        for (int row = 0; row < model.rowCount(QModelIndex()); ++row) {
            names << model.data(model.index(row, 0), FileTreeModel::NameRole)
                            .toString();
        }
        QCOMPARE(names, (QStringList{QStringLiteral("Abc"), QStringLiteral("Beta"),
                                     QStringLiteral("Zeta"), QStringLiteral("alpha.txt"),
                                     QStringLiteral("beta.txt"),
                                     QStringLiteral("zz.txt")}));
    }

    void testRefreshTreatsRenamedEntryAsRemovePlusInsert()
    {
        FileTreeModel model;
        model.setRootPath(m_root);
        QVERIFY(QFile::rename(QDir(m_root).filePath(QStringLiteral("alpha.txt")),
                              QDir(m_root).filePath(QStringLiteral("renamed.txt"))));

        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.refresh();

        QCOMPARE(removed.count(), 1);
        QCOMPARE(inserted.count(), 1);
        QVERIFY(!childIndex(model, QModelIndex(), QStringLiteral("alpha.txt")).isValid());
        QVERIFY(childIndex(model, QModelIndex(), QStringLiteral("renamed.txt")).isValid());
    }

    void testRefreshHandlesTypeChange()
    {
        FileTreeModel model;
        model.setRootPath(m_root);

        // 同名但文件变成了目录：不能复用旧节点（文件节点没有子行）。
        QVERIFY(QFile::remove(QDir(m_root).filePath(QStringLiteral("alpha.txt"))));
        QVERIFY(QDir(m_root).mkpath(QStringLiteral("alpha.txt")));
        model.refresh();

        const QModelIndex node = childIndex(model, QModelIndex(),
                                            QStringLiteral("alpha.txt"));
        QVERIFY(node.isValid());
        QCOMPARE(model.data(node, FileTreeModel::IsDirRole).toBool(), true);
        QVERIFY(model.canFetchMore(node));
    }

    void testRefreshSyncsCollapsedFetchedDirs()
    {
        FileTreeModel model;
        model.setRootPath(m_root);
        const QModelIndex zeta = childIndex(model, QModelIndex(),
                                            QStringLiteral("Zeta"));
        model.fetchChildren(zeta);
        QCOMPARE(model.rowCount(zeta), 0);

        // 收起但读过的目录也要对账，否则下次展开是过期数据。
        makeFile(QStringLiteral("Zeta/late.txt"));
        model.refresh();

        QCOMPARE(model.rowCount(zeta), 1);
        QCOMPARE(model.data(model.index(0, 0, zeta), FileTreeModel::NameRole).toString(),
                 QStringLiteral("late.txt"));
    }

    void testTopLevelCountChangesOnlyWhenItChanges()
    {
        FileTreeModel model;
        model.setRootPath(m_root);
        QCOMPARE(model.topLevelCount(), 4);

        QSignalSpy countChanged(&model, &FileTreeModel::topLevelCountChanged);
        model.refresh();
        QCOMPARE(countChanged.count(), 0);

        makeFile(QStringLiteral("another.txt"));
        model.refresh();
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(model.topLevelCount(), 5);
    }

    void testRootPathAndEmptyRoot()
    {
        FileTreeModel model;
        // 空根 = 无工作区的空树，而不是指向当前目录的假树。
        model.setRootPath(QString());
        QVERIFY(model.rootPath().isEmpty());
        QCOMPARE(model.rowCount(QModelIndex()), 0);

        model.setRootPath(m_root);
        QVERIFY(!model.rootPath().isEmpty());
        QCOMPARE(model.rowCount(QModelIndex()), 4);
    }

    void testHiddenEntriesExcluded()
    {
#ifdef Q_OS_WIN
        // Windows 的「隐藏」是文件属性而不是点前缀，属性要用 Win API 设置。
        const QString hiddenPath = QDir(m_root).filePath(QStringLiteral("secret.txt"));
        QFile hidden(hiddenPath);
        QVERIFY(hidden.open(QIODevice::WriteOnly));
        hidden.close();
        QVERIFY(SetFileAttributesW(
                reinterpret_cast<const wchar_t *>(hiddenPath.utf16()),
                FILE_ATTRIBUTE_HIDDEN));
#else
        makeFile(QStringLiteral(".dotfile"));
#endif

        FileTreeModel model;
        model.setRootPath(m_root);
#ifdef Q_OS_WIN
        QCOMPARE(model.rowCount(QModelIndex()), 4);
        for (int row = 0; row < model.rowCount(QModelIndex()); ++row) {
            QVERIFY(model.data(model.index(row, 0), FileTreeModel::NameRole)
                            != QStringLiteral("secret.txt"));
        }
#else
        QCOMPARE(model.rowCount(QModelIndex()), 4);
#endif
    }

private:
    /**
     * @brief 按名字在 parent 下找子行
     *
     * 模型不提供路径查找，测试自己走一层。
     *
     * @param model 被查的模型
     * @param parent 起始父索引
     * @param name 子项名
     * @return 匹配的子索引；找不到时返回无效索引
     */
    static QModelIndex childIndex(QAbstractItemModel &model,
                                  const QModelIndex &parent, const QString &name)
    {
        for (int row = 0; row < model.rowCount(parent); ++row) {
            const QModelIndex index = model.index(row, 0, parent);
            if (model.data(index, FileTreeModel::NameRole).toString() == name) {
                return index;
            }
        }
        return QModelIndex();
    }

    /**
     * @brief 在夹具根下建一个目录
     *
     * @param relative 相对根的路径
     */
    void makeDir(const QString &relative)
    {
        QVERIFY2(QDir(m_root).mkpath(relative), qPrintable(relative));
    }

    /**
     * @brief 在夹具根下写一个内容为单字节 "x" 的文件
     *
     * @param relative 相对根的路径（父目录需已存在）
     */
    void makeFile(const QString &relative)
    {
        QFile file(QDir(m_root).filePath(relative));
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(relative));
        file.write(QByteArrayLiteral("x"));
    }

    std::unique_ptr<QTemporaryDir> m_dir;  ///< 每个用例独立的临时工作区
    QString m_root;                         ///< 夹具根路径
};

AWB_TEST(TestFileTreeModel)
#include "tst_filetreemodel.moc"
