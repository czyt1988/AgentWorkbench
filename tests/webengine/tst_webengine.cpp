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

// Markdown 预览适配器只在 Qt 6 + WebEngine 的构建里存在（Qt 5 路线既没有
// awbmd:// 的供体，也没有随包渲染资源——见 src/markdown/webengine/
// CMakeLists.txt），所以本套件里与它相关的用例整块用同一个 #if 圈住。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "core/OpResult.h"
#include "markdown/MarkdownPreviewController.h"
#include "markdown/MarkdownRuntime.h"
#include "markdown/webengine/MarkdownPreviewProvider.h"
#include "markdown/webengine/MarkdownRuntimeHandler.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QQuickWebEngineProfile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWebEngineUrlScheme>
#endif

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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
namespace {

// Route 是 enum class，QCOMPARE 的失败打印需要 QMetaEnum 注册才好看；测试不
// 为打印去改生产类的元对象，直接把类别映成字符串再比对。
QString routeName(awb::markdown::MarkdownRuntimeHandler::Route route)
{
    using Route = awb::markdown::MarkdownRuntimeHandler::Route;
    switch (route) {
    case Route::Resource:
        return QStringLiteral("resource");
    case Route::Document:
        return QStringLiteral("document");
    case Route::Asset:
        return QStringLiteral("asset");
    case Route::Event:
        return QStringLiteral("event");
    case Route::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

} // namespace
#endif

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

    // --- Markdown 预览适配器（awbmd://）---------------------------------

    // URL 形状由 MarkdownRuntime 定、去处由 routeOf 定，这个用例把两侧对上：
    // 每一类 URL 都用生产代码的构造器生成，再用纯函数分类断言。分类值得
    // 单独测：它是 handler 里唯一不依赖引擎的分支，而分支错一类的表现是
    // 「预览整页 404」或「把不该供出的文件供出去」。
    void testMarkdownRouteClassification()
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QSKIP("the awbmd runtime only exists in Qt 6 + WebEngine builds");
#else
        using awb::markdown::MarkdownRuntime;
        using awb::markdown::MarkdownRuntimeHandler;
        QString rest;

        const QUrl vendor = MarkdownRuntime::vendorUrl(
            QStringLiteral("katex"), QStringLiteral("katex.min.js"));
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(vendor, &rest)),
                 QStringLiteral("resource"));
        QCOMPARE(rest, QStringLiteral("vendor/katex/katex.min.js"));

        const QUrl app = MarkdownRuntime::appUrl(QStringLiteral("shell.html"));
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(app, &rest)),
                 QStringLiteral("resource"));
        QCOMPARE(rest, QStringLiteral("app/shell.html"));

        const QUrl doc = MarkdownRuntime::documentUrl(7);
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(doc, &rest)),
                 QStringLiteral("document"));
        QCOMPARE(rest, QStringLiteral("doc.json"));

        const QUrl asset = MarkdownRuntime::assetUrl(
            QStringLiteral("d1"), QStringLiteral("images/a b.png"));
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(asset, &rest)),
                 QStringLiteral("asset"));
        // QUrl::path() 已经把 %20 解回空格：与磁盘上的真实名字对齐才读得到。
        QCOMPARE(rest, QStringLiteral("d1/images/a b.png"));

        const QUrl event = MarkdownRuntime::eventUrl(
            QStringLiteral("ready"), QStringLiteral("{}"));
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(event, &rest)),
                 QStringLiteral("event"));

        // 认不得的形状一律 unknown（handler 据此 fail，不猜、不「挑个最像的」
        // 资源供出去）。
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(
                     QUrl(QStringLiteral("awbmd://runtime/")))),
                 QStringLiteral("unknown"));
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(
                     QUrl(QStringLiteral("awbmd://runtime/vendors/x.js")))),
                 QStringLiteral("unknown"));
        // host 必须是 kHost：scheme 全局注册，别的 host 不该被这个 handler
        // 答话。
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(
                     QUrl(QStringLiteral("awbmd://other/runtime/doc.json")))),
                 QStringLiteral("unknown"));
        QCOMPARE(routeName(MarkdownRuntimeHandler::routeOf(
                     QUrl(QStringLiteral("http://runtime/doc.json")))),
                 QStringLiteral("unknown"));
#endif
    }

    // 信标的两头都要钉住：URL 里 k/p 的形状（parseBeacon）与事件名白名单
    // （isKnownEventKind）。白名单是页面唯一能触达 C++ 的入口，表外的名字
    // 必须「什么都不做」，而不是猜一个方法。
    void testMarkdownEventBeaconContract()
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QSKIP("the awbmd runtime only exists in Qt 6 + WebEngine builds");
#else
        using awb::markdown::MarkdownRuntime;
        using awb::markdown::MarkdownRuntimeHandler;

        const QString payload = QStringLiteral("{\"m\":\"probe \\\"q\\\"\"}");
        QString kind;
        QString decoded;
        MarkdownRuntimeHandler::parseBeacon(
            MarkdownRuntime::eventUrl(QStringLiteral("error"), payload),
            &kind, &decoded);
        QCOMPARE(kind, QStringLiteral("error"));
        QCOMPARE(decoded, payload);

        QString emptyKind;
        QString emptyPayload;
        MarkdownRuntimeHandler::parseBeacon(
            QUrl(QStringLiteral("awbmd://runtime/event")), &emptyKind,
            &emptyPayload);
        QVERIFY(emptyKind.isEmpty());

        const QStringList kinds = MarkdownRuntime::eventKinds();
        QVERIFY2(!kinds.isEmpty(), "the event table must not be empty");
        for (const QString &name : kinds) {
            QVERIFY2(MarkdownRuntimeHandler::isKnownEventKind(name),
                     qPrintable(QStringLiteral("unknown kind: ") + name));
        }
        QVERIFY(!MarkdownRuntimeHandler::isKnownEventKind(
            QStringLiteral("shutdown")));
        QVERIFY(!MarkdownRuntimeHandler::isKnownEventKind(QString()));

        // 信标的响应体必须是能当图片解码的 PNG（页面用 new Image() 发）。
        // QImage 没有「从字节构造」的入口，只能经 QImageReader 走一遍设备。
        QByteArray bytes = MarkdownRuntimeHandler::beaconPixel();
        QVERIFY2(bytes.startsWith("\x89PNG"),
                 qPrintable(QStringLiteral("beacon body is ")
                            + QString::number(bytes.size()) + " bytes"));
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        QImageReader reader(&buffer, "PNG");
        const QImage pixel = reader.read();
        QVERIFY2(!pixel.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(pixel.size(), QSize(1, 1));
#endif
    }

    // 文档相对路径的形状过滤：预览一张图片不该变成「读出用户主目录」。
    // 「逐段判 ..」而不是「整串 contains(..)」也在这里钉住：v1.2.png
    // 这种合法名字不能被挡。
    void testMarkdownAssetPathFilter()
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QSKIP("the awbmd runtime only exists in Qt 6 + WebEngine builds");
#else
        using awb::markdown::MarkdownRuntimeHandler;

        QVERIFY(MarkdownRuntimeHandler::isSafeRelativePath(
            QStringLiteral("images/a.png")));
        QVERIFY(MarkdownRuntimeHandler::isSafeRelativePath(
            QStringLiteral("v1.2.png")));
        QVERIFY(MarkdownRuntimeHandler::isSafeRelativePath(
            QStringLiteral("nested/dir/doc.md")));

        QVERIFY(!MarkdownRuntimeHandler::isSafeRelativePath(QString()));
        QVERIFY(!MarkdownRuntimeHandler::isSafeRelativePath(
            QStringLiteral("/etc/passwd")));
        QVERIFY(!MarkdownRuntimeHandler::isSafeRelativePath(
            QStringLiteral("../secret.md")));
        QVERIFY(!MarkdownRuntimeHandler::isSafeRelativePath(
            QStringLiteral("images/../../secret.md")));
        QVERIFY(!MarkdownRuntimeHandler::isSafeRelativePath(
            QStringLiteral("..\\..\\windows\\win.ini")));
#endif
    }

    // scheme 的权限组合就是「预览能不能工作」的直接原因：语法含端口段却没给
    // defaultPort 时 registerScheme() 直接 return（Qt 只警告一句 "needs a
    // default port"），fetch 在没注册的 scheme 上被引擎拒绝，FetchApiAllowed
    // 在 6.6 以前不存在，写了 NoAccessAllowed 时 origin 变 opaque、fetch 与
    // @font-face 一起废。schemeByName() 读的是引擎那份进程级注册表
    // （url::CustomScheme::FindScheme），所以注册结果能读回来钉住——比只读
    // 代码注释可靠，6.5 与 6.6+ 的差异就落在 registerScheme() 的一个 #if 里。
    void testMarkdownSchemeRegistration()
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QSKIP("the awbmd runtime only exists in Qt 6 + WebEngine builds");
#else
        const QByteArray schemeName(
            awb::markdown::MarkdownRuntime::kScheme);
        const QWebEngineUrlScheme scheme
            = QWebEngineUrlScheme::schemeByName(schemeName);
        QCOMPARE(scheme.name(), schemeName);
        QCOMPARE(int(scheme.syntax()), int(QWebEngineUrlScheme::Syntax::Host));

        const QWebEngineUrlScheme::Flags wanted
            = QWebEngineUrlScheme::Flag::SecureScheme
              | QWebEngineUrlScheme::Flag::LocalScheme
              | QWebEngineUrlScheme::Flag::LocalAccessAllowed
              | QWebEngineUrlScheme::Flag::CorsEnabled
              | QWebEngineUrlScheme::Flag::ServiceWorkersAllowed;
        QVERIFY2((scheme.flags() & wanted) == wanted,
                 qPrintable(QStringLiteral("registered flags: ")
                            + QString::number(int(scheme.flags()))));
        QVERIFY((scheme.flags() & QWebEngineUrlScheme::Flag::NoAccessAllowed)
                == 0);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        QVERIFY(scheme.flags().testFlag(
            QWebEngineUrlScheme::Flag::FetchApiAllowed));
#endif
#endif
    }

    // 能力位的来源：provider 没构造（WebEngine 关闭的构建、或者接线漏了）时
    // available 必须为假并给出原因；构造了且 scheme 确实在引擎的注册表里，
    // 才点亮。注册晚于 initialize() 时引擎拒绝写入注册表，这里就会落成假——
    // 而不是等到页面加载才发现。
    void testMarkdownSurfaceArming()
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QSKIP("the awbmd runtime only exists in Qt 6 + WebEngine builds");
#else
        using awb::markdown::MarkdownPreviewController;
        using awb::markdown::MarkdownPreviewProvider;

        awb::core::Settings settings;
        awb::theme::ThemeRegistry registry;
        awb::theme::Theme theme(&settings, &registry);
        MarkdownPreviewController controller(&settings, &theme);
        QVERIFY(!controller.available());
        QVERIFY(!controller.unavailableReason().isEmpty());

        MarkdownPreviewProvider provider(&controller);
        QVERIFY2(controller.available(),
                 qPrintable(controller.unavailableReason()));
        QVERIFY(controller.unavailableReason().isEmpty());
        QVERIFY(provider.previewProfile() != nullptr);
#endif
    }

    // 真实供体链（S1 的自动化版本）：一个真页面从 awbmd:// 加载——模块脚本
    // 要执行（.js 的 MIME 必须是 JS）、vendor 的 KaTeX 要跑、CSS 里相对路径
    // 的 woff2 要解码、同源 fetch 要拿到 doc.json、文档目录里的图片要读得到、
    // GET 信标要落到控制器的信号上。任何一环错——scheme 没注册、路径映射
    // 不对、MIME 表漏项、profile 没装 handler——都会在这里以标题里的失败清单
    // 暴露，而不是等发布后表现为「公式没有字形」。
    void testMarkdownSurfaceServesRuntime()
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QSKIP("the awbmd runtime only exists in Qt 6 + WebEngine builds");
#else
        using awb::markdown::MarkdownPreviewController;
        using awb::markdown::MarkdownPreviewProvider;
        using awb::markdown::MarkdownRuntime;

        awb::core::Settings settings;
        awb::theme::ThemeRegistry registry;
        awb::theme::Theme theme(&settings, &registry);
        MarkdownPreviewController controller(&settings, &theme);
        // 提供者构造时核对 scheme 是否已注册（注册在 main() 里、initialize()
        // 之前），并把控制器的能力位点亮。
        MarkdownPreviewProvider provider(&controller);
        QVERIFY2(controller.available(),
                 qPrintable(controller.unavailableReason()));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir documents(dir.path());
        const QString markdownPath = documents.filePath(QStringLiteral("note.md"));
        QFile markdown(markdownPath);
        QVERIFY(markdown.open(QIODevice::WriteOnly));
        markdown.write("# Probe\n\nSee ![dot](probe-image.png)\n");
        markdown.close();
        QImage dot(8, 8, QImage::Format_ARGB32);
        dot.fill(Qt::red);
        QVERIFY(dot.save(documents.filePath(QStringLiteral("probe-image.png")),
                         "PNG"));

        const awb::core::OpResult opened = controller.openFile(markdownPath);
        QVERIFY2(opened.ok, qPrintable(opened.error));
        // 文件会话的 docId 是 "f%1"（与草稿的 "d%1" 分命名空间——撞名
        // 会串会话，见 createFileSession 的注释）。
        QCOMPARE(controller.docId(), QStringLiteral("f1"));
        QVERIFY(controller.blocks().size() >= 2);

        QSignalSpy renderFailed(&controller,
                                &MarkdownPreviewController::renderFailed);

        QQuickWebEngineProfile *const profile = provider.previewProfile();
        QVERIFY(profile != nullptr);
        QCOMPARE(profile, provider.previewProfile());

        // 视图从内联 QML 建：WebEngineView 的 profile 只能在 QML 里赋值
        // （C++ 侧没有公开 setter），这里用上下文属性把预览 profile 递进去。
        // 上下文属性是测试工具的做法，app 侧禁用（约定见 AGENTS.md）。
        QQmlEngine engine;
        QScopedPointer<QQmlContext> context(
            new QQmlContext(engine.rootContext()));
        context->setContextProperty(QStringLiteral("awbPreviewProfile"), profile);
        QQmlComponent component(&engine);
        component.setData(QByteArrayLiteral(
            "import QtQuick\n"
            "import QtWebEngine\n"
            "WebEngineView {\n"
            "    width: 900\n"
            "    height: 700\n"
            "    profile: awbPreviewProfile\n"
            "}\n"), QUrl());
        QVERIFY2(component.status() != QQmlComponent::Error,
                 qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create(context.data()));
        QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
        QObject *const view = object.data();

        view->setProperty("url",
                          MarkdownRuntime::appUrl(
                              QStringLiteral("md-probe.html")).toString());

        QString title;
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 30000) {
            QTest::qWait(100);
            title = view->property("title").toString();
            if (title.startsWith(QStringLiteral("AWBMD-OK"))
                || title.contains(QStringLiteral("AWBMD-FAIL"))
                || title.contains(QStringLiteral("AWBMD-THROW"))) {
                break;
            }
        }
        QVERIFY2(title == QLatin1String("AWBMD-OK"),
                 qPrintable(QStringLiteral("probe title: ") + title));

        // 信标那一发（k=error）必须由 handler 派到控制器：页面回 200 只证明
        // 请求走了，信号才证明 C++ 侧接住了。
        QTRY_COMPARE_WITH_TIMEOUT(renderFailed.count(), 1, 5000);
#endif
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
    // 与 app/main.cpp 同序：awbmd:// 必须赶在 initialize() 之前注册，否则
    // handler 装不上、预览页 404（MarkdownPreviewProvider 的启动契约）。
    awb::markdown::MarkdownPreviewProvider::registerScheme();
    QtWebEngineQuick::initialize();
#else
    QtWebEngine::initialize();
#endif
    QGuiApplication app(argc, argv);
    TestWebEngineSurface tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_webengine.moc"
