#include "awbtest.h"

#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/ShellController.h"
#include "shell/UiServices.h"

#include <QClipboard>
#include <QColor>
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

/**
 * @brief 构造一个最小可注册的页面描述符
 *
 * @param id 页面 id（同时充当 title 与 QML source 的一部分）
 * @param order 排序权重，默认 10
 * @return 填好 id/title/icon/source/order 的 PageDescriptor
 */
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

/// 测 shell 模块的导航与壳层服务：NavigationModel 的页面注册/徽标/当前页/
/// keepAlive 快照、QML 调用方法的可调用性回归、ShellController 的状态持久化、
/// UiServices 的剪贴板 OpResult，以及 Notifications 的队列与按 id 撤销。
class TestShell : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // 重复 id 被拒绝并记日志；第一个页面保持不变。
    void testDuplicatePageRejected()
    {
        NavigationModel nav;
        QVERIFY(nav.registerPage(makePage(QStringLiteral("agents"))));
        QVERIFY(!nav.registerPage(makePage(QStringLiteral("agents"))));
        QCOMPARE(nav.rowCount(), 1);

        QVERIFY(!nav.registerPage(PageDescriptor{})); // 连 id 都没有
        QCOMPARE(nav.rowCount(), 1);

        QVERIFY(nav.unregisterPage(QStringLiteral("agents")));
        QVERIFY(!nav.unregisterPage(QStringLiteral("agents")));
        QCOMPARE(nav.rowCount(), 0);
    }

    // 徽标更新直达 model role，无需重新注册。
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

        // 同值不重发：不制造信号抖动。
        nav.setBadge(QStringLiteral("agents"), QStringLiteral("3"));
        QCOMPARE(changed.count(), 1);

        nav.setBadge(QStringLiteral("missing"), QStringLiteral("x"));
        QCOMPARE(changed.count(), 1);
    }

    // 当前页选择拒绝未知 id，并上报工作区 Loader 需要的描述符。
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

    // keepAlive 页面进 keepAlivePages 快照并经 page() 暴露标志；工作区让
    // 它们常驻而不是每次切换都销毁。标志后来翻转必须让快照重算
    // （pagesChanged），禁用的 keepAlive 页被跳过——够不着的页面不该占着
    // 常驻实例。
    void testKeepAlivePages()
    {
        NavigationModel nav;
        PageDescriptor plain = makePage(QStringLiteral("agents"));
        QVERIFY(nav.registerPage(plain));

        PageDescriptor web = makePage(QStringLiteral("web"), 20);
        web.keepAlive = true;
        QVERIFY(nav.registerPage(web));

        // page() 暴露该标志，Workspace 才能把 keepAlive 的当前页与普通页
        // 区分开（并让普通 Loader 不碰它）。
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

        // 可通知：注册时 Repeater 的绑定会重算。
        QSignalSpy spy(&nav, &NavigationModel::pagesChanged);
        PageDescriptor extra = makePage(QStringLiteral("tools"), 40);
        extra.keepAlive = true;
        QVERIFY(nav.registerPage(extra));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(nav.keepAlivePages().size(), 2);

        // 禁用的页面不可达——不给它们常驻实例。
        PageDescriptor off = makePage(QStringLiteral("logs"), 50);
        off.keepAlive = true;
        off.enabled = false;
        QVERIFY(nav.registerPage(off));
        QCOMPARE(nav.keepAlivePages().size(), 2);
    }

    // QML 在 nav/shell 单例上调用的每个方法都必须经 meta-object 可达
    // ——invokeMethod 正是 QML 解析调用的方式，裸的 Q_PROPERTY WRITE 或
    // 普通方法不在表里。回归：侧栏/Ctrl+N 点击与设置页的 surface/flags
    // 开关曾因 setCurrentPageId / setWebSurface / setWebChromiumFlags 没有
    // Q_INVOKABLE 而抛「…is not a function」（评审后手动运行才发现——按页
    // 加载的冒烟从不点击）。
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
                 qPrintable(failures.join(QStringLiteral("\n"))));
    }

    // 侧栏折叠与窗口几何持久化到 settings.json，新的 controller 读得回来。
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

    // 剪贴板写入以 OpResult 上报成功与失败。
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

    // 颜色选择器的最近记忆（进程内）：最新在前、重复去重并移到队首、
    // 上限 10 条、非法串忽略；recentColorsChanged 边沿触发——队首同色
    // 时不重发（订阅方不会无谓重排网格）。
    void testColorMemory()
    {
        UiServices ui;
        QSignalSpy spy(&ui, &UiServices::recentColorsChanged);

        ui.rememberColor(QStringLiteral("#ff0000"));
        QCOMPARE(ui.recentColors(),
                 QStringList{QStringLiteral("#ff0000")});
        QCOMPARE(spy.count(), 1);

        // 大小写与首尾空白归一成同一颜色：队首同色短路，不发信号。
        ui.rememberColor(QStringLiteral("#FF0000"));
        QCOMPARE(ui.recentColors(),
                 QStringList{QStringLiteral("#ff0000")});
        QCOMPARE(spy.count(), 1);

        ui.rememberColor(QStringLiteral("#00ff00"));
        ui.rememberColor(QStringLiteral("#0000ff"));
        QCOMPARE(ui.recentColors().first(), QStringLiteral("#0000ff"));
        QCOMPARE(ui.recentColors().size(), 3);

        // 重复的旧色：删旧插队首，记忆仍无重复。
        ui.rememberColor(QStringLiteral("#ff0000"));
        QCOMPARE(ui.recentColors().first(), QStringLiteral("#ff0000"));
        QCOMPARE(ui.recentColors().size(), 3);
        QCOMPARE(ui.recentColors().count(QStringLiteral("#ff0000")), 1);

        // 超出上限：灌 12 个不同颜色（队首同色会被短路，必须互不相同），
        // 记忆只保留最近 10 个。
        for (int i = 0; i < 12; ++i) {
            ui.rememberColor(QStringLiteral("#")
                             + QString::number(i, 16).rightJustified(6, '0'));
        }
        QCOMPARE(ui.recentColors().size(), 10);
        // 最新在前：队首是最后记进来的，最早的一条（#000000）被挤出。
        QCOMPARE(ui.recentColors().first(), QStringLiteral("#00000b"));
        QVERIFY(!ui.recentColors().contains(QStringLiteral("#000000")));

        // 非法输入：忽略，不发信号也不动记忆。
        spy.clear();
        ui.rememberColor(QString());
        ui.rememberColor(QStringLiteral("not-a-color"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(ui.recentColors().size(), 10);

        // 支撑数据是常量：主题色 10 列、标准色 10 项，全部可解析。
        QCOMPARE(ui.colorThemes().size(), 10);
        QCOMPARE(ui.standardColors().size(), 10);
        for (const QString &hex : ui.colorThemes()) {
            QVERIFY2(QColor(hex).isValid(),
                     qPrintable(QStringLiteral("bad theme color: %1").arg(hex)));
        }
        for (const QString &hex : ui.standardColors()) {
            QVERIFY2(QColor(hex).isValid(),
                     qPrintable(QStringLiteral("bad standard color: %1").arg(hex)));
        }
    }

    // toast 排队挤在可见的三个后面；dismiss 按 id 移除。
    void testToastQueueAndDismiss()
    {
        Notifications toasts;
        QCOMPARE(toasts.durationFor(QStringLiteral("info")), 3000);
        QCOMPARE(toasts.durationFor(QStringLiteral("warning")), 5000);
        QCOMPARE(toasts.durationFor(QStringLiteral("error")), 8000);

        for (int i = 0; i < 5; ++i) {
            toasts.notify(QStringLiteral("info"), QStringLiteral("T"),
                          QString::number(i));
        }
        QCOMPARE(toasts.rowCount(), 5); // 2 条排在可见的 3 条后面
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
