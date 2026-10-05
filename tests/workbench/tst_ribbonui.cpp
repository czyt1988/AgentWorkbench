#include <QtTest>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <SARibbonQml/SARibbonQmlGlobal.h>

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
// 动作逻辑不在这里测：两份命令条都只把点击转发给窗口动作层（窗口的
// 动作由 tst_toolsui 经真实点击覆盖），这里只回答「装得起来、排得出来」。

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

} // namespace

class TestRibbonUi : public QObject
{
    Q_OBJECT

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
