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
#include <SARibbonQml/SARibbonQmlGallery.h>
#include <SARibbonQml/SARibbonQmlTab.h>
#include <SARibbonQml/SARibbonQmlToolButton.h>

#include "core/OpResult.h"
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
// 例外是第四个用例：menuActions 菜单链（SARibbon 3.0 把 2.x 的
// RibbonMenuItem 类型退役，菜单项改为 QAction 列表）是 Ribbon 形态独有
// 的声明面，降级工具栏没有对应物——它经宿主的 activateMenuItem 直取
// QAction::trigger，断言命令真的落到 ToolsFacade 的持久化状态上。
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
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // WorkbenchRibbon 引用 A* 组件（import AgentWorkbench）：Qt 5 引擎
        // 的默认导入路径不含 qrc:/qt/qml（Qt 6 默认有），与整页用例同一条。
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
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

        // 装载进去的 RibbonBar：应用按钮已退役（2026-10：它没有任何
        // 动作——无 applicationMenuActions、无 backstage，「在哪份文档」
        // 由页面 tab 条回答）。标签留空 + 矩形为空 = 按钮整体退场，
        // 这条断言防止它悄悄长回来。
        const auto children = item->childItems();
        QVERIFY2(!children.isEmpty(), "the ribbon root has no bar child");
        QObject *bar = children.constFirst();
        QCOMPARE(bar->property("applicationLabel").toString(), QString());
        QCOMPARE(bar->property("applicationButtonRect").toRectF(), QRectF());

        // 全程不允许出现 SARibbon 侧的告警（qrc 未初始化、引擎不可达）。
        QVERIFY2(g_ribbonWarnings.isEmpty(),
                 qPrintable(QStringLiteral("SARibbon warnings during load: ")
                            + g_ribbonWarnings.join(QLatin1Char(';'))));
    }

    // 上下文页已退役（2026-10：模板库转正为普通 category）。原用例压的
    // 「contextColor 与 accent 可区分」问题随之消失——标题行底色现在就是
    // 应用 chrome 色（主题桥），没有第二套色带。

    // 样式下拉（SARibbon 的组合框经 RibbonControlContainer 嵌进面板）：
    // 选择写 ToolsFacade、当前值显示在下拉上；三行档已退役，旧持久化值
    // 收敛为紧凑（single）。压的是「声明 → 选择 → 门面状态 → 显示回读」
    // 整条链。
    void testStyleComboDrivesToolsState()
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
        WinStub win;

        QQmlEngine engine;
        saRibbonRegisterQmlTypes(&engine);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // WorkbenchRibbon 引用 A* 组件（import AgentWorkbench）：Qt 5 引擎
        // 的默认导入路径不含 qrc:/qt/qml（Qt 6 默认有），与整页用例同一条。
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
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

        QQmlComponent component(&engine, QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/workbench/WorkbenchRibbon.qml")));
        QVERIFY2(!component.isError(),
                 qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create(&context));
        QVERIFY2(!root.isNull(), qPrintable(component.errorString()));
        QQuickItem *const item = qobject_cast<QQuickItem *>(root.data());
        QVERIFY2(item, "the ribbon root is not a QQuickItem");
        // 宿主经根对象的 win 属性接桩（上下文属性会被组件根自己的
        // 同名 property 遮蔽）。
        QVERIFY2(item->setProperty("win", QVariant::fromValue(&win)),
                 "the ribbon root has no win property");

        // Style 下拉（View 标签页）：按 objectName 找（AComboBox 嵌在
        // RibbonControlContainer 里，装载即在视觉树，与当前 tab 无关）。
        QQuickItem *styleCombo = nullptr;
        const auto combos = item->findChildren<QQuickItem *>(
            QStringLiteral("styleCombo"));
        if (!combos.isEmpty()) {
            styleCombo = combos.constFirst();
        }
        QVERIFY2(styleCombo, "no style combo in the ribbon");
        QCOMPARE(styleCombo->property("currentIndex").toInt(), 0);

        // 激活走 ComboBox 的 activated 信号（QML 的 onActivated 写门面，
        // 同步执行）。
        QVERIFY2(QMetaObject::invokeMethod(styleCombo, "activated",
                                           Q_ARG(int, 1)),
                 "activating the combo entry failed");
        QCOMPARE(tools.ribbonStyle(), QStringLiteral("two-row"));
        QTRY_COMPARE(styleCombo->property("currentIndex").toInt(), 1);

        QVERIFY2(QMetaObject::invokeMethod(styleCombo, "activated",
                                           Q_ARG(int, 0)),
                 "activating the combo entry failed");
        QCOMPARE(tools.ribbonStyle(), QStringLiteral("single"));
        QTRY_COMPARE(styleCombo->property("currentIndex").toInt(), 0);

        // 三行档退役：写入旧持久化值，显示面必须收敛回紧凑
        // （styleEnumFor 对未知档回落单行）。
        tools.setRibbonStyle(QStringLiteral("three-row"));
        QTRY_COMPARE(styleCombo->property("currentIndex").toInt(), 0);
    }

    // 「开始」页 icon-only 按钮的两档形态（2026-10 用户报告「宽松档下
    // 图标巨大失衡」的回归）：宽松（two-row）档该批按钮带文字（与标题/
    // 插入面板同一形态——大按钮配 IconOnly 会把图标撑满整个按钮高度，
    // 就是用户看到的巨大图标）；紧凑单行档回到 icon-only 省横向空间。
    void testStartButtonsFollowRibbonStyleMode();

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
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // main.cpp 在 Qt 5 上补的模块导入路径（生成的 qmldir 在
        // qrc:/qt/qml 下）：页面 import AgentWorkbench 取 A* 组件走的是
        // 文件模块，Qt 5 引擎的默认导入路径不含 qrc:/qt/qml（Qt 6 默认
        // 有），漏了它页面报「module AgentWorkbench is not installed」。
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
        // main.cpp 同款 gadget 注册：页面动作层调 tools.createDraft 等
        // 返回 OpResult 的 Q_INVOKABLE——metatype 没注册时 QML 报
        // 「Unknown method return type」、动作静默失效。
        qRegisterMetaType<awb::core::OpResult>();
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
        // 2026-10 重组后的结构断言：右侧业务栏已删（编辑区占满中栏），
        // 体检与历史是非模态悬浮窗（默认关闭，动作打开）；模板库是命令条
        // 「提示词」页里的 gallery。
        QQuickItem *const legacyPanel = findItemByName(
            page, QStringLiteral("checksPanel"));
        QVERIFY2(legacyPanel == nullptr,
                 "the legacy right panel is still around");

        // Popup 关闭时不在视觉树（contentItem 未挂场），按 QObject 树找
        // 窗对象、经 visible 属性判开合。
        QObject *const checksPopup = page->findChild<QObject *>(
            QStringLiteral("checksPopup"));
        QVERIFY2(checksPopup != nullptr, "the page has no checks popup");
        QVERIFY2(!checksPopup->property("visible").toBool(),
                 "the checks popup starts visible");

        QObject *const historyPopup = page->findChild<QObject *>(
            QStringLiteral("historyPopup"));
        QVERIFY2(historyPopup != nullptr, "the page has no history popup");
        QVERIFY2(!historyPopup->property("visible").toBool(),
                 "the history popup starts visible");

        // 悬浮窗的动作开关走页面动作层（真实 QMetaObject 路径）：开体检
        // 窗即跑一次，checkList 在窗内；历史窗随后错位在场。
        QVERIFY2(QMetaObject::invokeMethod(
                     page, "toggleChecksWindow"),
                 "toggling the checks window failed");
        QTRY_VERIFY2_WITH_TIMEOUT(
            checksPopup->property("visible").toBool(),
            "the checks popup never opened", 5000);
        // 浮窗内容挂在窗口 overlay（Popup 的挂场点），不在 page 子树：
        // 从窗口 contentItem 搜。
        QQuickItem *const checkList = findItemByName(
            view.contentItem(), QStringLiteral("checkList"));
        QVERIFY2(checkList, "the checks popup has no check list");
        QVERIFY2(checkList->isVisible(), "the check list is not visible");
        QVERIFY2(QMetaObject::invokeMethod(
                     page, "toggleHistoryWindow"),
                 "toggling the history window failed");
        QTRY_VERIFY2_WITH_TIMEOUT(
            historyPopup->property("visible").toBool(),
            "the history popup never opened", 5000);
        QQuickItem *const snapshotList = findItemByName(
            view.contentItem(), QStringLiteral("snapshotList"));
        QVERIFY2(snapshotList, "the history popup has no snapshot list");

        // tab 条右端的新建草稿（+）：Ribbon 不再放草稿管理面板，这里是
        // 新建动作的唯一常驻入口。
        QQuickItem *const newDraftTabButton = findItemByName(
            page, QStringLiteral("newDraftTabButton"));
        QVERIFY2(newDraftTabButton != nullptr,
                 "the tab bar has no new-draft button");

        // 模板 gallery（SARibbon 真 gallery，2026-10 从自绘格子换入）：
        // 数据驱动填充（addItem/clear 重建），激活走库的 activateItem——
        // 叶子单元格点击的同一终点，页面的 onTriggered 转发
        // win.insertTemplate（未限定的 win 名靠组件作用域链解析，解析不到
        // 恰是消息钩子要抓的 ReferenceError）。切到「提示词」页再激活
        // （tab 序 = Start 0 / Prompt 1 / View 2）。
        SARibbonQml::RibbonGallery *templateGallery = nullptr;
        QTRY_VERIFY2_WITH_TIMEOUT(
            (templateGallery = page->findChild<SARibbonQml::RibbonGallery *>(
                 QStringLiteral("templateGallery"))) != nullptr,
            "the ribbon has no template gallery", 5000);
        QMetaObject::invokeMethod(
            page, "setRibbonTab", Q_ARG(int, 1));
        QTRY_VERIFY2_WITH_TIMEOUT(
            templateGallery->groupCount() > 0
                && templateGallery->groupAt(0)->itemCount() > 0,
            "the template gallery never filled with entries", 5000);
        QVERIFY2(QMetaObject::invokeMethod(
                     templateGallery, "activateItem", Q_ARG(int, 0)),
                 "activating the first template cell failed");
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

    // 2026-10 用户报告「折叠功能区后点 tab 没反应」的回归用例：折叠态点
    // tab 要把 category 行作为悬浮预览再升起（SARibbon 的 minimumModeExpanded），
    // 且页面要把命令条 Loader 的 z 抬到内容之上——预览越过命令条底边绘制，
    // z 只在兄弟之间比较，宿主不抬就会被下方内容盖住（用户看到的就是
    // 「点了没反应」）。几何判据：预览的底边要越过命令条底边（覆盖式，
    // 不推挤内容）。
    void testCollapsedTabClickRaisesCategoryPreview()
    {
        QTemporaryDir dataRoot;
        QVERIFY2(dataRoot.isValid(), "a temporary dir failed");
        QVERIFY2(seedThemes(), "seeding the builtin themes failed");
        Settings settings;
        ThemeRegistry registry;
        Theme theme(&settings, &registry);
        theme.applyTheme(QStringLiteral("mocha-dark"));
        ToolsFacade tools(dataRoot.path());
        // 折叠位在页面装载前写进 tools.json：页面建起时 ribbon 就是折叠态。
        tools.setRibbonCollapsed(true);
        MarkdownEdit markdownEdit(&theme);
        MarkdownPreviewController markdown(&settings, &theme);
        WorkbenchStub workbench;
        NavStub nav;
        UiStub ui;

        QQmlEngine engine;
        saRibbonRegisterQmlTypes(&engine);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
        qRegisterMetaType<awb::core::OpResult>();
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
        QQuickItem *const page = pageOwner.get();

        QQuickView view(&engine, nullptr);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setContent(QUrl(), nullptr, pageOwner.release());
        view.resize(1200, 800);
        view.show();
        QVERIFY2(QTest::qWaitForWindowExposed(&view),
                 "the page window never became exposed");

        SARibbonQml::RibbonBar *bar = nullptr;
        QTRY_VERIFY2_WITH_TIMEOUT(
            (bar = page->findChild<SARibbonQml::RibbonBar *>()) != nullptr,
            "the page never produced a RibbonBar (ribbon path not taken)",
            5000);
        QVERIFY2(bar->isMinimumMode(),
                 "the ribbon did not start in minimum mode");
        QVERIFY2(!bar->isMinimumModeExpanded(),
                 "the collapsed preview started raised");

        QQuickItem *const commandBarLoader = bar->parentItem()
            ? bar->parentItem()->parentItem() : nullptr;
        QVERIFY2(commandBarLoader != nullptr,
                 "the RibbonBar is not hosted by the command bar Loader");
        QVERIFY2(qFuzzyIsNull(commandBarLoader->z()),
                 "the command bar loader is raised without a preview");

        // 点当前 tab（真实点击驱动 RibbonTab 的 MouseArea，与用户操作同一
        // 条路径）：预览升起，当前 category 可见且底边越过命令条底边，
        // 宿主 Loader 的 z 同步抬到内容之上。
        const auto tabs = bar->findChildren<SARibbonQml::RibbonTab *>();
        QVERIFY2(!tabs.isEmpty(), "the ribbon bar has no tabs");
        QQuickItem *const tab = tabs.first();
        const QPointF tabCenter = tab->mapToScene(
            QPointF(tab->width() / 2, tab->height() / 2));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                          tabCenter.toPoint());
        QTRY_VERIFY2_WITH_TIMEOUT(bar->isMinimumModeExpanded(),
                                  "clicking the tab never raised the "
                                  "collapsed preview", 5000);

        SARibbonQml::RibbonCategory *visibleCategory = nullptr;
        const auto categories = bar->findChildren<SARibbonQml::RibbonCategory *>();
        for (SARibbonQml::RibbonCategory *category : categories) {
            if (category->isVisible() && category->height() > 0) {
                visibleCategory = category;
                break;
            }
        }
        QVERIFY2(visibleCategory != nullptr,
                 "no visible category while the collapsed preview is up");
        const qreal previewBottom = bar->mapToScene(
            QPointF(0, visibleCategory->y() + visibleCategory->height())).y();
        const qreal commandBarBottom = commandBarLoader->mapToScene(
            QPointF(0, commandBarLoader->height())).y();
        QVERIFY2(previewBottom > commandBarBottom + 8,
                 qPrintable((QStringLiteral("the preview bottom (%1) does ")
                             + QStringLiteral("not overlay the content below ")
                             + QStringLiteral("the command bar (%2)"))
                                .arg(previewBottom).arg(commandBarBottom)));
        QTRY_VERIFY2_WITH_TIMEOUT(commandBarLoader->z() > 0,
                                  "the command bar loader never raised "
                                  "above the page content", 5000);

        // 折叠预览之外：点击预览区域以外要把它收回去（Qt::Popup 关闭语义
        // 的回归断言——SARibbon 的窗口级事件过滤器负责这条路径）。
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                          QPoint(20, view.height() - 40));
        QTRY_VERIFY2_WITH_TIMEOUT(!bar->isMinimumModeExpanded(),
                                  "clicking outside never dropped the "
                                  "collapsed preview", 5000);
        QVERIFY2(qFuzzyIsNull(commandBarLoader->z()),
                 "the loader stayed raised after the preview dropped");

        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
        QVERIFY2(g_ribbonWarnings.isEmpty(),
                 qPrintable(QStringLiteral("QML warnings during the ")
                            + QStringLiteral("collapsed-preview round: ")
                            + g_ribbonWarnings.join(QLatin1Char(';'))));
    }
};

// 「开始」页 icon-only 按钮的两档形态（2026-10 用户报告「宽松档下开始
// 标签页的 icon-only 图标巨大失衡」的回归用例）：
//   宽松（two-row）——该批按钮带文字（TextBesideIcon，与标题/插入面板
//     同一形态）。根因：面板按钮的 proportion 默认是 Large，大按钮配
//     IconOnly 时核心布局把图标撑满整个按钮高度——两行档的面板高，
//     图标就跟着巨大；
//   紧凑（single）——回到 icon-only（高频动作省横向空间，单行档按钮
//     本来就是小的横排，没有失衡问题）。
// 断言落在两个层面：toolButtonStyle 枚举值（声明面）与 textGeometry
// 的宽度（渲染面——宽松档文字框必须真的长出来，紧凑档必须缩回去）。
void TestRibbonUi::testStartButtonsFollowRibbonStyleMode()
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
    saRibbonRegisterQmlTypes(&engine);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // WorkbenchRibbon 引用 A* 组件（import AgentWorkbench）：Qt 5 引擎的
    // 默认导入路径不含 qrc:/qt/qml（与整页用例同一条）。
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme", &theme);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Tools", &tools);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0,
                                 "MarkdownEdit", &markdownEdit);

    QQmlContext context(engine.rootContext());
    context.setContextProperty(QStringLiteral("theme"), &theme);
    context.setContextProperty(QStringLiteral("tools"), &tools);
    context.setContextProperty(QStringLiteral("markdown"), &markdown);

    QQmlComponent component(&engine, QUrl(QStringLiteral(
        "qrc:/qt/qml/AgentWorkbench/workbench/WorkbenchRibbon.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create(&context));
    QVERIFY2(!root.isNull(), qPrintable(component.errorString()));
    QQuickItem *const item = qobject_cast<QQuickItem *>(root.data());
    QVERIFY2(item, "the ribbon root is not a QQuickItem");

    // 「开始」页是首个 category（默认 currentIndex 0），装载即在 QObject
    // 树上；按源串挑几枚被改的按钮（覆盖剪贴板 / 格式 / 收尾面板）。
    const QStringList probeTexts = { QStringLiteral("Paste"),
                                     QStringLiteral("Bold"),
                                     QStringLiteral("Divider") };

    // 宽松档：带文字（TextBesideIcon = 2，见 RibbonEnums::ToolButtonStyle）。
    tools.setRibbonStyle(QStringLiteral("two-row"));
    for (const QString &probe : probeTexts) {
        SARibbonQml::RibbonToolButton *button = nullptr;
        const auto buttons =
            item->findChildren<SARibbonQml::RibbonToolButton *>();
        for (SARibbonQml::RibbonToolButton *candidate : buttons) {
            if (candidate->text() == probe) {
                button = candidate;
                break;
            }
        }
        QVERIFY2(button != nullptr,
                 qPrintable(QStringLiteral("no Start-tab tool button ")
                            + probe));
        QTRY_COMPARE_WITH_TIMEOUT(
            button->property("toolButtonStyle").toInt(), 2, 5000);
        // 渲染面：宽松档文字框必须长出来（图标不再独占按钮）。
        QTRY_VERIFY2_WITH_TIMEOUT(
            button->property("textGeometry").toRectF().width() > 0,
            qPrintable(QStringLiteral("the loose-mode caption box of ")
                       + probe + QStringLiteral(" never appeared")), 5000);
    }

    // 紧凑档：icon-only（枚举 0），文字框随之缩回去。
    tools.setRibbonStyle(QStringLiteral("single"));
    for (const QString &probe : probeTexts) {
        SARibbonQml::RibbonToolButton *button = nullptr;
        const auto buttons =
            item->findChildren<SARibbonQml::RibbonToolButton *>();
        for (SARibbonQml::RibbonToolButton *candidate : buttons) {
            if (candidate->text() == probe) {
                button = candidate;
                break;
            }
        }
        QVERIFY2(button != nullptr,
                 qPrintable(QStringLiteral("no Start-tab tool button ")
                            + probe));
        QTRY_COMPARE_WITH_TIMEOUT(
            button->property("toolButtonStyle").toInt(), 0, 5000);
        QTRY_VERIFY2_WITH_TIMEOUT(
            button->property("textGeometry").toRectF().width() <= 0,
            qPrintable(QStringLiteral("the compact-mode caption box of ")
                       + probe + QStringLiteral(" did not fold away")), 5000);
    }
}

// Quick 场景需要 QGuiApplication（共享注册表的 QCoreApplication 驱动不了）。
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    TestRibbonUi tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_ribbonui.moc"
