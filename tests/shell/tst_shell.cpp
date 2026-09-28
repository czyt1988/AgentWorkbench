#include "awbtest.h"

#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/ShellController.h"
#include "shell/UiServices.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

using awb::core::Settings;
using awb::shell::NavigationModel;
using awb::shell::Notifications;
using awb::shell::PageDescriptor;
using awb::shell::ShellController;
using awb::shell::UiServices;

namespace {

PageDescriptor makePage(const QString &id, int order = 10)
{
    PageDescriptor page;
    page.id = id;
    page.title = id;
    page.iconSource = QStringLiteral("qrc:/icons/default.svg");
    page.source = QStringLiteral("qrc:/qt/qml/AgentWorkbench/%1/%2.qml")
                      .arg(id, id);
    page.order = order;
    return page;
}

} // namespace

class TestShell : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // A duplicate id is rejected and logged; the first page stays
    void testDuplicatePageRejected()
    {
        NavigationModel nav;
        QVERIFY(nav.registerPage(makePage(QStringLiteral("agents"))));
        QVERIFY(!nav.registerPage(makePage(QStringLiteral("agents"))));
        QCOMPARE(nav.rowCount(), 1);

        QVERIFY(!nav.registerPage(PageDescriptor{})); // no id at all
        QCOMPARE(nav.rowCount(), 1);

        QVERIFY(nav.unregisterPage(QStringLiteral("agents")));
        QVERIFY(!nav.unregisterPage(QStringLiteral("agents")));
        QCOMPARE(nav.rowCount(), 0);
    }

    // Badge updates reach the model role without re-registering.
    void testBadgeUpdate()
    {
        NavigationModel nav;
        QVERIFY(nav.registerPage(makePage(QStringLiteral("agents"))));
        QSignalSpy changed(&nav, &NavigationModel::dataChanged);

        nav.setBadge(QStringLiteral("agents"), QStringLiteral("3"));
        QCOMPARE(changed.count(), 1);
        const QModelIndex idx = nav.index(0, 0);
        QCOMPARE(idx.data(NavigationModel::BadgeRole).toString(),
                 QStringLiteral("3"));

        // Same value: no signal churn.
        nav.setBadge(QStringLiteral("agents"), QStringLiteral("3"));
        QCOMPARE(changed.count(), 1);

        nav.setBadge(QStringLiteral("missing"), QStringLiteral("x"));
        QCOMPARE(changed.count(), 1);
    }

    // Current page selection rejects unknown ids and reports the descriptor
    // the workspace Loader needs.
    void testCurrentPage()
    {
        NavigationModel nav;
        QVERIFY(nav.registerPage(makePage(QStringLiteral("agents"))));
        QVERIFY(nav.registerPage(makePage(QStringLiteral("settings"), 100)));

        QSignalSpy spy(&nav, &NavigationModel::currentPageChanged);
        nav.setCurrentPageId(QStringLiteral("settings"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(nav.currentPageId(), QStringLiteral("settings"));
        QCOMPARE(nav.currentPage().value(QStringLiteral("source")).toString(),
                 QStringLiteral("qrc:/qt/qml/AgentWorkbench/settings/settings.qml"));

        nav.setCurrentPageId(QStringLiteral("nope"));
        QCOMPARE(nav.currentPageId(), QStringLiteral("settings"));
        QCOMPARE(spy.count(), 1);
    }

    // keepAlive pages land in the keepAlivePages snapshot and expose the
    // flag through page(); the workspace keeps them alive instead of
    // destroying them on every switch. A keepAlive flag flipping later must
    // re-evaluate the snapshot (pagesChanged), and disabled keepAlive pages
    // are skipped — an unreachable page must not hold a resident instance.
    void testKeepAlivePages()
    {
        NavigationModel nav;
        PageDescriptor plain = makePage(QStringLiteral("agents"));
        QVERIFY(nav.registerPage(plain));

        PageDescriptor web = makePage(QStringLiteral("web"), 20);
        web.keepAlive = true;
        QVERIFY(nav.registerPage(web));

        // page() exposes the flag so Workspace can tell a keepAlive current
        // page apart from a regular one (and keep the plain Loader away
        // from it).
        QCOMPARE(nav.page(QStringLiteral("web"))
                     .value(QStringLiteral("keepAlive")).toBool(), true);
        QCOMPARE(nav.page(QStringLiteral("agents"))
                     .value(QStringLiteral("keepAlive")).toBool(), false);

        QVariantList alive = nav.keepAlivePages();
        QCOMPARE(alive.size(), 1);
        QCOMPARE(alive.first().toMap()
                     .value(QStringLiteral("id")).toString(),
                 QStringLiteral("web"));
        QCOMPARE(alive.first().toMap()
                     .value(QStringLiteral("source")).toString(),
                 QStringLiteral("qrc:/qt/qml/AgentWorkbench/web/web.qml"));

        // Notifiable so a Repeater binding re-evaluates on registration.
        QSignalSpy spy(&nav, &NavigationModel::pagesChanged);
        PageDescriptor extra = makePage(QStringLiteral("tools"), 40);
        extra.keepAlive = true;
        QVERIFY(nav.registerPage(extra));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(nav.keepAlivePages().size(), 2);

        // Disabled pages are not reachable — no resident instance for them.
        PageDescriptor off = makePage(QStringLiteral("logs"), 50);
        off.keepAlive = true;
        off.enabled = false;
        QVERIFY(nav.registerPage(off));
        QCOMPARE(nav.keepAlivePages().size(), 2);
    }

    // Every method QML calls on the nav/shell singletons must be reachable
    // through the meta-object — invokeMethod is exactly how QML resolves a
    // call, and a bare Q_PROPERTY WRITE or plain method is NOT registered.
    // Regression: sidebar/Ctrl+N clicks and the settings page's surface/
    // flags switches threw "…is not a function" because setCurrentPageId /
    // setWebSurface / setWebChromiumFlags were not Q_INVOKABLE (found by a
    // manual run after the review — page-load smoke never clicks).
    void testQmlCalledMethodsAreInvokable()
    {
        NavigationModel nav;
        QVERIFY(nav.registerPage(makePage(QStringLiteral("agents"))));
        QVERIFY(nav.registerPage(makePage(QStringLiteral("settings"), 100)));
        QVERIFY2(QMetaObject::invokeMethod(&nav, "setCurrentPageId",
                                           Q_ARG(QString, "settings")),
                 "nav.setCurrentPageId is not invokable — sidebar/Ctrl+N "
                 "clicks would throw TypeError in QML");
        QCOMPARE(nav.currentPageId(), QStringLiteral("settings"));

        QVERIFY(QDir().mkpath(
            QFileInfo(Settings::settingsFilePath()).absolutePath()));
        QFile::remove(Settings::settingsFilePath());
        {
            Settings settings;
            ShellController shell(&settings);
            QVERIFY2(QMetaObject::invokeMethod(&shell, "setWebSurface",
                                               Q_ARG(QString, "external")),
                     "shell.setWebSurface is not invokable — the settings "
                     "page's surface switch silently does nothing");
            QCOMPARE(shell.webSurface(), QStringLiteral("external"));
        QVERIFY(QMetaObject::invokeMethod(&shell, "setWebChromiumFlags",
                                          Q_ARG(QString, "--disable-gpu")));
            QCOMPARE(shell.webChromiumFlags(), QStringLiteral("--disable-gpu"));
        }
        QFile::remove(Settings::settingsFilePath());
    }

    // UiServices 的 Q_INVOKABLE 返回 OpResult：QML 调用端按 moc 记录的
    // 类型名查 QMetaType 注册表（守卫的完整说明见
    // awbUnresolvedQmlCallTypes）；注册序与 app/main.cpp 一致。
    void testUiServicesQmlMethodTypesResolve()
    {
        qRegisterMetaType<awb::core::OpResult>();
        UiServices ui;
        const QStringList failures = awbUnresolvedQmlCallTypes(&ui);
        QVERIFY2(failures.isEmpty(),
                 qPrintable(failures.join(QLatin1String("\n"))));
    }

    // Sidebar collapse and window geometry persist to settings.json and
    // come back after a fresh controller.
    void testSidebarStatePersists()
    {
        QVERIFY(QDir().mkpath(
            QFileInfo(Settings::settingsFilePath()).absolutePath()));
        QFile::remove(Settings::settingsFilePath());

        {
            Settings settings;
            ShellController shell(&settings);
            QVERIFY(!shell.sidebarCollapsed());
            shell.setSidebarCollapsed(true);
            shell.saveWindowSize(1200, 800);
            shell.setLastPageId(QStringLiteral("settings"));
        }
        {
            Settings settings;
            ShellController shell(&settings);
            QVERIFY(shell.sidebarCollapsed());
            QCOMPARE(shell.windowWidth(), 1200);
            QCOMPARE(shell.windowHeight(), 800);
            QCOMPARE(shell.lastPageId(), QStringLiteral("settings"));
        }
        QFile::remove(Settings::settingsFilePath());
    }

    // Clipboard writes report success and failure as OpResult.
    void testClipboardResult()
    {
        UiServices ui;
        const auto ok = ui.copyText(QStringLiteral("hello-awb"));
        QVERIFY(ok.ok);
        QVERIFY(ok.error.isEmpty());
        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QStringLiteral("hello-awb"));

        const auto empty = ui.copyText(QString());
        QVERIFY(!empty.ok);
        QVERIFY(!empty.error.isEmpty());
    }

    // Toasts queue behind the visible three; dismiss removes by id.
    void testToastQueueAndDismiss()
    {
        Notifications toasts;
        QCOMPARE(toasts.durationFor(QStringLiteral("info")), 3000);
        QCOMPARE(toasts.durationFor(QStringLiteral("warning")), 5000);
        QCOMPARE(toasts.durationFor(QStringLiteral("error")), 8000);

        for (int i = 0; i < 5; ++i)
            toasts.notify(QStringLiteral("info"), QStringLiteral("T"),
                          QString::number(i));
        QCOMPARE(toasts.rowCount(), 5); // 2 queue behind the visible 3
        const QString firstId =
            toasts.index(0, 0).data(Notifications::IdRole).toString();
        QVERIFY(!firstId.isEmpty());

        toasts.dismiss(firstId);
        QCOMPARE(toasts.rowCount(), 4);
        toasts.dismiss(QStringLiteral("missing"));
        QCOMPARE(toasts.rowCount(), 4);
    }
};

QTEST_MAIN(TestShell)
#include "tst_shell.moc"
