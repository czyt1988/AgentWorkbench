#include <QtTest>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>

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
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);
        WebTabsFacade webTabs(&settings);
        // 与 tst_web 相同的手工注册：openTab 走内嵌路径而不是外部回退。
        webTabs.registerSurface(QStringLiteral("embedded"),
                                QStringLiteral("qrc:/fake/Surface.qml"));
        WebEngineProfileStore profileStore;
        WebEngineCompat compat;
        WorkbenchStub workbench;

        QQmlEngine engine;
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
        QQmlContext context(engine.rootContext());
        context.setContextProperty(QStringLiteral("theme"), &theme);
        context.setContextProperty(QStringLiteral("web"), &webTabs);
        context.setContextProperty(QStringLiteral("workbench"),
                                   &workbench);

        QQmlComponent component(&engine, QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/web/WebEngineSurface.qml")));
        QVERIFY2(component.status() != QQmlComponent::Error,
                 qPrintable(component.errorString()));

        QScopedPointer<QObject> surface(component.create(&context));
        QVERIFY2(!surface.isNull(),
                 qPrintable(component.errorString()));

        // 注入一个真实标签（about:blank：离线安全），与 WebTabsPage 的
        // Loader 完全同一入口（tabObject 就是 QML 拿标签的角色）。
        QVariantMap fields;
        fields[QStringLiteral("agentId")] = QStringLiteral("dsh");
        fields[QStringLiteral("url")] = QStringLiteral("about:blank");
        fields[QStringLiteral("title")] = QStringLiteral("dsh");
        const QString tabId = webTabs.openTab(fields);
        QVERIFY(!tabId.isEmpty());
        QObject *const tabObject = webTabs.tabObject(tabId);
        QVERIFY(tabObject != nullptr);
        QVERIFY(surface->setProperty("tab",
                                     QVariant::fromValue(tabObject)));

        // Component.onCompleted → WebEngineCompat.watchPopups(view) 走通：
        // 桥在视图上留有幂等标记（信号连接本身由 #if 分支里的成员指针
        // 连接在编译期保证——信号名错了根本编不过）。
        bool watched = false;
        const auto children = surface->findChildren<QObject *>();
        for (const QObject *child : children)
            watched = watched
                      || child->property("_awbPopupsWatched").toBool();
        QVERIFY2(watched,
                 "WebEngineCompat.watchPopups did not attach to the view — "
                 "the Component.onCompleted wiring is broken");

        // 让 about:blank 的加载跑完（loadingChanged → 门面的状态机），
        // 置信度高于「只创建不加载」。
        QTest::qWait(500);
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
