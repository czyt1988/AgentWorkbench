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

// 懒加载文件树的模型契约：roles、目录优先排序、fetch 行为、
// 刷新保住已展开状态、隐藏项排除。夹具结构：
//   root/ alpha.txt  beta.txt  Beta/inner.txt  Zeta/
class TestFileTreeModel : public QObject
{
    Q_OBJECT

private slots:
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

    void testRefreshSeesChangesAndKeepsFetched()
    {
        FileTreeModel model;
        model.setRootPath(m_root);
        const QModelIndex beta = model.index(0, 0);
        model.fetchMore(beta);
        // 展开目录本身已足够：refresh 恢复的是整条已读取链。

        makeFile(QStringLiteral("new.txt"));
        makeFile(QStringLiteral("Beta/extra.txt"));

        QSignalSpy refreshed(&model, &FileTreeModel::refreshed);
        model.refresh();
        QCOMPARE(refreshed.count(), 1);

        QCOMPARE(model.rowCount(QModelIndex()), 5);
        // reset 之后旧索引失效，一律经 indexByPath 重新取。
        const QModelIndex betaAgain = model.indexByPath(QStringLiteral("Beta"));
        QVERIFY(betaAgain.isValid());
        QCOMPARE(model.rowCount(betaAgain), 2);
        QCOMPARE(model.data(model.indexByPath(QStringLiteral("Beta/inner.txt")),
                            FileTreeModel::NameRole),
                 QStringLiteral("inner.txt"));
        QCOMPARE(model.data(model.indexByPath(QStringLiteral("Beta/extra.txt")),
                            FileTreeModel::NameRole),
                 QStringLiteral("extra.txt"));
        QCOMPARE(model.data(model.indexByPath(QStringLiteral("new.txt")),
                            FileTreeModel::NameRole),
                 QStringLiteral("new.txt"));
        // reset 之前的旧索引不作断言：isValid() 只做结构检查，reset 后仍为
        // true，但 internalPointer 指向已释放的旧节点，使用即未定义行为。
    }

    void testIndexByPathFetchesIntermediates()
    {
        makeDir(QStringLiteral("Zeta/deep"));
        makeFile(QStringLiteral("Zeta/deep/leaf.md"));
        FileTreeModel model;
        model.setRootPath(m_root);

        // 中间层从未 fetch；indexByPath 必须沿途补读再给出索引。
        const QModelIndex leaf =
                model.indexByPath(QStringLiteral("Zeta/deep/leaf.md"));
        QVERIFY(leaf.isValid());
        QCOMPARE(model.data(leaf, FileTreeModel::SuffixRole).toString(),
                 QStringLiteral("md"));
        QCOMPARE(model.parent(leaf),
                 model.indexByPath(QStringLiteral("Zeta/deep")));

        // 不存在的路径与空串返回无效索引。
        QVERIFY(!model.indexByPath(QStringLiteral("no/such/path")).isValid());
        QVERIFY(!model.indexByPath(QString()).isValid());
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
        for (int row = 0; row < model.rowCount(QModelIndex()); ++row)
            QVERIFY(model.data(model.index(row, 0), FileTreeModel::NameRole)
                            != QStringLiteral("secret.txt"));
#else
        QCOMPARE(model.rowCount(QModelIndex()), 4);
#endif
    }

private:
    void makeDir(const QString &relative)
    {
        QVERIFY2(QDir(m_root).mkpath(relative), qPrintable(relative));
    }

    void makeFile(const QString &relative)
    {
        QFile file(QDir(m_root).filePath(relative));
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(relative));
        file.write(QByteArrayLiteral("x"));
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_root;
};

AWB_TEST(TestFileTreeModel)
#include "tst_filetreemodel.moc"
