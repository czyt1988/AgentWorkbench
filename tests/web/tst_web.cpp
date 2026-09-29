#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include "core/Settings.h"
#include "web/WebTabsFacade.h"
#include "web/WebTabsModel.h"

using awb::core::Settings;
using awb::web::WebTab;
using awb::web::WebTabsFacade;
using awb::web::WebTabsModel;

/// 测 web 模块的 Web 标签页（不带 Qt Web Engine）：内嵌表面手工注册，让
/// openTab() 走标签路径而不是外置浏览器回退（tst_web 在任何配置下都能跑）。
/// 覆盖同 agent 复用标签、关标签不动进程、离线/在线状态迁移、表面解析、
/// Ctrl+Tab 循环、活动标签的行偏移回归、LRU 释放、会话 URL 重定向与
/// Home 页（web.homeUrl）。
class TestWebTabs : public QObject
{
    Q_OBJECT

    private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        // 干净的 settings.json：web.homeUrl 的测试不读上一个用例的残留。
        QDir().mkpath(QFileInfo(Settings::settingsFilePath()).absolutePath());
        QFile::remove(Settings::settingsFilePath());
    }

    // web.homeUrl 的 Home 行为：留空时 openHome 无效果；配置后按保留
    // agent id "home" 开标签、重复调用激活既有标签不重复开；setHomeUrl
    // 写盘、新实例读得回。
    void testHomeUrl()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));

        // 留空：no-op（Web 页的 Home 按钮保持回到 agent 列表）。
        QVERIFY(web.homeUrl().isEmpty());
        QVERIFY(web.openHome().isEmpty());
        QCOMPARE(web.tabs()->rowCount(), 0);

        // 配置后：开 Home 标签。
        web.setHomeUrl(QStringLiteral("http://127.0.0.1:8080"));
        QCOMPARE(web.homeUrl(), QStringLiteral("http://127.0.0.1:8080"));
        const QString id = web.openHome();
        QVERIFY(!id.isEmpty());
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(web.tabs()->tabById(id)->agentId(), QStringLiteral("home"));
        QCOMPARE(web.tabs()->tabById(id)->url().toString(),
                 QStringLiteral("http://127.0.0.1:8080"));

        // 再按 Home：同一 agent 的既有标签被激活，不重复开。
        const QString again = web.openHome();
        QCOMPARE(again, id);
        QCOMPARE(web.tabs()->rowCount(), 1);

        // 落盘：新 Settings 实例读得回。
        Settings reloaded;
        QCOMPARE(reloaded.webOptions().homeUrl,
                 QStringLiteral("http://127.0.0.1:8080"));
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

        // 再开同一个 agent 激活既有标签。
        const QString second = web.openTab(fields);
        QCOMPARE(second, first);
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(web.activeTabId(), first);
    }

    // 关闭只丢标签本身——agent 进程毫发无损（启动器有自己的 PID 簿记）。
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
        web.closeTab(QStringLiteral("missing")); // 不崩溃
    }

    // 离线/在线的跨域规则。
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

        // agent 回来 → loading（表面重载）。
        web.markOnlineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("loading"));

        // agent 仍在线时的 error 标签（如 token 门禁返回 HTTP 401）不被
        // 自动重载——那曾是一个每 3 秒重试的死循环。
        web.setTabState(id, QStringLiteral("error"));
        web.markOnlineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("error"));

        // agent 掉线期间观察到的 error 变成 offline，正常的恢复路径
        // （agent 回来 → loading）因此依然可用。
        web.setTabState(id, QStringLiteral("error"));
        web.markOfflineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("offline"));
        web.markOnlineForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(tab->state(), QStringLiteral("loading"));

        // 再次离线，然后删除 → 标签整个关掉。
        web.setTabState(id, QStringLiteral("ready"));
        web.markOfflineForAgent(QStringLiteral("kimi-code"));
        web.closeTabsForAgent(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 0);

        // 未知 agent：no-op。
        web.markOfflineForAgent(QStringLiteral("ghost"));
        web.markOnlineForAgent(QStringLiteral("ghost"));
    }

    // 表面解析：注册过的类型能解析，未知类型返回空，`external` 永远存在。
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

    // Ctrl+Tab 循环换到头会回绕，并跟踪活动 id。
    void testStepActiveTab()
    {
        Settings settings;
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));

        web.stepActiveTab(1); // 还没有标签：no-op
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
        QCOMPARE(web.activeTabId(), idA); // 回绕了
        web.stepActiveTab(-1);
        QCOMPARE(web.activeTabId(), idB);
    }

    // 关掉活动标签左侧的标签时，活动标签仍指向同一个标签（行号整体下移）
    // ——评审发现的 activeIndex 回归。
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
        // 最后打开的标签是活动标签。
        QCOMPARE(web.activeTabId(), ids.at(2));

        // 关掉第一个标签：活动标签必须仍是 ids[2]，不能滑到邻居身上。
        web.closeTab(ids.at(0));
        QCOMPARE(web.tabs()->rowCount(), 2);
        QCOMPARE(web.activeTabId(), ids.at(2));

        // 关掉活动标签本身则激活它右侧的邻居。
        web.closeTab(ids.at(2));
        QCOMPARE(web.activeTabId(), ids.at(1));
    }

    // 超过 maxLiveTabs 后最久未用的非活动视图被释放（标签保留，状态
    // "released"）。
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
        // 默认 maxLiveTabs = 8：10 个标签里最新的那个是活动的，
        // 最老的非活动标签被释放了。
        QVERIFY(released >= 1);
        // 活动标签绝不释放。
        QVERIFY(web.tabs()->tabById(web.activeTabId())->state()
                != QStringLiteral("released"));

        // 重新打开恢复视图。
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

    // 捕获到的会话 URL（dsh 的每进程 token）重定向已开的标签：换 URL、清
    // error、视图重载。未知 agent 与空 URL 是 no-op。
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

        // 该 agent 没有标签 / URL 无效：什么都不发生。
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
