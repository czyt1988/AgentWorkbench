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
    // (specs/03 S4-T1).
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

    // Sidebar collapse and window geometry persist to settings.json and
    // come back after a fresh controller (S4-T9).
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

    // Clipboard writes report success and failure as OpResult (S4-T1).
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
