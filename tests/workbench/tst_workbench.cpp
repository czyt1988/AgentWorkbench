#include "awbtest.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentRepository.h"
#include "agentcatalog/AgentsFacade.h"
#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/UiServices.h"
#include "web/WebTabsFacade.h"
#include "web/WebTabsModel.h"
#include "workbench/WorkbenchContext.h"

#include <QDesktopServices>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using awb::agentcatalog::AgentRepository;
using awb::agentcatalog::AgentsFacade;
using awb::core::Settings;
using awb::shell::NavigationModel;
using awb::shell::Notifications;
using awb::shell::PageDescriptor;
using awb::shell::UiServices;
using awb::web::WebTabsFacade;
using awb::workbench::WorkbenchContext;

namespace {

/// 捕获「本应交给浏览器打开」的 URL，测试就永远不会真的拉起浏览器
/// （QDesktopServices 会路由给已安装的处理器而不是操作系统）。
class UrlRecorder : public QObject
{
    Q_OBJECT
public:
    QUrl lastUrl;
public Q_SLOTS:
    void record(const QUrl &url) { lastUrl = url; }
};

} // namespace

/**
 * @brief 注册 WorkbenchContext 要在其间导航的页面
 *
 * "agents" 必须也注册——NavigationModel 拒绝未知 id。
 *
 * @param nav 目标导航模型
 */
void registerNavPages(NavigationModel *nav)
{
    PageDescriptor agentsPage;
    agentsPage.id = QStringLiteral("agents");
    QVERIFY2(nav->registerPage(agentsPage), "agents page");
    PageDescriptor webPage;
    webPage.id = QStringLiteral("web");
    QVERIFY2(nav->registerPage(webPage), "web page");
}

/// 测 workbench::WorkbenchContext 的跨域意图：openWeb 建标签并切页、外置
/// 表面策略不建标签也不切页、split-button 的外置路线把最终 URL（含 token
/// 片段）交给系统浏览器。
class TestWorkbenchContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // openWeb 既建标签又把用户带到 web 页；外置表面策略不开标签，
    // 也不得切换页面。
    void testOpenWebNavigates()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository seed(tmp.path());
            seed.load();
        }
        Settings settings;
        AgentsFacade agents(&settings, tmp.path());
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));
        NavigationModel nav;
        Notifications notifications;
        UiServices ui;
        WorkbenchContext workbench(&nav, &ui, &notifications, &agents,
                                   &web, &settings);

        registerNavPages(&nav);
        nav.setCurrentPageId(QStringLiteral("agents"));

        // agent 必须存在（随包默认种进了模型）。
        const int row = agents.agentModel()->indexOf(
            QStringLiteral("kimi-code"));
        QVERIFY(row >= 0);

        workbench.openWeb(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(nav.currentPageId(), QStringLiteral("web"));

        // 离开页面再打开会激活既有标签（不重复建）并切回 web 页。
        nav.setCurrentPageId(QStringLiteral("agents"));
        workbench.openWeb(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(nav.currentPageId(), QStringLiteral("web"));

        // 未知 agent：不建标签、不导航、不崩溃。
        nav.setCurrentPageId(QStringLiteral("agents"));
        workbench.openWeb(QStringLiteral("no-such-agent"));
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(nav.currentPageId(), QStringLiteral("agents"));
    }

    // 外置表面策略下 openWeb 把 URL 交给浏览器：不建标签，也不切到
    // web 页。
    void testOpenWebExternalSurfaceDoesNotNavigate()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository seed(tmp.path());
            seed.load();
        }
        Settings settings;
        settings.setWebSurface(QStringLiteral("external"));
        AgentsFacade agents(&settings, tmp.path());
        WebTabsFacade web(&settings);
        NavigationModel nav;
        Notifications notifications;
        UiServices ui;
        WorkbenchContext workbench(&nav, &ui, &notifications, &agents,
                                   &web, &settings);

        registerNavPages(&nav);
        nav.setCurrentPageId(QStringLiteral("agents"));

        workbench.openWeb(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 0);
        QCOMPARE(nav.currentPageId(), QStringLiteral("agents"));
    }

    // 卡片上 split 按钮的另一条路线：不建标签、不导航，把最终 URL（含
    // token 片段）交给系统浏览器。
    void testOpenWebExternalHandsUrlToBrowser()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository seed(tmp.path());
            seed.load();
        }
        Settings settings;
        AgentsFacade agents(&settings, tmp.path());
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));
        NavigationModel nav;
        Notifications notifications;
        UiServices ui;
        WorkbenchContext workbench(&nav, &ui, &notifications, &agents,
                                   &web, &settings);

        registerNavPages(&nav);
        nav.setCurrentPageId(QStringLiteral("agents"));

        UrlRecorder recorder;
        QDesktopServices::setUrlHandler(QStringLiteral("http"), &recorder,
                                        "record");

        workbench.openWebExternal(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 0);
        QCOMPARE(nav.currentPageId(), QStringLiteral("agents"));
        QVERIFY(recorder.lastUrl.isValid());
        QCOMPARE(recorder.lastUrl.host(), QStringLiteral("127.0.0.1"));

        // 未知 agent：什么都不交出去，也不崩溃。
        recorder.lastUrl.clear();
        workbench.openWebExternal(QStringLiteral("no-such-agent"));
        QVERIFY(!recorder.lastUrl.isValid());

        QDesktopServices::unsetUrlHandler(QStringLiteral("http"));
    }
};

AWB_TEST(TestWorkbenchContext)
#include "tst_workbench.moc"

/**
 * @brief workbench 测试套件入口（今天只有一个类，与其它多类套件一样经
 *        awbtest.h 注册）
 */
int main(int argc, char *argv[])
{
    return awbRunRegisteredTests(argc, argv);
}
