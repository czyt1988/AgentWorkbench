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
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <SARibbonQml/SARibbonQmlGlobal.h>

#include "core/Paths.h"
#include "core/Settings.h"
#include "markdown/MarkdownPreviewController.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
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

using awb::core::Settings;
using awb::markdown::MarkdownPreviewController;
using awb::theme::Theme;
using awb::theme::ThemeRegistry;
using awb::tools::ToolsFacade;

namespace {

// SARibbon 侧的告警一律算失败：叶子资源缺失、宿主找不到引擎这类问题都
// 只以 qWarning 的形式存在，对象图看起来一切正常。
QStringList g_ribbonWarnings;
QtMessageHandler g_previousHandler = nullptr;

void ribbonMessageHandler(QtMsgType type, const QMessageLogContext &context,
                          const QString &message)
{
    if ((type == QtWarningMsg || type == QtCriticalMsg)
        && (message.contains(QLatin1String("SARibbonQml"))
            || message.contains(QLatin1String("leaf resource missing")))) {
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
        MarkdownPreviewController markdown(&settings, &theme);

        QQmlEngine engine;
        // 注册必须先于任何加载——这正是产品 main.cpp 的那一行。
        saRibbonRegisterQmlTypes(&engine);

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
        MarkdownPreviewController markdown(&settings, &theme);
        WinStub win;

        QQmlEngine engine;
        saRibbonRegisterQmlTypes(&engine);

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
