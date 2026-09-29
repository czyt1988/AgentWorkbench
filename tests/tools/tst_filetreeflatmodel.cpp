#include "awbtest.h"

#include "tools/FileTreeFlatModel.h"
#include "tools/FileTreeModel.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using awb::tools::FileTreeFlatModel;
using awb::tools::FileTreeModel;

// FileTreeFlatModel 的投影契约：顶层压平、展开/收起递归、懒 fetch 由
// toggleExpanded 兜底、modelReset 清空展开状态、refreshed 重建后按路径保留
// 展开状态。夹具结构与 tst_filetreemodel 相同：
//   root/ alpha.txt  beta.txt  Beta/inner.txt  Zeta/
class TestFileTreeFlatModel : public QObject
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

    void testTopLevelProjection()
    {
        FileTreeModel source;
        FileTreeFlatModel flat;
        flat.setSourceModel(&source);
        source.setRootPath(m_root);

        // 顶层一层立即投影，顺序与源模型一致（目录在前）。
        QCOMPARE(flat.rowCount(), 4);
        QCOMPARE(flat.data(flat.index(0, 0), FileTreeFlatModel::NameRole),
                 QStringLiteral("Beta"));
        QCOMPARE(flat.data(flat.index(1, 0), FileTreeFlatModel::NameRole),
                 QStringLiteral("Zeta"));
        QCOMPARE(flat.data(flat.index(2, 0), FileTreeFlatModel::NameRole),
                 QStringLiteral("alpha.txt"));
        QCOMPARE(flat.data(flat.index(0, 0), FileTreeFlatModel::DepthRole)
                     .toInt(), 0);
        QCOMPARE(flat.data(flat.index(0, 0), FileTreeFlatModel::ExpandedRole)
                     .toBool(), false);
        QCOMPARE(flat.visibleCount(), 4);

        // role 名是 QML delegate 的契约，与源模型同名同义。
        QCOMPARE(flat.roleNames().value(FileTreeFlatModel::NameRole),
                 QByteArrayLiteral("name"));
        QCOMPARE(flat.roleNames().value(FileTreeFlatModel::RelativePathRole),
                 QByteArrayLiteral("relativePath"));
        QCOMPARE(flat.roleNames().value(FileTreeFlatModel::DepthRole),
                 QByteArrayLiteral("depth"));
        QCOMPARE(flat.roleNames().value(FileTreeFlatModel::ExpandedRole),
                 QByteArrayLiteral("expanded"));
        QCOMPARE(flat.roleNames().value(FileTreeFlatModel::HasChildrenRole),
                 QByteArrayLiteral("hasChildren"));
    }

    void testExpandCollapse()
    {
        FileTreeModel source;
        FileTreeFlatModel flat;
        flat.setSourceModel(&source);
        source.setRootPath(m_root);

        // 展开 Beta：fetch 兜底 + 子行插在 1..1（inner.txt），depth=1。
        QSignalSpy inserted(&flat, &QAbstractItemModel::rowsInserted);
        flat.toggleExpanded(0);
        QCOMPARE(flat.rowCount(), 5);
        QCOMPARE(inserted.count(), 1);
        // 传给信号的区间：first=1, last=1。
        QCOMPARE(inserted.first().at(1).toInt(), 1);
        QCOMPARE(inserted.first().at(2).toInt(), 1);
        QCOMPARE(flat.data(flat.index(1, 0),
                           FileTreeFlatModel::RelativePathRole)
                     .toString(),
                 QStringLiteral("Beta/inner.txt"));
        QCOMPARE(flat.data(flat.index(1, 0), FileTreeFlatModel::DepthRole)
                     .toInt(), 1);
        QCOMPARE(flat.data(flat.index(0, 0),
                           FileTreeFlatModel::ExpandedRole).toBool(), true);

        // 箭头语义：文件行 hasChildren 恒假；未读目录恒真；空目录读过为假。
        QCOMPARE(flat.data(flat.index(1, 0),
                           FileTreeFlatModel::HasChildrenRole).toBool(), false);
        QCOMPARE(flat.data(flat.index(2, 0),
                           FileTreeFlatModel::HasChildrenRole).toBool(), true);

        // 展开非目录行是 no-op；越界行也是。
        flat.toggleExpanded(1);
        QCOMPARE(flat.rowCount(), 5);
        flat.toggleExpanded(-1);
        flat.toggleExpanded(99);
        QCOMPARE(flat.rowCount(), 5);

        // 收起 Beta：子树递归移除。
        QSignalSpy removed(&flat, &QAbstractItemModel::rowsRemoved);
        flat.toggleExpanded(0);
        QCOMPARE(flat.rowCount(), 4);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed.first().at(1).toInt(), 1);
        QCOMPARE(removed.first().at(2).toInt(), 1);
        QCOMPARE(flat.data(flat.index(0, 0),
                           FileTreeFlatModel::ExpandedRole).toBool(), false);
    }

    void testNestedCollapseIsRecursive()
    {
        makeDir(QStringLiteral("Beta/Nested"));
        makeFile(QStringLiteral("Beta/Nested/deep.txt"));
        FileTreeModel source;
        FileTreeFlatModel flat;
        flat.setSourceModel(&source);
        source.setRootPath(m_root);

        // Beta -> Nested 两层展开。Nested 的子行还没读盘：投影只保证把
        // 「已 fetch 的目录」压平，第二层要先对源模型 fetch（对应真实交互
        // 中 TreeView/ListView 首次展开逐层触发 fetch 的节奏）。
        flat.toggleExpanded(0);
        const QModelIndex beta = source.index(0, 0);
        QVERIFY(source.data(beta, FileTreeModel::IsDirRole).toBool());
        source.fetchChildren(beta);
        flat.rebuild();
        QCOMPARE(flat.data(flat.index(0, 0),
                           FileTreeFlatModel::ExpandedRole).toBool(), true);
        const int nestedRow = rowOf(flat, QStringLiteral("Beta/Nested"));
        QVERIFY(nestedRow > 0);
        flat.toggleExpanded(nestedRow);
        QVERIFY(flat.rowCount() > 6);

        // 收起 Beta：Nested 的展开状态一并清掉，重展开后 deep.txt 不在。
        flat.toggleExpanded(0);
        flat.toggleExpanded(0);
        QVERIFY(rowOf(flat, QStringLiteral("Beta/Nested/deep.txt")) < 0);
        QCOMPARE(flat.data(
                     flat.index(rowOf(flat, QStringLiteral("Beta/Nested")), 0),
                     FileTreeFlatModel::ExpandedRole).toBool(), false);
    }

    void testRefreshRebuildKeepsExpansion()
    {
        FileTreeModel source;
        FileTreeFlatModel flat;
        flat.setSourceModel(&source);
        source.setRootPath(m_root);
        flat.toggleExpanded(0);
        QCOMPARE(flat.rowCount(), 5);

        // 外部变更（新增顶层文件）+ refresh：按路径保留展开状态，投影重建。
        makeFile(QStringLiteral("fresh.txt"));
        source.refresh();
        QCOMPARE(flat.rowCount(), 6);
        QCOMPARE(flat.data(flat.index(0, 0),
                           FileTreeFlatModel::ExpandedRole).toBool(), true);
        QVERIFY(rowOf(flat, QStringLiteral("Beta/inner.txt")) > 0);
        QVERIFY(rowOf(flat, QStringLiteral("fresh.txt")) > 0);
    }

    void testRootChangeClearsExpansion()
    {
        QTemporaryDir other;
        QVERIFY(other.isValid());
        QFile file(QDir(other.path()).filePath(QStringLiteral("solo.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArrayLiteral("x"));
        file.close();

        FileTreeModel source;
        FileTreeFlatModel flat;
        flat.setSourceModel(&source);
        source.setRootPath(m_root);
        flat.toggleExpanded(0);
        QCOMPARE(flat.rowCount(), 5);

        // 换根 = 另一棵树：旧展开键失效清零，投影只剩新顶层。
        source.setRootPath(other.path());
        QCOMPARE(flat.rowCount(), 1);
        QCOMPARE(flat.data(flat.index(0, 0), FileTreeFlatModel::NameRole),
                 QStringLiteral("solo.txt"));

        // 切回原根：Beta 处于收起状态（展开状态没有跨根残留）。
        source.setRootPath(m_root);
        QCOMPARE(flat.rowCount(), 4);
        QCOMPARE(flat.data(flat.index(0, 0),
                           FileTreeFlatModel::ExpandedRole).toBool(), false);
    }

private:
    /// 按相对路径在投影里找行（相对路径是投影的唯一行身份）。
    static int rowOf(const FileTreeFlatModel &flat, const QString &relativePath)
    {
        for (int row = 0; row < flat.rowCount(); ++row) {
            if (flat.data(flat.index(row, 0),
                          FileTreeFlatModel::RelativePathRole)
                    .toString() == relativePath) {
                return row;
            }
        }
        return -1;
    }

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

#include "tst_filetreeflatmodel.moc"
AWB_TEST(TestFileTreeFlatModel)
