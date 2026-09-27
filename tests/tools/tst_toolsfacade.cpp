#include "awbtest.h"

#include "core/Paths.h"
#include "tools/FileTreeModel.h"
#include "tools/ToolsFacade.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::Paths;
using awb::tools::ToolsFacade;

// 门面层：工作区校验/MRU/淘汰与文件树根的联动、草稿跨实例往返。
class TestToolsFacade : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY2(m_dir->isValid(), "temporary data root");
        Paths::setDataRootForTesting(m_dir->path());

        m_ws = std::make_unique<QTemporaryDir>();
        QVERIFY2(m_ws->isValid(), "temporary workspace");
    }

    void cleanup()
    {
        Paths::setDataRootForTesting(QString());
        m_dir.reset();
        m_ws.reset();
    }

    void testAddWorkspaceValidation()
    {
        ToolsFacade facade(Paths::dataRoot());

        // 不存在的路径被拒，且不进列表。
        QVERIFY(!facade.addWorkspace(QStringLiteral("/no/such/dir")).ok);
        QCOMPARE(facade.workspaces().size(), 0);

        QVERIFY(facade.addWorkspace(m_ws->path()).ok);
        QCOMPARE(facade.workspaces().size(), 1);
        QCOMPARE(facade.currentWorkspace(), m_ws->path());
    }

    void testSwitchWorkspaceRetargetsTree()
    {
        ToolsFacade facade(Paths::dataRoot());
        QDir(m_ws->path()).mkdir(QStringLiteral("marker-a"));
        QVERIFY(facade.addWorkspace(m_ws->path()).ok);
        QCOMPARE(facade.fileTreeModel()->rootPath(),
                 QDir(m_ws->path()).absolutePath());
        QCOMPARE(facade.fileTreeModel()->rowCount(QModelIndex()), 1);

        // 第二个工作区：切换后树根与行数都跟着换。
        auto ws2 = std::make_unique<QTemporaryDir>();
        QDir(ws2->path()).mkdir(QStringLiteral("marker-b"));
        QVERIFY(facade.addWorkspace(ws2->path()).ok);
        QCOMPARE(facade.currentWorkspace(), ws2->path());
        QCOMPARE(facade.fileTreeModel()->rowCount(QModelIndex()), 1);
        QCOMPARE(facade.fileTreeModel()->data(
                         facade.fileTreeModel()->index(0, 0),
                         awb::tools::FileTreeModel::NameRole),
                 QStringLiteral("marker-b"));

        // 切回第一个：MRU 把它挪到队首，树也换回去。
        facade.setCurrentWorkspace(m_ws->path());
        QCOMPARE(facade.workspaces().constFirst(), m_ws->path());
        QCOMPARE(facade.fileTreeModel()->data(
                         facade.fileTreeModel()->index(0, 0),
                         awb::tools::FileTreeModel::NameRole),
                 QStringLiteral("marker-a"));

        // 列表外路径不生效。
        facade.setCurrentWorkspace(QStringLiteral("/no/such/dir"));
        QCOMPARE(facade.currentWorkspace(), m_ws->path());
    }

    void testRemoveWorkspace()
    {
        ToolsFacade facade(Paths::dataRoot());
        auto ws2 = std::make_unique<QTemporaryDir>();
        QVERIFY(facade.addWorkspace(m_ws->path()).ok);
        QVERIFY(facade.addWorkspace(ws2->path()).ok);

        // 移除当前项：树根顺延到剩余队首，current 同步。
        QVERIFY(facade.removeWorkspace(ws2->path()).ok);
        QCOMPARE(facade.workspaces().size(), 1);
        QCOMPARE(facade.currentWorkspace(), m_ws->path());

        // 移除不存在的项被拒。
        QVERIFY(!facade.removeWorkspace(QStringLiteral("/no/such/dir")).ok);
    }

    void testDraftPersistsAcrossInstances()
    {
        {
            ToolsFacade facade(Paths::dataRoot());
            facade.setDraft(QStringLiteral("line one\nline two"));
            // 防抖未到期就析构：析构兜底必须把草稿写进去。
        }
        ToolsFacade reloaded(Paths::dataRoot());
        QCOMPARE(reloaded.draft(), QStringLiteral("line one\nline two"));
    }

    void testRefreshSignal()
    {
        ToolsFacade facade(Paths::dataRoot());
        QDir(m_ws->path()).mkdir(QStringLiteral("dir"));
        QVERIFY(facade.addWorkspace(m_ws->path()).ok);

        QSignalSpy finished(&facade, &ToolsFacade::refreshFinished);
        facade.refresh();
        QCOMPARE(finished.count(), 1);
    }

    // ToolsPage.qml 调用的方法面：走 QMetaObject::invokeMethod 复现 QML 的
    // 真实解析路径（同 tst_shell::testQmlCalledMethodsAreInvokable 的做法，
    // 抓「声明了但没进 metaobject 方法表」这类只在线上点击时爆的缺陷）。
    void testFileReferenceFormat()
    {
        ToolsFacade facade(Paths::dataRoot());

        // 用户确认的插入格式：反引号包裹的 ./相对路径（正斜杠）。
        QCOMPARE(facade.fileReference(QStringLiteral("src/app.cpp")),
                 QStringLiteral("`./src/app.cpp`"));
        QCOMPARE(facade.fileReference(QStringLiteral("docs/guide.md")),
                 QStringLiteral("`./docs/guide.md`"));
        // 目录引用与文件同格式。
        QCOMPARE(facade.fileReference(QStringLiteral("src")),
                 QStringLiteral("`./src`"));
        QVERIFY(facade.fileReference(QString()).isEmpty());
    }

    void testQmlCalledMethodsAreInvokable()
    {
        QDir(m_ws->path()).mkdir(QStringLiteral("sub"));
        ToolsFacade facade(Paths::dataRoot());
        QVERIFY(facade.addWorkspace(m_ws->path()).ok);

        QVERIFY2(QMetaObject::invokeMethod(&facade, "addWorkspace",
                                           Q_ARG(QString, m_ws->path())),
                 "tools.addWorkspace is not invokable");
        QVERIFY2(QMetaObject::invokeMethod(&facade, "refresh"),
                 "tools.refresh is not invokable");
        // Q_PROPERTY WRITE 侧走属性系统（QML 赋值的真实路径），
        // 方法名调用只对 Q_INVOKABLE 生效而 WRITE 不要求。
        QVERIFY2(facade.setProperty("currentWorkspace", m_ws->path()),
                 "tools.currentWorkspace is not a writable property");
        QVERIFY2(facade.setProperty("draft", QStringLiteral("typed")),
                 "tools.draft is not a writable property");
        QCOMPARE(facade.draft(), QStringLiteral("typed"));

        awb::tools::FileTreeModel *model = facade.fileTreeModel();
        const QModelIndex first = model->index(0, 0);
        QVERIFY2(first.isValid(), "fixture row missing");
        QVERIFY2(QMetaObject::invokeMethod(model, "setNodeExpanded",
                                           Q_ARG(QModelIndex, first),
                                           Q_ARG(bool, true)),
                 "tools.model.setNodeExpanded is not invokable");
        QVERIFY2(QMetaObject::invokeMethod(model, "fetchChildren",
                                           Q_ARG(QModelIndex, first)),
                 "tools.model.fetchChildren is not invokable");
        QModelIndex sub;
        QVERIFY2(QMetaObject::invokeMethod(model, "indexByPath",
                                           Q_RETURN_ARG(QModelIndex, sub),
                                           Q_ARG(QString, QStringLiteral("sub"))),
                 "tools.model.indexByPath is not invokable");
        QVERIFY(sub.isValid());

        // removeWorkspace 放最后：它会清掉当前工作区，树随之变空。
        QVERIFY2(QMetaObject::invokeMethod(&facade, "removeWorkspace",
                                           Q_ARG(QString, m_ws->path())),
                 "tools.removeWorkspace is not invokable");
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QTemporaryDir> m_ws;
};

AWB_TEST(TestToolsFacade)
#include "tst_toolsfacade.moc"
