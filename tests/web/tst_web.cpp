#include <QtTest>
#include <QStandardPaths>
#include <QUrl>

#include "core/Settings.h"
#include "web/WebTabsFacade.h"
#include "web/WebTabsModel.h"

using awb::core::Settings;
using awb::web::WebTab;
using awb::web::WebTabsFacade;
using awb::web::WebTabsModel;

// Web tabs without Qt WebEngine: the embedded surface is registered
// manually so openTab() takes the tab path instead of the external
// fallback (tst_web runs in any configuration).
class TestWebTabs : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void testSameAgentReusesTab()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));

        QVariantMap fields;
        fields[QStringLiteral("agentId")] = QStringLiteral("kimi-code");
        fields[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:58627");
        fields[QStringLiteral("title")] = QStringLiteral("Kimi");

        const QString first = web.openTab(fields);
        QVERIFY(!first.isEmpty());
        QCOMPARE(web.tabs()->rowCount(), 1);

        // Opening the same agent again activates the existing tab.
        const QString second = web.openTab(fields);
        QCOMPARE(second, first);
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(web.activeTabId(), first);
    }

    // Closing drops the tab only — nothing about the agent process is
    // touched (the launcher keeps its own PID bookkeeping).
    void testCloseKeepsProcessAlone()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));
        QVariantMap fields;
        fields[QStringLiteral("agentId")] = QStringLiteral("opencode");
        fields[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:4096");
        const QString id = web.openTab(fields);
        QVERIFY(!id.isEmpty());

        web.closeTab(id);
        QCOMPARE(web.tabs()->rowCount(), 0);
        QVERIFY(web.activeTabId().isEmpty());
        web.closeTab(QStringLiteral("missing")); // no crash
    }

    // The offline/online cross-domain rules.
    void testOfflineOnlineTransitions()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));
        QVariantMap fields;
        fields[QStringLiteral("agentId")] = QStringLiteral("kimi-code");
        fields[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:58627");
        const QString id = web.openTab(fields);
        WebTab *tab = web.tabs()->tabById(id);
        QVERIFY(tab);

        web.setTabState(id, QStringLiteral("ready"));
        web.markOfflineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("offline"));

        // Agent back -> loading (the surface reloads).
        web.markOnlineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("loading"));

        // An error tab with the agent STILL up (e.g. HTTP 401 from a token
        // gate) is NOT auto-reloaded — that was an endless 3s retry loop.
        web.setTabState(id, QStringLiteral("error"));
        web.markOnlineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("error"));

        // An error observed while the agent is down becomes offline, so the
        // normal recovery path (agent back -> loading) still works.
        web.setTabState(id, QStringLiteral("error"));
        web.markOfflineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("offline"));
        web.markOnlineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("loading"));

        // Offline again, then deleted -> the tab is closed entirely.
        web.setTabState(id, QStringLiteral("ready"));
        web.markOfflineForAgent(QStringLiteral("kimi-code"));
        web.closeTabsForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 0);

        // Unknown agent: no-ops.
        web.markOfflineForAgent(QStringLiteral("ghost"));
        web.markOnlineForAgent(QStringLiteral("ghost"));
    }

    // Surface resolution: registered kinds resolve, unknown kinds
    // come back empty, `external` always exists.
    void testSurfaceUrlResolution()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        QVERIFY(web.surfaceUrl(QStringLiteral("external")).isEmpty());
        QVERIFY(web.surfaceUrl(QStringLiteral("embedded")).isEmpty());

        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));
        QCOMPARE(web.surfaceUrl(QStringLiteral("embedded")),
                 QStringLiteral("qrc:/fake/Surface.qml"));
        QVERIFY(web.engineAvailable());
    }

    // Ctrl+Tab cycling wraps around and tracks the active id.
    void testStepActiveTab()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));

        web.stepActiveTab(1); // no tabs yet: no-op
        QVERIFY(web.activeTabId().isEmpty());

        QVariantMap a;
        a[QStringLiteral("agentId")] = QStringLiteral("a");
        a[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:1");
        QVariantMap b = a;
        b[QStringLiteral("agentId")] = QStringLiteral("b");
        b[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:2");
        const QString idA = web.openTab(a);
        const QString idB = web.openTab(b);
        QVERIFY(idA != idB);
        QCOMPARE(web.activeTabId(), idB);

        web.stepActiveTab(1);
        QCOMPARE(web.activeTabId(), idA); // wrapped
        web.stepActiveTab(-1);
        QCOMPARE(web.activeTabId(), idB);
    }

    // Closing a tab LEFT of the active one keeps the active tab pointing at
    // the same tab (rows shift down) — the review's activeIndex regression.
    void testCloseLeftOfActiveKeepsActiveTab()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));

        QStringList ids;
        for (int i = 0; i < 3; ++i) {
            QVariantMap fields;
            fields[QStringLiteral("agentId")] = QStringLiteral("agent-%1").arg(i);
            fields[QStringLiteral("url")] =
                QStringLiteral("http://127.0.0.1:%1").arg(6000 + i);
            ids.append(web.openTab(fields));
        }
        // The last opened tab is active.
        QCOMPARE(web.activeTabId(), ids.at(2));

        // Close the FIRST tab: the active one must stay ids[2], not slip
        // onto a neighbour.
        web.closeTab(ids.at(0));
        QCOMPARE(web.tabs()->rowCount(), 2);
        QCOMPARE(web.activeTabId(), ids.at(2));

        // Closing the active tab itself activates its right neighbour.
        web.closeTab(ids.at(2));
        QCOMPARE(web.activeTabId(), ids.at(1));
    }

    // Past maxLiveTabs the least recently used inactive view is released
    // (tab kept, state "released").
    void testLruRelease()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));

        QStringList ids;
        for (int i = 0; i < 10; ++i) {
            QVariantMap fields;
            fields[QStringLiteral("agentId")] = QStringLiteral("agent-%1").arg(i);
            fields[QStringLiteral("url")] =
                QStringLiteral("http://127.0.0.1:%1").arg(5000 + i);
            ids.append(web.openTab(fields));
        }
        QCOMPARE(web.tabs()->rowCount(), 10);

        int released = 0;
        for (const QString &id : std::as_const(ids)) {
            if (web.tabs()->tabById(id)->state() == QStringLiteral("released")) {
                ++released;
            }
        }
        // Default maxLiveTabs = 8: 10 tabs, the newest is active, the
        // oldest inactive ones got released.
        QVERIFY(released >= 1);
        // The active tab is never released.
        QVERIFY(web.tabs()->tabById(web.activeTabId())->state()
                != QStringLiteral("released"));

        // Reopen restores the view.
        QString releasedId;
        for (const QString &id : ids) {
            if (web.tabs()->tabById(id)->state() == QStringLiteral("released")) {
                releasedId = id;
                break;
            }
        }
        QVERIFY(!releasedId.isEmpty());
        web.reopen(releasedId);
        QCOMPARE(web.tabs()->tabById(releasedId)->state(),
                 QStringLiteral("loading"));
    }

    // A captured session URL (dsh's per-process token) retargets an open
    // tab: URL swap, error cleared, view reloads. Unknown agents and empty
    // URLs are no-ops.
    void testRetargetTabForAgent()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));
        QVariantMap fields;
        fields[QStringLiteral("agentId")] = QStringLiteral("dsh");
        fields[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:3080");
        const QString id = web.openTab(fields);
        WebTab *tab = web.tabs()->tabById(id);
        QVERIFY(tab);

        web.setTabState(id, QStringLiteral("error"));
        web.setTabLastError(id, QStringLiteral("Failed to load ... (HTTP 401)"));

        web.retargetTabForAgent(
            QStringLiteral("dsh"),
            QUrl(QStringLiteral("http://127.0.0.1:3080/?token=abc")));
        QCOMPARE(tab->url().toString(),
                 QStringLiteral("http://127.0.0.1:3080/?token=abc"));
        QCOMPARE(tab->state(), QStringLiteral("loading"));
        QVERIFY(tab->lastError().isEmpty());

        // No tab for the agent / invalid URL: nothing happens.
        web.retargetTabForAgent(QStringLiteral("ghost"),
                                QUrl(QStringLiteral("http://127.0.0.1:1")));
        web.retargetTabForAgent(QStringLiteral("dsh"), QUrl());
        QCOMPARE(tab->url().toString(),
                 QStringLiteral("http://127.0.0.1:3080/?token=abc"));
    }
};

QTEST_MAIN(TestWebTabs)
#include "tst_web.moc"
#include <utility>
