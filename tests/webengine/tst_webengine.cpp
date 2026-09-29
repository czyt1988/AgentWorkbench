#include <QtTest>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QUrl>

#include "core/Settings.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
#include "web/WebTabsFacade.h"
#include "web/webengine/WebEngineCompat.h"
#include "web/webengine/WebEngineProfileStore.h"

// tst_webengine — 内嵌表面加载冒烟。
// 表面 QML 与它引用的唯一组件（AButton）以与 app 相同的模块 URL/别名嵌进
// 本二进制，再按 MainWindow 根别名的契约供上 theme/web/workbench，像
// per-tab Loader 一样创建组件：任何「不存在的属性/信号」（版本改名类
// 缺陷）都会让组件加载失败。WebEngineView 的创建要求 initialize() 先于
// QGuiApplication（与 app/main.cpp 同序），所以本套件不经共享注册表跑，
// 自带 main。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtWebEngineQuick>
#else
#include <QtWebEngine>
#endif

using awb::core::Settings;
using awb::theme::Theme;
using awb::theme::ThemeRegistry;
using awb::web::WebEngineCompat;
using awb::web::WebEngineProfileStore;
using awb::web::WebTabsFacade;

// workbench 桩：表面 QML 只调用这三个方法，且都不在加载路径上（点击 /
// 权限请求才触发）；真门面的 Q_INVOKABLE 守卫由 tst_shell 与
// check_architecture 负责，这里只需要名字可解析。
class WorkbenchStub : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE void openExternalUrl(const QString &) {}
    Q_INVOKABLE void notify(const QString &, const QString &,
                             const QString &) {}
    Q_INVOKABLE void launchAgent(const QString &) {}
};

// 表面测试脚手架：复刻 main.cpp 的单例注册与 MainWindow 根别名上下文，
// 按 per-tab Loader 的方式创建表面组件并绑定真实标签。每个用例一个独立
// 实例（单例注册随实例重建——旧实例已随上一个用例析构，不存在悬垂）。
// 成员声明顺序即依赖顺序，析构反序：surface/引擎先于被引用的单例。
class SurfaceHarness
{
public:
    SurfaceHarness()
        : theme(&settings, &registry),
          webTabs(&settings)
    {
        // 与 tst_web 相同的手工注册：openTab 走内嵌路径而不是外部回退。
        webTabs.registerSurface(QStringLiteral("embedded"),
                                QStringLiteral("qrc:/fake/Surface.qml"));

        // main.cpp 在 Qt 5 上补的模块导入路径（生成的 qmldir 在
        // qrc:/qt/qml 下）。
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
        // main.cpp 同款单例注册：表面 QML 经 import AgentWorkbench.App
        // 引用的全部名字。
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                     "Theme", &theme);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                     "Web", &webTabs);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                     "WebProfiles", &profileStore);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                     "WebEngineCompat", &compat);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                     "Workbench", &workbench);

        // 小写别名（theme/web/workbench）在 app 里是 MainWindow 根部
        // 属性，经作用域链对 Loader 加载的组件可见；单独创建组件时用
        // 上下文属性提供同名解析（对象就是上面这些单例）。这是测试工具
        // 的做法，不是 app 的设计（app 禁用 setContextProperty）。
        context.reset(new QQmlContext(engine.rootContext()));
        context->setContextProperty(QStringLiteral("theme"), &theme);
        context->setContextProperty(QStringLiteral("web"), &webTabs);
        context->setContextProperty(QStringLiteral("workbench"),
                                    &workbench);

        component.reset(new QQmlComponent(&engine, QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/web/WebEngineSurface.qml"))));
    }

    // 创建表面并绑定一个指向 url 的真实标签——与 WebTabsPage 的 Loader
    // 完全同一入口（tabObject 就是 QML 拿标签的角色）。任一步失败返回
    // 空指针，由调用方的 QVERIFY 报告。
    QObject *createSurfacedTab(const QString &agentId, const QUrl &url)
    {
        surface.reset(component->create(context.data()));
        if (surface.isNull()) {
            return nullptr;
        }
        QVariantMap fields;
        fields[QStringLiteral("agentId")] = agentId;
        fields[QStringLiteral("url")] = url.toString();
        fields[QStringLiteral("title")] = agentId;
        const QString tabId = webTabs.openTab(fields);
        if (tabId.isEmpty()) {
            return nullptr;
        }
        QObject *const tabObject = webTabs.tabObject(tabId);
        if (!tabObject
            || !surface->setProperty("tab", QVariant::fromValue(tabObject))) {
            return nullptr;
        }
        return tabObject;
    }

    Settings settings;
    ThemeRegistry registry;
    Theme theme;
    WebTabsFacade webTabs;
    WebEngineProfileStore profileStore;
    WebEngineCompat compat;
    WorkbenchStub workbench;
    QQmlEngine engine;
    QScopedPointer<QQmlContext> context;
    QScopedPointer<QQmlComponent> component;
    QScopedPointer<QObject> surface;
};

class TestWebEngineSurface : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // 回归：表面 QML 里曾声明的 onNewWindowRequested 是 Qt 6 的信号名，
    // Qt 5.15 的引擎以「Cannot assign to non-existent property」拒绝整个
    // 组件——内嵌页空白（0.4.0 的实际线上症状）；页面加载冒烟从不点弹窗，
    // C++ 单测也从未加载过这个文件，缺陷因此静默存在。这里加载真实文件、
    // 创建真实 WebEngineView 并注入真实标签：这一类错误会让组件状态变
    // Error / create() 返回空。
    void testSurfaceLoadsCleanly()
    {
        SurfaceHarness harness;
        QVERIFY2(harness.component->status() != QQmlComponent::Error,
                 qPrintable(harness.component->errorString()));

        // 注入一个真实标签（about:blank：离线安全）。
        QObject *const tabObject = harness.createSurfacedTab(
            QStringLiteral("dsh"), QUrl(QStringLiteral("about:blank")));
        QVERIFY2(!harness.surface.isNull(),
                 qPrintable(harness.component->errorString()));
        QVERIFY(tabObject != nullptr);

        // Component.onCompleted → WebEngineCompat.watchPopups(view) 走通：
        // 桥在视图上留有幂等标记（信号连接本身由 #if 分支里的成员指针
        // 连接在编译期保证——信号名错了根本编不过）。
        bool watched = false;
        const auto children = harness.surface->findChildren<QObject *>();
        for (const QObject *child : children) {
            watched = watched
                      || child->property("_awbPopupsWatched").toBool();
        }
        QVERIFY2(watched,
                 "WebEngineCompat.watchPopups did not attach to the view — "
                 "the Component.onCompleted wiring is broken");

        // 让 about:blank 的加载跑完（loadingChanged → 门面的状态机），
        // 置信度高于「只创建不加载」。
        QTest::qWait(500);
    }

    // 回归：Qt 5.15 内嵌引擎是 Chromium 87（本机 Src 树 chrome/VERSION
    // 可查证），现代 agent WebUI 启动即调用 .at（Chrome 92）/ toSorted
    // （110）/ Promise.withResolvers（119）等新 API，TypeError 白屏——
    // 2026-09 线上日志（qwen/kimi/dsh 三页全挂）的实际症状。
    // compat-polyfills.js 必须在页面任何脚本之前生效（DocumentCreation +
    // MainWorld）：探测页逐项检查 API 存在性并抽查行为，结果经
    // document.title → 表面 onTitleChanged → 门面回流，整条注入链
    // （注册、时机、world、polyfill 行为）在真实引擎上一次验证。
    // 在 Chromium 118+（Qt 6）上全部特性原生存在，本用例同时守护
    // 「polyfill 不搅扰新引擎」（特性检测下空转）。
    void testCompatPolyfillsAreInjected()
    {
        SurfaceHarness harness;
        QVERIFY2(harness.component->status() != QQmlComponent::Error,
                 qPrintable(harness.component->errorString()));

        const QString html = QStringLiteral(
            "<html><body><script>"
            "var missing = [];"
            "function need(ok, name) { if (!ok) missing.push(name); }"
            "need(typeof Promise.withResolvers === 'function', 'withResolvers');"
            "need(typeof [].at === 'function', 'Array.at');"
            "need(typeof ''.at === 'function', 'String.at');"
            "need(typeof [].toSorted === 'function', 'toSorted');"
            "need(typeof [].findLast === 'function', 'findLast');"
            "need(typeof Object.hasOwn === 'function', 'hasOwn');"
            "need(typeof Object.groupBy === 'function', 'groupBy');"
            "need(typeof structuredClone === 'function', 'structuredClone');"
            "need(typeof AbortSignal.timeout === 'function', 'signalTimeout');"
            "need(typeof crypto.randomUUID === 'function', 'randomUUID');"
            "need(typeof URL.canParse === 'function', 'canParse');"
            "try { var r = Promise.withResolvers(); r.resolve(1); }"
            " catch (e) { missing.push('withResolvers-behavior'); }"
            "try { var a = [3, 1, 2];"
            "  if (a.toSorted().join() !== '1,2,3' || a.join() !== '3,1,2')"
            "    missing.push('toSorted-behavior');"
            "  if (a.at(-1) !== 2) missing.push('at-behavior');"
            "} catch (e) { missing.push('array-behavior'); }"
            "try { var c = structuredClone({ d: new Date(0), a: [1] });"
            "  if (!(c.d instanceof Date) || c.d.getTime() !== 0"
            "      || c.a[0] !== 1)"
            "    missing.push('structuredClone-behavior');"
            "} catch (e) { missing.push('structuredClone-behavior'); }"
            "document.title = missing.length === 0 ? 'POLYFILL-OK'"
            "  : 'POLYFILL-MISSING:' + missing.join(',');"
            "</script></body></html>");
        // data: URL 由 view.url 直接加载属浏览器发起的导航，不受
        // Chromium「渲染器不得导航顶层到 data:」的限制。
        const QUrl probeUrl(QStringLiteral("data:text/html,")
                            + QString::fromLatin1(
                                  QUrl::toPercentEncoding(html)));

        QObject *const tabObject =
            harness.createSurfacedTab(QStringLiteral("probe"), probeUrl);
        QVERIFY2(!harness.surface.isNull(),
                 qPrintable(harness.component->errorString()));
        QVERIFY(tabObject != nullptr);

        // 标题回流是异步的（渲染进程 → titleChanged → 门面）；失败时
        // QTRY 把实际标题（含缺失项清单）带进报告。
        QTRY_COMPARE_WITH_TIMEOUT(tabObject->property("title").toString(),
                                  QStringLiteral("POLYFILL-OK"), 30000);
    }

    // 回归：polyfill 救不了语法级缺口——dsh 的主 bundle 含 class 静态块
    // （Chrome 94+），Chromium 87 解析即失败，HTTP 层却是 200：状态机
    // 停在 ready，用户面对零提示的空白页（2026-09 线上症状）。表面的
    // 白屏兜底必须把这种标签落到 error 态（覆盖层带「在浏览器中打开」），
    // 且错误信息点明内嵌引擎过旧。这里用 data: URL 复现同一失败形状：
    // 空根容器 + 一个未捕获异常；探测、计时、状态迁移全走真实链路。
    // 断言不含具体版本号，Qt 6（Chromium 118+）上同样成立。
    void testBlankPageWithScriptErrorsFallsToErrorState()
    {
        SurfaceHarness harness;
        QVERIFY2(harness.component->status() != QQmlComponent::Error,
                 qPrintable(harness.component->errorString()));

        const QString html = QStringLiteral(
            "<html><body><div id=\"root\"></div>"
            "<script>throw new Error('simulated incompatible bundle');"
            "</script></body></html>");
        const QUrl brokenUrl(QStringLiteral("data:text/html,")
                             + QString::fromLatin1(
                                   QUrl::toPercentEncoding(html)));

        QObject *const tabObject =
            harness.createSurfacedTab(QStringLiteral("broken"), brokenUrl);
        QVERIFY2(!harness.surface.isNull(),
                 qPrintable(harness.component->errorString()));
        QVERIFY(tabObject != nullptr);

        // 加载成功 → ready → 白屏检查计时（3 s）→ error。手动轮询以便
        // 失败时把 uncaughtErrors / lastError 一并带进报告（定位是
        // 「异常没被计数」还是「探测判了非空白」）。
        QString state;
        QElapsedTimer clock;
        clock.start();
        do {
            QTest::qWait(250);
            state = tabObject->property("state").toString();
        } while (state != QStringLiteral("error") && clock.elapsed() < 30000);
        QVERIFY2(state == QStringLiteral("error"),
                 qPrintable(QStringLiteral("state=%1 uncaughtErrors=%2 "
                                           "lastError=%3")
                     .arg(state)
                     .arg(harness.surface->property("uncaughtErrors").toInt())
                     .arg(tabObject->property("lastError").toString())));
        const QString lastError =
            tabObject->property("lastError").toString();
        QVERIFY2(lastError.contains(QStringLiteral("Chromium")),
                 qPrintable(QStringLiteral(
                     "lastError should name the engine, got: ") + lastError));
    }

    // 手动 E2E（默认 QSKIP，ctest 保持密闭）：把 AWB_E2E_URL 指向一个
    // 正在运行的 agent WebUI（如 qwen serve 的 http://127.0.0.1:4170），
    // 走真实表面 + 真实注入链路验证整页启动。判据是客观的：
    // - 标签落到 ready 并稳定跨过白屏检查窗口（不转 error）；
    // - 表面的 uncaughtErrors 保持 0——页面启动路径上的任何 TypeError
    //   （.at/toSorted 缺口的原始症状）都会被控制台接管计入。
    // 用法：AWB_E2E_URL=<url> ./build/tests/webengine/Debug/tst_webengine.exe
    //       testRealAgentPageManual
    void testRealAgentPageManual()
    {
        const QByteArray urlEnv = qgetenv("AWB_E2E_URL");
        if (urlEnv.isEmpty()) {
            QSKIP("manual E2E: set AWB_E2E_URL to a running agent WebUI");
        }
        SurfaceHarness harness;
        QVERIFY2(harness.component->status() != QQmlComponent::Error,
                 qPrintable(harness.component->errorString()));

        QObject *const tabObject = harness.createSurfacedTab(
            QStringLiteral("e2e"),
            QUrl::fromUserInput(QString::fromLocal8Bit(urlEnv)));
        QVERIFY2(!harness.surface.isNull(),
                 qPrintable(harness.component->errorString()));
        QVERIFY(tabObject != nullptr);

        QTRY_COMPARE_WITH_TIMEOUT(tabObject->property("state").toString(),
                                  QStringLiteral("ready"), 30000);
        // 白屏检查在 LoadSucceeded 后 3 s 触发；等过窗口再断言仍 ready。
        QTest::qWait(6000);
        QCOMPARE(tabObject->property("state").toString(),
                 QStringLiteral("ready"));
        QCOMPARE(harness.surface->property("uncaughtErrors").toInt(), 0);
        qDebug() << "E2E page title:"
                 << tabObject->property("title").toString();
    }
};

// 与 app/main.cpp 同序：测试模式最先（WebEngine 的持久存储不落真实
// 数据目录），然后 WebEngine 初始化，最后 QGuiApplication。
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QtWebEngineQuick::initialize();
#else
    QtWebEngine::initialize();
#endif
    QGuiApplication app(argc, argv);
    TestWebEngineSurface tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_webengine.moc"
