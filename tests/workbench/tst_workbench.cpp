#include "awbtest.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentRepository.h"
#include "agentcatalog/AgentsFacade.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/UiServices.h"
#include "web/WebTabsFacade.h"
#include "web/WebTabsModel.h"
#include "workbench/EnvironmentCache.h"
#include "workbench/EnvironmentProbe.h"
#include "workbench/EnvironmentService.h"
#include "workbench/WorkbenchContext.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using awb::agentcatalog::AgentRepository;
using awb::agentcatalog::AgentsFacade;
using awb::core::Paths;
using awb::core::Settings;
using awb::shell::NavigationModel;
using awb::shell::Notifications;
using awb::shell::PageDescriptor;
using awb::shell::UiServices;
using awb::web::WebTabsFacade;
using awb::workbench::EnvironmentCache;
using awb::workbench::EnvironmentProbe;
using awb::workbench::EnvironmentService;
using awb::workbench::RuntimeProbe;
using awb::workbench::RuntimeState;
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

/// 测 workbench 的运行时检测：探测结论怎么并入已知状态、缓存怎么写怎么
/// 读、服务构造时怎么恢复。
///
/// 这里**不跑真实探测**（那要起 python/node 子进程，依赖本机装了什么，
/// 违反「测试不依赖本机已安装的工具」）：需要结论的地方直接喂结论。
class TestEnvironmentService : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanup()
    {
        Paths::setDataRootForTesting(QString());
    }

    // 探测拿不到结论（Unknown）时必须原样保留上次的值——一次超时不是卸载。
    void testMergeKeepsLastKnownWhenInconclusive()
    {
        RuntimeState current;
        current.known = true;
        current.installed = true;
        current.version = QStringLiteral("3.11.4");
        current.path = QStringLiteral("D:/Python311/python.exe");

        const RuntimeProbe inconclusive; // 默认就是 Unknown
        QCOMPARE(EnvironmentProbe::merge(current, inconclusive), current);
    }

    // 权威结论必须覆盖上次的值：Missing 清掉版本与路径，Found 换上新值。
    void testMergeAppliesVerdicts()
    {
        RuntimeState current;
        current.known = true;
        current.installed = true;
        current.version = QStringLiteral("3.11.4");
        current.path = QStringLiteral("D:/Python311/python.exe");

        RuntimeProbe missing;
        missing.status = RuntimeProbe::Status::Missing;
        const RuntimeState afterMissing =
            EnvironmentProbe::merge(current, missing);
        QVERIFY(afterMissing.known);
        QVERIFY(!afterMissing.installed);
        QVERIFY(afterMissing.version.isEmpty());
        QVERIFY(afterMissing.path.isEmpty());

        RuntimeProbe found;
        found.status = RuntimeProbe::Status::Found;
        found.version = QStringLiteral("22.20.0");
        found.path = QStringLiteral("C:/nodejs/node.exe");
        const RuntimeState afterFound =
            EnvironmentProbe::merge(afterMissing, found);
        QVERIFY(afterFound.known);
        QVERIFY(afterFound.installed);
        QCOMPARE(afterFound.version, QStringLiteral("22.20.0"));
        QCOMPARE(afterFound.path, QStringLiteral("C:/nodejs/node.exe"));
    }

    // 运行时自报的路径：跳过启动噪声行，只认绝对路径；都没有时用回退值。
    void testParseReportedPath()
    {
        const QString fallback = QStringLiteral("C:/fallback/node.exe");
        const QString reported =
            EnvironmentProbe::parseReportedPath(
                QStringLiteral("C:/nodejs/node.exe\r\n"), fallback);
        QCOMPARE(reported, QDir::toNativeSeparators(
                               QStringLiteral("C:/nodejs/node.exe")));

        // 前面一行噪声（实验特性提示之类）不是绝对路径，要跳过。
        QCOMPARE(EnvironmentProbe::parseReportedPath(
                     QStringLiteral("(node:1) Warning: something\n"
                                    "C:/nodejs/node.exe\n"),
                     fallback),
                 QDir::toNativeSeparators(QStringLiteral("C:/nodejs/node.exe")));

        // 输出里没有绝对路径、或干脆没有输出：回退。
        QCOMPARE(EnvironmentProbe::parseReportedPath(
                     QStringLiteral("command not found\n"), fallback),
                 fallback);
        QCOMPARE(EnvironmentProbe::parseReportedPath(QString(), fallback),
                 fallback);
    }

    // 缓存往返：两个运行时的结论（含「权威判定没装」）都原样回来。
    void testCacheRoundTrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());

        RuntimeState python;
        python.known = true;
        python.installed = true;
        python.version = QStringLiteral("3.11.4");
        python.path = QStringLiteral("D:/Python311/python.exe");
        RuntimeState node;
        node.known = true; // 权威判定：没装
        QVERIFY(EnvironmentCache::save(python, node).ok);

        const EnvironmentCache::Snapshot loaded = EnvironmentCache::load();
        QVERIFY(loaded.isValid());
        QCOMPARE(loaded.python, python);
        QCOMPARE(loaded.node, node);
    }

    // 没有缓存文件是正常路径：当作「没有结论」，不告警也不落盘。
    void testCacheMissingFileIsInvalid()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());

        QVERIFY(!EnvironmentCache::load().isValid());
        QVERIFY(!QFile::exists(Paths::environmentCacheFile()));
    }

    // 格式版本不认识（升级前的旧缓存、手工改坏的文件）按「没有缓存」处理。
    void testCacheIgnoresForeignFormat()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());

        QJsonObject root;
        root[QStringLiteral("version")] = 99;
        root[QStringLiteral("python")] = QJsonObject();
        root[QStringLiteral("node")] = QJsonObject();
        QFile file(Paths::environmentCacheFile());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();

        QVERIFY(!EnvironmentCache::load().isValid());
    }

    // 构造只恢复缓存：属性立刻可用，且不起探测（不碰子进程、不依赖本机）。
    void testServiceRestoresCacheWithoutProbing()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());

        RuntimeState python;
        python.known = true;
        python.installed = true;
        python.version = QStringLiteral("3.11.4");
        python.path = QStringLiteral("D:/Python311/python.exe");
        RuntimeState node;
        node.known = true; // 权威判定：没装
        QVERIFY(EnvironmentCache::save(python, node).ok);

        EnvironmentService service;
        QCOMPARE(service.pythonStatus(), QStringLiteral("found"));
        QVERIFY(service.pythonInstalled());
        QCOMPARE(service.pythonVersion(), QStringLiteral("3.11.4"));
        QCOMPARE(service.pythonPath(), QStringLiteral("D:/Python311/python.exe"));
        QCOMPARE(service.nodeStatus(), QStringLiteral("missing"));
        QVERIFY(!service.nodeInstalled());
        QVERIFY(service.nodeVersion().isEmpty());
        QVERIFY(service.nodePath().isEmpty());
        // 构造不派发探测——start() 才派。
        QVERIFY(!service.detecting());
    }

    // 没有缓存时是「没有结论」（unknown），不是「没装」：界面因此不会在
    // 探测出结果之前先报红叉。
    void testServiceWithoutCacheHasNoVerdict()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());

        EnvironmentService service;
        QCOMPARE(service.pythonStatus(), QStringLiteral("unknown"));
        QCOMPARE(service.nodeStatus(), QStringLiteral("unknown"));
        QVERIFY(!service.pythonInstalled());
        QVERIFY(!service.nodeInstalled());
        QVERIFY(service.pythonPath().isEmpty());
        QVERIFY(service.nodePath().isEmpty());
        QVERIFY(!service.detecting());
    }
};

AWB_TEST(TestWorkbenchContext)
AWB_TEST(TestEnvironmentService)
#include "tst_workbench.moc"

/**
 * @brief workbench 测试套件入口（多类经 awbtest.h 注册依次执行）
 */
int main(int argc, char *argv[])
{
    return awbRunRegisteredTests(argc, argv);
}
