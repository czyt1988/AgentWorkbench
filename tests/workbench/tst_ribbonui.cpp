#include <QtTest>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <SARibbonQml/SARibbonQmlGlobal.h>
#include <SARibbonQml/SARibbonQmlBar.h>
#include <SARibbonQml/SARibbonQmlCategory.h>

#include "core/Paths.h"
#include "core/Settings.h"
#include "markdown/MarkdownPreviewController.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
#include "tools/MarkdownEdit.h"
#include "tools/ToolsFacade.h"

// tst_ribbonui — SARibbon 命令条的运行期冒烟（仅 AWB_ENABLE_RIBBON 构建）。
//
// 其余 QML 测试（tst_toolsui / tst_markdownpreviewui）刻意不链接 SARibbon：
// 它们装载的命令条是降级工具栏。于是「Ribbon 形态真的渲染得出来」这条
// 路径只剩本套件把守，而它压的正是静态链接 SARibbon 的三个已知风险面：
//   1. qrc 初始化——静态库的 qrc 不会自动装载，saRibbonRegisterQmlTypes
//      里的 Q_INIT_RESOURCE(saribbon_qml) 没生效时，叶子创建会打
//      「leaf resource missing」警告、bar 停在空壳（这里以消息钩子拦下）；
//   2. 命令式注册——saRibbonRegisterQmlTypes 必须在 engine.load 之前，
//      晚了 import SARibbon 解析不到，整份文件加载失败；
//   3. 叶子创建——RibbonBar 的叶子在 Component.onCompleted 里按资源 URL
//      实例化，任何一个环节断了 bar 都没有隐式高度。
//
// 动作逻辑不在这里测：两份命令条都只把点击转发给页面动作层（页面的
// 动作由 tst_toolsui 经真实点击覆盖），这里只回答「装得起来、排得出来」。
//
// 第二个用例把 win 桩接进来（preset = "prompt"），让上下文页真正激活，
// 然后读 RibbonBar 的 contextBands：色带颜色必须与主题 accent 可区分
//（2026-10 的「上下文页与普通页混叠」缺陷正是 contextColor ==
// theme.accent 造成的——SARibbon 的标题行底色就是 RibbonTheme.accent，
// 同色的色带与 tab 文字全部隐形），且色带只覆盖自己的 tab 区间（x >= 0、
// 宽度有限），而不是横贯整条标题行。
//
// 第三个用例装载整份 MarkdownWorkbenchPage.qml（页面里的 Loader 自己去
// 装 WorkbenchRibbon.qml）。「页面 + SARibbon」的组合只有这里覆盖，压的
// 是 2026-10 的三个首显缺陷：
//   a. 首次显示时 category 行不出现（只有 tab 行）——以 bar 的隐式高度
//      包含 categoryRowY 且有可见 category 落位该行断言；
//   b. delegate 信号处理器里引用文档根 id（workbenchWindow）抛
//      ReferenceError——页面已改为经 scope 属性传递，这里用真实点击驱动
//      fileTab 的 TapHandler；
//   c. ObjectModel 的用法回退（Component 声明为模型子项、createObject 的
//      父给模型对象）刷「Delegate must be of Item type」与「not placed in
//      the graphics scene」——消息钩子一律拦下算失败。

using awb::core::Settings;
using awb::markdown::MarkdownPreviewController;
using awb::theme::Theme;
using awb::theme::ThemeRegistry;
using awb::tools::MarkdownEdit;
using awb::tools::ToolsFacade;

namespace {

// 命令条与页面侧的 QML 告警一律算失败：SARibbon 的叶子资源缺失、引擎
// 不可达，以及页面的 ReferenceError（import 缺失、delegate 外部 id）、
// ObjectModel 用法回退，都只以 qWarning 的形式存在，对象图看起来一切
// 正常。
QStringList g_ribbonWarnings;
QtMessageHandler g_previousHandler = nullptr;

void ribbonMessageHandler(QtMsgType type, const QMessageLogContext &context,
                          const QString &message)
{
    if ((type == QtWarningMsg || type == QtCriticalMsg)
        && (message.contains(QLatin1String("SARibbonQml"))
            || message.contains(QLatin1String("leaf resource missing"))
            || message.contains(QLatin1String("ReferenceError"))
            || message.contains(QLatin1String("Delegate must be of Item type"))
            || message.contains(
                   QLatin1String("not placed in the graphics scene")))) {
        g_ribbonWarnings.append(message);
    }
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

// win 桩：命令条只读宿主的 preset / activeEditor / ribbonCollapsed 这几个
// 名字（动作转发在 win 为空时全部短路，桩不必提供它们）。
class WinStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString preset READ preset CONSTANT)
    Q_PROPERTY(bool ribbonCollapsed READ ribbonCollapsed CONSTANT)

public:
    QString preset() const { return QStringLiteral("prompt"); }
    bool ribbonCollapsed() const { return false; }
};

// workbench/ui/nav 桩（第三个用例装整页时用，桩面与 tst_toolsui 同款）：
// 页面经上下文名消费它们，行为不在断言范围内。
class WorkbenchStub : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE void notify(const QString &, const QString &, const QString &) {}
    Q_INVOKABLE void copyText(const QString &) {}
    Q_INVOKABLE bool canPreviewMarkdown(const QString &) { return false; }
    Q_INVOKABLE void previewMarkdown(const QString &) {}
    // 会话恢复/落盘：页面 onCompleted 会调；桩给「无可恢复」的空现场，
    // saveMarkdownSession 只吞掉（本套件不测持久化）。
    Q_INVOKABLE QVariantMap restoreMarkdownSession()
    {
        QVariantMap result;
        result.insert(QStringLiteral("restored"), false);
        return result;
    }
    Q_INVOKABLE void saveMarkdownSession(const QVariantMap &) {}
    Q_INVOKABLE void openLocalFileExternal(const QString &) {}
};

class NavStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPageId READ currentPageId CONSTANT)
public:
    QString currentPageId() const { return QStringLiteral("tools"); }
};

class UiStub : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE QString pickFolder(const QString &) { return QString(); }
};

// 视觉树递归查找（objectName 匹配）：Repeater 的 delegate 只设视觉父
//（QObject 父为 null），findChild 的 QObject 树搜不到——页面里按
// objectName 定位的 delegate 一律走这里。
QQuickItem *findItemByName(QQuickItem *root, const QString &name)
{
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == name) {
        return root;
    }
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *hit = findItemByName(child, name)) {
            return hit;
        }
    }
    return nullptr;
}

} // namespace

class TestRibbonUi : public QObject
{
    Q_OBJECT

private:
    /**
     * @brief 把随包的两份主题 JSON 写进测试模式的主题目录
     * @return 两份都写成功为真
     *
     * 主题 qrc 只嵌在 app 目标里；测试二进制里的 ThemeRegistry 扫不到
     * 任何主题时全部令牌都是无效色。从源码树拷贝（QT_TESTCASE_SOURCEDIR
     * 由构建系统提供），registry 构造前的 scan() 就能命中。
     */
    bool seedThemes()
    {
        QDir themesDir(awb::core::Paths::themesDir());
        if (!themesDir.mkpath(QStringLiteral("."))) {
            return false;
        }
        const QString sourceRoot = QStringLiteral(QT_TESTCASE_SOURCEDIR)
                                   + QStringLiteral("/../../resources/themes");
        const QStringList ids = {QStringLiteral("mocha-dark"),
                                 QStringLiteral("latte-light")};
        for (const QString &id : ids) {
            QFile src(sourceRoot + QLatin1Char('/') + id
                      + QStringLiteral(".json"));
            if (!src.open(QIODevice::ReadOnly)) {
                return false;
            }
            const QByteArray body = src.readAll();
            src.close();
            QFile dst(themesDir.filePath(id + QStringLiteral(".json")));
            if (!dst.open(QIODevice::WriteOnly)) {
                return false;
            }
            dst.write(body);
        }
        return true;
    }

private Q_SLOTS:
    void init() { QStandardPaths::setTestModeEnabled(true); }

    // 命令条装载：注册 → 加载 → bar 有布局产物，全程无 SARibbon 告警。
    void testRibbonLoadsAndLaysOut()
    {
        QTemporaryDir dataRoot;
        QVERIFY2(dataRoot.isValid(), "a temporary dir failed");
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);
        theme.applyTheme(QStringLiteral("mocha-dark"));
        ToolsFacade tools(dataRoot.path());
        MarkdownEdit markdownEdit(&theme);
        MarkdownPreviewController markdown(&settings, &theme);

        QQmlEngine engine;
        // 注册必须先于任何加载——这正是产品 main.cpp 的那一行。
        saRibbonRegisterQmlTypes(&engine);
        // main.cpp 同款单例注册：命令条经 import AgentWorkbench.App 引用
        // MarkdownEdit（格式按钮的光标态查询），缺了它整份文件装不起来。
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme",
                                     &theme);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Tools",
                                     &tools);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                     "MarkdownEdit", &markdownEdit);

        // 小写别名经上下文属性提供（MainWindow 根部属性在测试里的等价物）。
        QQmlContext context(engine.rootContext());
        context.setContextProperty(QStringLiteral("theme"), &theme);
        context.setContextProperty(QStringLiteral("tools"), &tools);
        context.setContextProperty(QStringLiteral("markdown"), &markdown);

        // 宿主窗先于命令条声明（销毁次序相反）：bar 的叶子排布依赖宽度，
        // 孤立 Item 的 0 宽让隐式高度永远算不出来。
        QQuickWindow host;
        host.resize(900, 200);

        g_ribbonWarnings.clear();
        g_previousHandler = qInstallMessageHandler(ribbonMessageHandler);
        QQmlComponent component(&engine, QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/workbench/WorkbenchRibbon.qml")));
        QVERIFY2(!component.isError(),
                 qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create(&context));
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
        QVERIFY2(!root.isNull(), qPrintable(component.errorString()));

        // 布局产物：bar 的隐式高度要真的算出来（叶子创建失败时它是 0）。
        QQuickItem *const item = qobject_cast<QQuickItem *>(root.data());
        QVERIFY2(item, "the ribbon root is not a QQuickItem");
        item->setParentItem(host.contentItem());
        item->setWidth(900);
        host.show();
        QVERIFY2(QTest::qWaitForWindowExposed(&host),
                 "the host window never became exposed");
        QTRY_VERIFY2_WITH_TIMEOUT(item->implicitHeight() > 0,
                                  "the ribbon bar never produced a height",
                                  5000);

        // 装载进去的 RibbonBar：无宿主窗口（win 为空）时应用按钮的文案
        // 落在 Draft——这条绑定同时证明 applicationLabel 属性与窗口状态
        // 的桥接在读得到的那一侧。
        const auto children = item->childItems();
        QVERIFY2(!children.isEmpty(), "the ribbon root has no bar child");
        QObject *bar = children.constFirst();
        QCOMPARE(bar->property("applicationLabel").toString(),
                 QStringLiteral("Draft"));

        // 全程不允许出现 SARibbon 侧的告警（qrc 未初始化、引擎不可达）。
        QVERIFY2(g_ribbonWarnings.isEmpty(),
                 qPrintable(QStringLiteral("SARibbon warnings during load: ")
                            + g_ribbonWarnings.join(QLatin1Char(';'))));
    }

    // 上下文页激活时：色带在场、颜色与主题 accent 可区分、几何只覆盖
    // 自己的 tab 区间（不横贯标题行）。
    void testContextCategoryBandIsDistinctAndBounded()
    {
        QTemporaryDir dataRoot;
        QVERIFY2(dataRoot.isValid(), "a temporary dir failed");
        // 主题 JSON 只嵌在 app 目标的 qrc 里；本目标要拿到真实的语义令牌
        // 就把随包的两份主题写进测试模式的主题目录（构造 registry 前写好，
        // scan() 才能扫到）。否则全部令牌都是无效色，色带断言无从谈起。
        QVERIFY2(seedThemes(), "seeding the builtin themes failed");
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);
        theme.applyTheme(QStringLiteral("mocha-dark"));
        ToolsFacade tools(dataRoot.path());
        MarkdownEdit markdownEdit(&theme);
        MarkdownPreviewController markdown(&settings, &theme);
        WinStub win;

        QQmlEngine engine;
        saRibbonRegisterQmlTypes(&engine);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme",
                                     &theme);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Tools",
                                     &tools);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                     "MarkdownEdit", &markdownEdit);

        QQmlContext context(engine.rootContext());
        context.setContextProperty(QStringLiteral("theme"), &theme);
        context.setContextProperty(QStringLiteral("tools"), &tools);
        context.setContextProperty(QStringLiteral("markdown"), &markdown);

        QQuickWindow host;
        host.resize(900, 200);

        g_ribbonWarnings.clear();
        g_previousHandler = qInstallMessageHandler(ribbonMessageHandler);
        QQmlComponent component(&engine, QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/workbench/WorkbenchRibbon.qml")));
        QVERIFY2(!component.isError(),
                 qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create(&context));
        QVERIFY2(!root.isNull(), qPrintable(component.errorString()));
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;

        QQuickItem *const item = qobject_cast<QQuickItem *>(root.data());
        QVERIFY2(item, "the ribbon root is not a QQuickItem");
        // 宿主要走根对象的 win 属性（页面在 onStatusChanged 里就是这么
        // 接的）：组件根自己声明了 property var win，上下文属性里的同名
        // 名字会被它遮蔽，绑不到桩上。
        QVERIFY2(item->setProperty("win", QVariant::fromValue(&win)),
                 "the ribbon root has no win property");
        item->setParentItem(host.contentItem());
        item->setWidth(900);
        host.show();
        QVERIFY2(QTest::qWaitForWindowExposed(&host),
                 "the host window never became exposed");
        QTRY_VERIFY2_WITH_TIMEOUT(item->implicitHeight() > 0,
                                  "the ribbon bar never produced a height",
                                  5000);

        QObject *bar = item->childItems().constFirst();
        QVERIFY2(bar, "the ribbon root has no bar child");

        // 色带数据由宿主在布局后发布；preset = "prompt" 上下文页激活。
        QVariantList bands;
        QTRY_VERIFY2_WITH_TIMEOUT(
            !(bands = bar->property("contextBands").toList()).isEmpty(),
            "the active context category produced no band", 5000);
        QCOMPARE(bands.size(), 1);
        const QVariantMap band = bands.at(0).toMap();
        QCOMPARE(band.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Prompt tools"));

        // 颜色可区分：上下文色不得与主题 accent 相同（隐形混叠的根因）。
        const QColor contextColor =
            band.value(QStringLiteral("color")).value<QColor>();
        QVERIFY2(contextColor.isValid(),
                 "the band color is not a valid QColor");
        const QColor accentColor = theme.accent();
        QVERIFY2(contextColor != accentColor,
                 qPrintable(QStringLiteral("contextColor equals theme.accent (")
                            + accentColor.name()
                            + QStringLiteral(") - the band and its tab text ")
                            + QStringLiteral("are invisible on the accent ")
                            + QStringLiteral("title row")));

        // 几何有界：色带只覆盖上下文 tab 的区间（x 起点在标题行内容区、
        // 宽度远小于整条 bar），不是横贯全宽的背景。
        const qreal x = band.value(QStringLiteral("x")).toReal();
        const qreal width = band.value(QStringLiteral("width")).toReal();
        QVERIFY2(x >= 0, "the band starts left of the bar");
        QVERIFY2(width > 0, "the band has no width");
        QVERIFY2(x + width <= item->width() + 1.0,
                 "the band spills past the right edge of the bar");
        QVERIFY2(width < item->width() * 0.9,
                 qPrintable(QStringLiteral("the band spans the whole title ")
                            + QStringLiteral("row (width=%1 of %2)")
                                  .arg(width).arg(item->width())));

        QVERIFY2(g_ribbonWarnings.isEmpty(),
                 qPrintable(QStringLiteral("SARibbon warnings during load: ")
                            + g_ribbonWarnings.join(QLatin1Char(';'))));
    }

    // 整页首显（2026-10 三个缺陷的复现现场）：页面 + 真 Ribbon 路径。
    // 页面里的 Loader 装载 WorkbenchRibbon.qml（不是测试直接建命令条），
    // 首屏就要看到 category 行——tab 行在场、面板区缺失正是用户报的
    // 「第一次进去看不到 Ribbon 的 category，点一下 tab 才出现」。
    void testPageShowsRibbonCategoryOnFirstShow()
    {
        QTemporaryDir dataRoot;
        QVERIFY2(dataRoot.isValid(), "a temporary dir failed");
        QVERIFY2(seedThemes(), "seeding the builtin themes failed");
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);
        theme.applyTheme(QStringLiteral("mocha-dark"));
        ToolsFacade tools(dataRoot.path());
        MarkdownEdit markdownEdit(&theme);
        // 纯层控制器：available 恒为假——页面停在提示词模式（previewPane
        // 的 Loader 因此不装载，WebEngine 不进这个测试）。
        MarkdownPreviewController markdown(&settings, &theme);
        WorkbenchStub workbench;
        NavStub nav;
        UiStub ui;

        QQmlEngine engine;
        saRibbonRegisterQmlTypes(&engine);
        // main.cpp 同款单例注册（页面与命令条都 import AgentWorkbench.App）。
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme",
                                     &theme);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Tools",
                                     &tools);
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                     "MarkdownEdit", &markdownEdit);

        // 小写别名经上下文属性提供（MainWindow 根部属性在测试里的等价物，
        // tst_toolsui 同款做法）。
        QQmlContext context(engine.rootContext());
        context.setContextProperty(QStringLiteral("theme"), &theme);
        context.setContextProperty(QStringLiteral("tools"), &tools);
        context.setContextProperty(QStringLiteral("markdown"), &markdown);
        context.setContextProperty(QStringLiteral("workbench"), &workbench);
        context.setContextProperty(QStringLiteral("nav"), &nav);
        context.setContextProperty(QStringLiteral("ui"), &ui);

        g_ribbonWarnings.clear();
        g_previousHandler = qInstallMessageHandler(ribbonMessageHandler);
        QQmlComponent component(&engine, QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/workbench/MarkdownWorkbenchPage.qml")));
        QVERIFY2(!component.isError(),
                 qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> pageOwner(
            qobject_cast<QQuickItem *>(component.create(&context)));
        QVERIFY2(pageOwner != nullptr,
                 qPrintable(QStringLiteral("the page root is not a ")
                            + QStringLiteral("QQuickItem: ")
                            + component.errorString()));
        // 裸指针留着后面用（setContent 收养后 pageOwner 已空）。
        QQuickItem *const page = pageOwner.get();

        QQuickView view(&engine, nullptr);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        // setContent 接管 page 的所有权（tst_toolsui 同款）。
        view.setContent(QUrl(), nullptr, pageOwner.release());
        view.resize(1200, 800);
        view.show();
        QVERIFY2(QTest::qWaitForWindowExposed(&view),
                 "the page window never became exposed");

        // 命令条必须走真 Ribbon：页面里的 Loader 成功装载了
        // WorkbenchRibbon.qml（装载失败会换降级工具栏——那就没有
        // RibbonBar 可找）。QObject 树上直接找（Loader 的 item 也是
        // QObject 子链）。
        SARibbonQml::RibbonBar *bar = nullptr;
        QTRY_VERIFY2_WITH_TIMEOUT(
            (bar = page->findChild<SARibbonQml::RibbonBar *>()) != nullptr,
            "the page never produced a RibbonBar (ribbon path not taken)",
            5000);

        // 首显断言：bar 的隐式高度必须包含 category 行——minimumMode 为
        // 假（tools.json 默认）时高度 = categoryRowY + category 高，
        // 只有 categoryRowY 说明面板区没画出来（用户报的首显缺陷）。
        QTRY_VERIFY2_WITH_TIMEOUT(bar->implicitHeight() > 0,
                                  "the ribbon bar never produced a height",
                                  5000);
        const int categoryRowY = bar->categoryRowY();
        QVERIFY2(bar->implicitHeight() > categoryRowY + 8,
                 qPrintable((QStringLiteral("the bar height (%1) does not ")
                             + QStringLiteral("include the category row (y=%2)"))
                                .arg(bar->implicitHeight())
                                .arg(categoryRowY)));

        // 当前 category 落位且可见：Start 页在首屏就要在场。
        bool sawVisibleCategory = false;
        const auto categories = bar->findChildren<SARibbonQml::RibbonCategory *>();
        for (SARibbonQml::RibbonCategory *category : categories) {
            if (category->isVisible() && category->height() > 0) {
                sawVisibleCategory = true;
                QVERIFY2(category->y() >= categoryRowY - 1,
                         "a visible category sits above the category row");
            }
        }
        QVERIFY2(sawVisibleCategory,
                 "no visible ribbon category on the first show");

        // 布局不叠窗（2026-10 用户报告右侧像两个窗口叠着）：命令条的装载
        // 高度必须把 RibbonBar 的隐式高度原样带给 ColumnLayout——包装 Item
        // 的 implicitHeight 若与 bar 实际内容高度脱节（首显缺陷的另一种形
        // 态），面板区会越出 Loader 的高度压住下方的 tab 条与右栏顶部。
        // 判据取几何而不是像素：右栏与 tab 条的顶部都在命令条底部之下。
        QQuickItem *const commandBarLoader = bar->parentItem()
            ? bar->parentItem()->parentItem() : nullptr;
        QVERIFY2(commandBarLoader != nullptr,
                 "the RibbonBar is not hosted by the command bar Loader");
        const qreal commandBarBottom = commandBarLoader->y()
            + commandBarLoader->height();
        QVERIFY2(commandBarLoader->height() >= bar->implicitHeight() - 1,
                 qPrintable(QStringLiteral("the loader height (%1) dropped ")
                            + QStringLiteral("below the bar's implicit ")
                            + QStringLiteral("height (%2) — ribbon content ")
                            + QStringLiteral("would overlap the page below")
                              .arg(commandBarLoader->height())
                              .arg(bar->implicitHeight())));
        QQuickItem *const checksPanel = findItemByName(
            page, QStringLiteral("checksPanel"));
        QVERIFY2(checksPanel != nullptr, "the page has no right panel");
        QVERIFY2(checksPanel->mapToScene(QPointF(0, 0)).y()
                     >= commandBarBottom - 1,
                 qPrintable(QStringLiteral("the right panel top (%1) is at ")
                            + QStringLiteral("or above the command bar ")
                            + QStringLiteral("bottom (%2) — the panels overlap")
                              .arg(checksPanel->mapToScene(QPointF(0, 0)).y())
                              .arg(commandBarBottom)));
        QVERIFY2(checksPanel->height() > 100,
                 "the right panel collapsed vertically");

        // delegate 作用域（2026-10 首显缺陷 b）：模板行与 tab 行的处理器
        // 都经 scope 引用页面根——裸 id 的写法在点下去的瞬间抛
        // ReferenceError（消息钩子抓到即失败）。先点一行模板：这会驱动
        // scope.insertTemplate → applyPreset → 草稿会话建立 → tab 出现；
        // 再点那个 tab：驱动 fileTab 处理器里的 scope.switchDraftTo /
        // scope.applyPreset（正是用户日志里 1665 行报错的那条路径）。
        QQuickItem *const templateList = page->findChild<QQuickItem *>(
            QStringLiteral("templateList"));
        QVERIFY2(templateList != nullptr,
                 "the page has no template list");
        QTRY_VERIFY2_WITH_TIMEOUT(
            templateList->property("count").toInt() > 0,
            "the template list never filled with rows", 5000);
        QQuickItem *const contentItem = templateList->property("contentItem")
                                             .value<QQuickItem *>();
        QVERIFY2(contentItem != nullptr,
                 "the template list has no content item");
        QVERIFY2(!contentItem->childItems().isEmpty(),
                 "the template list has no rows instantiated");
        QQuickItem *const firstRow = contentItem->childItems().constFirst();
        const QPointF rowCenter = firstRow->mapToScene(
            QPointF(firstRow->width() / 2, firstRow->height() / 2));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                          rowCenter.toPoint());
        // 模板插入后草稿会话在场 → tab 出现。注意 Repeater 的 delegate 只
        // 有视觉父（QObject 父为 null），findChild 的 QObject 树找不
        // 到——按视觉树找（itemAt 经 QMetaObject，页面 QML 侧的解析
        // 路径同一条）。
        QQuickItem *fileTab = nullptr;
        QTRY_VERIFY2_WITH_TIMEOUT(
            (fileTab = findItemByName(page, QStringLiteral("fileTab")))
                != nullptr,
            "the draft tab never appeared after inserting a template",
            5000);
        const QPointF tabCenter = fileTab->mapToScene(
            QPointF(fileTab->width() / 2, fileTab->height() / 2));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                          tabCenter.toPoint());
        QTRY_COMPARE(page->property("preset").toString(),
                     QStringLiteral("prompt"));
        QVERIFY2(markdown.property("draftActive").toBool(),
                 "the draft session is not active after clicking its tab");

        // 消息钩子全程无告警：ReferenceError（import/scope）、ObjectModel
        // 用法（Delegate must be of Item type / not placed in the
        // graphics scene）、SARibbon 侧问题都不允许出现。
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
        QVERIFY2(g_ribbonWarnings.isEmpty(),
                 qPrintable(QStringLiteral("QML warnings during the page ")
                            + QStringLiteral("first-show: ")
                            + g_ribbonWarnings.join(QLatin1Char(';'))));
    }
};

// Quick 场景需要 QGuiApplication（共享注册表的 QCoreApplication 驱动不了）。
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    TestRibbonUi tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_ribbonui.moc"
