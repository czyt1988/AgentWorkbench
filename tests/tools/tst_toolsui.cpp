#include <QtTest>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/OpResult.h"
#include "core/Settings.h"
#include "markdown/MarkdownPreviewController.h"
#include "markdown/MarkdownRuntime.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
#include "tools/FileTreeModel.h"
#include "tools/MarkdownEdit.h"
#include "tools/ToolsFacade.h"

// tst_toolsui — Markdown 工作台页（提示词模式那条面）的交互级冒烟：
// 真实鼠标点击与键盘输入驱动。按页面加载的冒烟从不点击、从不开窗口，
// 抓不到「handler 抛错整段中止」这类缺陷；本套件补上交互层
// （tst_webengine 加载真实 QML 的同款做法：页面与组件以与 app 相同的
// 模块 URL/别名嵌进本二进制，页面装进 QQuickView 宿主——工作台页住在
// 主窗工作区里，不再是独立窗口，测试的宿主窗口就是它的工作区替身）。
//
// 五个回归锚点：
// 1. 打字 + Ctrl+Z / Ctrl+Y：编辑器 undo/redo（TextControl 标准键链路）；
// 2. 单击目录行展开文件树：Qt 5.15 里 delegate 声明 required property 后
//    裸 index 抛 ReferenceError、toggleExpanded 从未执行——树无法展开
//    （Qt 6 无此坑，只有真实点击能在任一版本拦住回归）；
// 3. 右键弹菜单：统一菜单组件（AMenu/AMenuItem）经 MarkdownContextMenu
//    真实弹出；
// 4. 长文可滚动：编辑区若是裸 TextArea，内容撑出高度后既看不到也翻不到
//    （没有滚动条、滚轮与拖拽皆无效）。
// 5. 文件树右键的 Markdown 预览条目：可见性跟着 workbench 的结论走，
//    点击交出的是这一行的绝对路径。
//
// ToolsFacade 用 QTemporaryDir 数据根，不碰真实数据目录；Markdown 预览
// 控制器也是真的（纯层，available 恒为假——没有 provider 点亮它，窗口
// 因此停在提示词模式，正合本套件的焦点）。
//
// 命令条走降级路径：本目标不链接 SARibbon，窗口里那个 Loader 装载
// WorkbenchRibbon.qml 失败后换 WorkbenchToolbar.qml——工具栏形态正是
// 无 Ribbon 构建的用户看到的那个，顺带被这里冒烟。

using awb::core::Settings;
using awb::markdown::MarkdownPreviewController;
using awb::theme::Theme;
using awb::theme::ThemeRegistry;
using awb::tools::FileTreeModel;
using awb::tools::MarkdownEdit;
using awb::tools::ToolsFacade;

// workbench/ui 桩：窗口加载与交互路径只用 notify/copyText/pickFolder
// 这几个名字，行为不在断言范围内。
//
// 预览那三条是例外：文件树右键的「Preview Markdown」条目可不可用、点下去
// 交出的是哪条路径、双击文件开到哪条车道，全靠 canPreviewMarkdown/
// previewMarkdown/openLocalFileExternal 这三个名字接得上——名字拼错一个
// 字母，绑定抛 TypeError、条目停在默认值上，窗口加载的冒烟照样全绿。所以
// 桩把结论交给用例（previewCapable 由 loadPage 灌，askedAbout/previewed/
// externallyOpened 是来路记录）。「哪些后缀归预览管」的真判定在纯层，
// 由 tst_markdown 覆盖，这里不重复实现一遍。
class WorkbenchStub : public QObject
{
    Q_OBJECT
public:
    bool previewCapable = false;
    QStringList askedAbout;
    QStringList previewed;
    QStringList externallyOpened;

    Q_INVOKABLE void notify(const QString &, const QString &, const QString &) {}
    Q_INVOKABLE void copyText(const QString &) {}
    Q_INVOKABLE bool canPreviewMarkdown(const QString &path)
    {
        askedAbout.append(path);
        return previewCapable;
    }
    Q_INVOKABLE void previewMarkdown(const QString &path)
    {
        previewed.append(path);
    }
    // 会话恢复/落盘：页面 onCompleted 会调；桩给「无可恢复」的空
    // 现场，saveMarkdownSession 只吞掉（本套件不测持久化）。
    Q_INVOKABLE QVariantMap restoreMarkdownSession()
    {
        QVariantMap result;
        result.insert(QStringLiteral("restored"), false);
        return result;
    }
    Q_INVOKABLE void saveMarkdownSession(const QVariantMap &) {}
    // 双击文件树的非 Markdown 文件与导出落地回调（onMarkupWritten）都走
    // 这里；记录来路供双击用例断言（不真开系统程序）。
    Q_INVOKABLE void openLocalFileExternal(const QString &path)
    {
        externallyOpened.append(path);
    }
};

// nav 桩：工作台页是 keepAlive 页，快捷键与回页复查门控在读
// nav.currentPageId——桩恒答「当前页就是工作台页」，页面全程在场。
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
    Q_INVOKABLE void revealFile(const QString &) {}
};

class TestToolsUi : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // 页面装配 + 真实窗口。每个用例独立一份（引擎/对象图互不串扰），
    // 返回 false 表示装配失败——错误串在 QVERIFY2 的输出里可见。
    // 出参：window 是窗口本体（ApplicationWindow 根），objectName 查找
    // 一律从它出发——ApplicationWindow 的声明子项挂在窗口的 QObject 树
    // 上，而不是 contentItem 的子树里。window 归 harness 所有，用例结束
    // 自动销毁。
    bool loadPage(QString *errorOut, QQuickWindow **windowOut,
                  ToolsFacade **toolsOut)
    {
        m_dataRoot = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<Settings>();
        m_registry = std::make_unique<ThemeRegistry>();
        m_theme = std::make_unique<Theme>(m_settings.get(), m_registry.get());
        m_tools = std::make_unique<ToolsFacade>(m_dataRoot->path());
        m_markdownEdit = std::make_unique<MarkdownEdit>(m_theme.get());
        // 纯层控制器：没有 provider 点亮 available，窗口停在提示词模式。
        m_markdown = std::make_unique<MarkdownPreviewController>(
            m_settings.get(), m_theme.get());
        m_workbench = std::make_unique<WorkbenchStub>();
        // 能力开关在页面装配前定死：菜单条目的可见性是一条方法调用绑定，
        // 建好之后不会因桩的成员变量变化重算（QML 追不到 C++ 方法内部的
        // 依赖），所以「有/没有预览能力」必须是两次独立的装配。
        m_workbench->previewCapable = m_previewCapable;
        m_ui = std::make_unique<UiStub>();
        m_nav = std::make_unique<NavStub>();

        m_engine = std::make_unique<QQmlEngine>();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // main.cpp 在 Qt 5 上补的模块导入路径（生成的 qmldir 在
        // qrc:/qt/qml 下）。
        m_engine->addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
        // main.cpp 同款 gadget 注册：页面的动作层调 tools.createDraft 等
        // 返回 OpResult 的 Q_INVOKABLE——metatype 没注册时 QML 报
        // 「Unknown method return type」、整个动作静默失效。
        qRegisterMetaType<awb::core::OpResult>();
        // main.cpp 同款单例注册：窗口经 import AgentWorkbench.App 引用的名字。
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme",
                                     m_theme.get());
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Tools",
                                     m_tools.get());
        qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "MarkdownEdit",
                                     m_markdownEdit.get());

        // 小写别名（theme/tools/workbench/ui/markdown）在 app 里是
        // MainWindow 根部属性，经作用域链对窗口可见；单独创建组件时用
        // 上下文属性提供同名解析（对象就是上面的单例）。这是测试工具的
        // 做法，不是 app 的设计（app 禁用 setContextProperty）。
        m_context = std::make_unique<QQmlContext>(m_engine->rootContext());
        m_context->setContextProperty(QStringLiteral("theme"), m_theme.get());
        m_context->setContextProperty(QStringLiteral("tools"), m_tools.get());
        m_context->setContextProperty(QStringLiteral("workbench"),
                                       m_workbench.get());
        m_context->setContextProperty(QStringLiteral("ui"), m_ui.get());
        m_context->setContextProperty(QStringLiteral("nav"), m_nav.get());
        m_context->setContextProperty(QStringLiteral("markdown"),
                                       m_markdown.get());

        // 页面根是普通 Item（工作台页住在主窗工作区里）：装进 QQuickView
        // 宿主——它本身就是 QQuickWindow，事件注入与 findChild 照旧。
        QQmlComponent component(m_engine.get(), QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/workbench/MarkdownWorkbenchPage.qml")));
        if (component.status() == QQmlComponent::Error) {
            *errorOut = component.errorString();
            return false;
        }
        std::unique_ptr<QQuickItem> page(
            qobject_cast<QQuickItem *>(component.create(m_context.get())));
        if (!page) {
            *errorOut = QStringLiteral(
                "the workbench page root is not a QQuickItem: ")
                + component.errorString();
            return false;
        }
        m_view = std::make_unique<QQuickView>(m_engine.get(), nullptr);
        m_view->setResizeMode(QQuickView::SizeRootObjectToView);
        // 收养创建好的页面：setContent 接管所有权，上下文仍是上面那份。
        m_view->setContent(QUrl(), nullptr, page.release());
        m_view->resize(1200, 800);
        m_view->setTitle(QStringLiteral("tst_toolsui"));
        m_view->show();
        if (!QTest::qWaitForWindowExposed(m_view.get())) {
            *errorOut = QStringLiteral("window never became exposed");
            return false;
        }
        if (!QTest::qWaitForWindowActive(m_view.get())) {
            *errorOut = QStringLiteral("window never became active");
            return false;
        }

        *windowOut = m_view.get();
        *toolsOut = m_tools.get();
        return true;
    }

    void cleanup()
    {
        // 先销毁宿主窗口再销毁对象图：窗口还在渲染时拆掉 C++ 单例会让
        // 渲染帧解引用悬挂指针。
        m_view.reset();
        m_engine.reset();
        m_context.reset();
        m_markdown.reset();
        m_markdownEdit.reset();
        m_tools.reset();
        m_theme.reset();
        m_registry.reset();
        m_settings.reset();
        m_dataRoot.reset();
    }

    // --- 用例 --------------------------------------------------------------

    // 逐键敲一段 ASCII 文本（QTest 对 QWindow 只有 keyClick 单键版，
    // keyClicks 是 QWidget 专用；逐键与真实键盘输入等价）。
    static void typeText(QWindow *window, const QString &text)
    {
        for (const QChar ch : text) {
            QTest::keyClick(window, ch.toLatin1());
        }
    }

    // 编辑器可打字，Ctrl+Z 回退一步输入，Ctrl+Y 重做。TextControl 的标准
    // 键链路（焦点在编辑器上）两端 Qt 大版本都内建；此用例把它锁进回归
    // ——右键菜单里的 Undo/Redo 条目依赖同一条链路。
    void testTypingUndoRedo()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        Q_UNUSED(tools);
        QVERIFY2(loadPage(&error, &window, &tools),
                 qPrintable(error));

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");

        // 点击编辑器获得焦点，然后打字。
        const QPointF center = editor->mapToScene(
            QPointF(editor->width() / 2, editor->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        QVERIFY2(editor->hasActiveFocus(),
                 "clicking the editor did not give it focus");
        typeText(window, QStringLiteral("hello"));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("hello"));

        // Ctrl+Z 回退：一次撤销至少回退一步输入（相邻按键是否合并成同
        // 一条 undo 命令是 QTextDocument 的实现细节，断言只锁行为下限）。
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY2(editor->property("text").toString()
                     != QStringLiteral("hello"),
                 "Ctrl+Z did not undo anything");

        // Ctrl+Y 重做：把刚才撤销的内容带回来。
        QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("hello"));

        // 草稿同步不受撤销/重做干扰（undo 走 text 变化，draft 跟随）。
        QCOMPARE(m_tools->draft(), QStringLiteral("hello"));
    }

    // 长文必须能滚动。回归锚点：编辑区曾是裸 ATextArea——TextArea 的文本
    // 滚不动，内容一超过控件高度就看不见、也翻不到。现在编辑区是
    // Flickable + `TextArea.flickable`，断言链路三段：内容撑出滚动范围、
    // 滚动条随之现身、滚动真的移动了文本。
    void testEditorScrollsLongText()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        Q_UNUSED(tools);
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        QQuickItem *const scroll = window->findChild<QQuickItem *>(
            QStringLiteral("promptScroll"));
        QVERIFY2(scroll, "promptScroll not found by objectName");
        QObject *const bar = window->findChild<QObject *>(
            QStringLiteral("promptScrollBar"));
        QVERIFY2(bar, "promptScrollBar not found by objectName");

        // 空编辑器：布局先给出视口尺寸，内容没撑出视口、滚动范围等于内容。
        QTRY_VERIFY2(scroll->height() > 10,
                     "the editor viewport never got a size from the layout");
        QVERIFY2(scroll->property("contentHeight").toDouble() < scroll->height(),
                 "an empty editor should not have a scrollable area");
        QVERIFY2(!bar->property("visible").toBool(),
                 "the scroll bar is visible although nothing overflows");

        // 造一段远超视口高度的文本（80 行）。
        QString longText;
        for (int i = 0; i < 80; ++i)
            longText += QStringLiteral("line %1\n").arg(i);
        editor->setProperty("text", longText);

        QTRY_VERIFY2(scroll->property("contentHeight").toDouble()
                         > scroll->height(),
                     "long text did not grow the scrollable area");
        QTRY_VERIFY2(bar->property("size").toDouble() < 1.0,
                     "the scroll bar did not appear for overflowing content");
        QVERIFY2(bar->property("visible").toBool(),
                 "the scroll bar stayed hidden although the text overflows");

        // 滚到底：文本整体上移（滚动作用在编辑器上，不是只改了数字）。
        const qreal before = editor->mapToScene(QPointF(0, 0)).y();
        scroll->setProperty("contentY",
                            scroll->property("contentHeight").toDouble());
        QTRY_VERIFY2(editor->mapToScene(QPointF(0, 0)).y() < before - 10,
                     "scrolling the viewport did not move the editor");
    }

    // 单击目录行展开、再单击收起。回归锚点：Qt 5.15 的 delegate 声明
    // required property 后裸 index 抛 ReferenceError，单击 handler 整段
    // 中止——树永远无法展开（Qt 6 下同一份代码正常，只有真实点击能在
    // 任一版本拦住回归）。
    void testTreeExpandOnClick()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools),
                 qPrintable(error));

        // 造一棵两层树：subdir/（内含 a.txt）+ top.md（目录排序在前）。
        const QString root = m_dataRoot->path() + QStringLiteral("/ws");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/subdir")));
        QFile file(root + QStringLiteral("/subdir/a.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("a");
        file.close();
        QFile top(root + QStringLiteral("/top.md"));
        QVERIFY(top.open(QIODevice::WriteOnly));
        top.write("t");
        top.close();
        QVERIFY(tools->addWorkspace(root).ok);

        QQuickItem *const tree = window->findChild<QQuickItem *>(
            QStringLiteral("fileTree"));
        QVERIFY2(tree, "fileTree not found by objectName");
        QCOMPARE(tools->model()->rowCount(), 2); // subdir + top.md

        // 工作区设置时树初始不可见（无工作区状态 visible=false），Layout
        // 在 visible 翻转 + polish 之后才给它尺寸；等布局就绪再点击。
        QTRY_VERIFY2(tree->width() > 10 && tree->height() > 10,
                     "file tree never got a size from the layout");
        // 首行渲染完成（contentItem 上出现 delegate）。
        QTRY_COMPARE(tree->property("count").toInt(), 2);

        // 单击第一行（目录）中心：展开，子行插入投影。
        const QPointF rowCenter = tree->mapToScene(QPointF(30, 14));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          rowCenter.toPoint());
        QTRY_COMPARE(tools->model()->rowCount(), 3); // + subdir/a.txt

        // 再单击同一行：收起（toggle 语义）。
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          rowCenter.toPoint());
        QTRY_COMPARE(tools->model()->rowCount(), 2);
    }

    // 右键编辑器弹菜单：统一菜单组件经真实右键路径弹出。同时验证菜单
    // 里 Undo 条目的可用性绑定在打字后为真（enabled 绑定到 editor 的
    // canUndo——Q_PROPERTY 经 required 链路可达，静默失效在这里会以
    // 条目永远 disabled 的形式暴露）。
    void testContextMenuOpens()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        Q_UNUSED(tools);
        QVERIFY2(loadPage(&error, &window, &tools),
                 qPrintable(error));

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY(editor != nullptr);
        // Menu 是 Popup（QObject，非 Item），查找类型用 QObject。
        QObject *const menu = window->findChild<QObject *>(
            QStringLiteral("editorMenu"));
        QVERIFY2(menu, "editorMenu not found by objectName");

        // 打一段字让 Undo 条目变为可用，再右键弹菜单。
        const QPointF center = editor->mapToScene(
            QPointF(editor->width() / 2, editor->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        typeText(window, QStringLiteral("hi"));
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                          center.toPoint());
        // opened 要等 enter transition（淡入）跑完才置位，这里等它。
        QTRY_VERIFY2(menu->property("opened").toBool(),
                     "right-click did not open the context menu");

        // Undo 条目：enabled 绑定 editor.canUndo。
        QObject *undoItem = nullptr;
        const auto items = menu->findChildren<QObject *>();
        for (QObject *child : items) {
            if (child->property("text").toString()
                    == QStringLiteral("Undo")) {
                undoItem = child;
                break;
            }
        }
        QVERIFY2(undoItem, "Undo menu item not found in editorMenu");
        QVERIFY2(undoItem->property("enabled").toBool(),
                 "Undo item is disabled despite typed content");

        // 收菜单，别把弹窗留给下一个用例。
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!menu->property("opened").toBool());
    }

    // 左栏（工作区面板）宽度手柄的拖拽语义（2026-10 用户报告「一拖整个
    // 面板就收起来」的回归）：向右拖 80 面板要跟着变宽且保持展开；随后
    // 把指针拖到收起阈值以下才收起。宽度公式必须按指针的绝对位置减去
    // 按下时记录的**恒定**抓取偏移——手柄会随面板宽度挪位置，逐事件读
    // mouse.x（早前版本甚至减 leftPanel.width）都会把拖动算错。
    void testLeftPanelHandleDragResizesNotCollapses()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools),
                 qPrintable(error));

        QQuickItem *const handle = window->findChild<QQuickItem *>(
            QStringLiteral("leftPanelHandle"));
        QVERIFY2(handle != nullptr, "leftPanelHandle not found by objectName");
        QVERIFY2(handle->isVisible() && handle->width() > 4,
                 "the left panel handle is not visible or too small");
        // 面板本体：宽度状态住在 WorkspaceTreePanel 里（组件化时从页面
        // 属性搬进去的——它只在面板内部用，搬走后面板才能独立复用）。
        QQuickItem *const panel = window->findChild<QQuickItem *>(
            QStringLiteral("leftPanel"));
        QVERIFY2(panel != nullptr, "leftPanel not found by objectName");
        // 页面根（promptLeftOpen 的持有者）：setContent 把页面根挂成
        // contentItem 的独子。
        QQuickItem *const page = window->contentItem()->childItems()
                .isEmpty() ? nullptr
                           : window->contentItem()->childItems().constFirst();
        QVERIFY2(page != nullptr, "the page root is not on the window");
        const qreal widthBefore = panel->property("panelWidth").toReal();
        QVERIFY2(page->property("promptLeftOpen").toBool(),
                 "the workspace panel did not start open");

        // --- 拖宽 +80：面板跟手变宽，收起分支绝不允许触发 -----------------
        const QPointF start = handle->mapToScene(
            QPointF(handle->width() / 2, handle->height() / 2));
        const QPoint from = start.toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, QPoint(from.x() + 40, from.y()));
        QTest::mouseMove(window, QPoint(from.x() + 80, from.y()));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                            QPoint(from.x() + 80, from.y()));
        QTRY_VERIFY2_WITH_TIMEOUT(
            qAbs(panel->property("panelWidth").toReal()
                 - (widthBefore + 80)) <= 2,
            qPrintable(QStringLiteral("the panel width stayed at ")
                       + QString::number(
                             panel->property("panelWidth").toReal())
                       + QStringLiteral(" after a +80 drag")), 5000);
        QVERIFY2(page->property("promptLeftOpen").toBool(),
                 "an ordinary widening drag collapsed the panel");

        // --- 把指针拖到阈值以下：这次才收起（窄边的展开按钮接管） --------
        // 手柄已随宽度右移，重新取它当前的位置。
        const QPointF start2 = handle->mapToScene(
            QPointF(handle->width() / 2, handle->height() / 2));
        const QPoint from2 = start2.toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from2);
        QTest::mouseMove(window, QPoint(from2.x() - 320, from2.y()));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                            QPoint(from2.x() - 320, from2.y()));
        QTRY_VERIFY2_WITH_TIMEOUT(
            !page->property("promptLeftOpen").toBool(),
            "dragging far past the threshold never collapsed the panel",
            5000);
    }

    // 文件树右键的「Preview Markdown」：workbench 说有预览能力时条目可用，
    // 真实点击把这一行的绝对路径交给 previewMarkdown。
    void testTreeRowPreviewMenuItemEnabledAndOpens()
    {
        m_previewCapable = true;
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));
        runRowMenuCase(window, tools, true);
    }

    // 同一个菜单在目标不适用时（workbench 恒答否）：条目**可见但禁用**
    //（2026-10 用户报告：隐藏会让菜单顶部空出一块，且无从得知「动作存在
    // 但此处不可用」），也不会因为点在别处而误发预览。
    void testTreeRowPreviewMenuItemDisabledWithoutCapability()
    {
        m_previewCapable = false;
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));
        runRowMenuCase(window, tools, false);
    }

    // 双击文件的打开路由（动作层 openFileFromTree）：canPreviewMarkdown
    // 说「行」时交给 previewMarkdown 开成 tab，说「不行」时交给系统打开
    // 器。桩不带后缀判定（真判定在纯层，由 tst_markdown 覆盖），两条
    // 车道就用两种装配各压一条。双击手势本身（TapHandler 的双击识别）
    // 是框架行为，这里锁页面收到路径后的分车道；路由不碰文件树，无需
    // 装配工作区。
    void testTreeDoubleClickOpensFile()
    {
        // 车道一：可以预览 → previewMarkdown（新 tab 的入口）。
        m_previewCapable = true;
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));
        QObject *const pageRoot = m_view->rootObject();
        QVERIFY2(pageRoot, "no page root in the host view");
        const QString docPath = m_dataRoot->path()
                + QStringLiteral("/note.md");
        QFile doc(docPath);
        QVERIFY(doc.open(QIODevice::WriteOnly));
        doc.write("# hi\n");
        doc.close();
        QVERIFY(QMetaObject::invokeMethod(pageRoot, "openFileFromTree",
                                          Q_ARG(QVariant, QVariant(docPath))));
        QVERIFY2(m_workbench->previewed.contains(docPath),
                 qPrintable(QStringLiteral("openFileFromTree routed ")
                            + docPath + QStringLiteral(" to ")
                            + m_workbench->previewed.join(QLatin1Char(';'))
                            + QStringLiteral(" instead of previewMarkdown")));
        QVERIFY(m_workbench->externallyOpened.isEmpty());

        // 车道二：不能预览（无能力的构建 / 不归预览管的文件）→ 系统打
        // 开器；同一个双击入口两种车道，不串。loadPage 会换新的临时数据
        // 目录，文件跟着新目录重建。
        m_previewCapable = false;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));
        QObject *const pageRoot2 = m_view->rootObject();
        QVERIFY2(pageRoot2, "no page root in the host view");
        const QString docPath2 = m_dataRoot->path()
                + QStringLiteral("/note.md");
        QFile doc2(docPath2);
        QVERIFY(doc2.open(QIODevice::WriteOnly));
        doc2.write("# hi\n");
        doc2.close();
        QVERIFY(QMetaObject::invokeMethod(pageRoot2, "openFileFromTree",
                                          Q_ARG(QVariant, QVariant(docPath2))));
        QVERIFY2(m_workbench->externallyOpened.contains(docPath2),
                 qPrintable(QStringLiteral("openFileFromTree routed ")
                            + docPath2 + QStringLiteral(" to ")
                            + m_workbench->externallyOpened.join(
                                  QLatin1Char(';'))
                            + QStringLiteral(" instead of the system opener")));
        QVERIFY(m_workbench->previewed.isEmpty());
    }

    // 右键菜单的「Insert path at cursor」：双击改开文件后，插引用的指针
    // 入口搬到这里（原双击的行为）。点击后草稿编辑器收到这一行的
    // `./相对路径` 引用（形状与拖放共用 ToolsFacade::fileReference）。
    void testTreeRowInsertPathMenuItem()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        const QString root = m_dataRoot->path()
                + QStringLiteral("/insertws");
        QVERIFY(QDir().mkpath(root));
        const QString docPath = QDir(root).filePath(QStringLiteral("note.md"));
        QFile doc(docPath);
        QVERIFY(doc.open(QIODevice::WriteOnly));
        doc.write("# hi\n");
        doc.close();
        QVERIFY(tools->addWorkspace(root).ok);

        QQuickItem *const tree = window->findChild<QQuickItem *>(
            QStringLiteral("fileTree"));
        QVERIFY(tree);
        QTRY_VERIFY2(tree->width() > 10 && tree->height() > 10,
                     "file tree never got a size from the layout");
        QTRY_COMPARE(tree->property("count").toInt(), 1);
        QTRY_VERIFY2_WITH_TIMEOUT(rowMenu(tree) != nullptr,
                                  "the file tree never rendered a row delegate",
                                  4000);

        // 右键行中心弹出菜单，找「Insert path at cursor」条目。
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                          tree->mapToScene(QPointF(30, 12)).toPoint());
        QObject *const menu = rowMenu(tree);
        QVERIFY2(menu, "treeRowMenu not found by objectName");
        QTRY_VERIFY2(menu->property("opened").toBool(),
                     "right-click did not open the tree row menu");

        QObject *insertItem = nullptr;
        const auto items = menu->findChildren<QObject *>();
        for (QObject *child : items) {
            if (child->property("text").toString()
                    == QStringLiteral("Insert path at cursor")) {
                insertItem = child;
                break;
            }
        }
        QVERIFY2(insertItem,
                 "Insert path at cursor item not found in treeRowMenu");
        QVERIFY2(insertItem->property("enabled").toBool(),
                 "Insert path at cursor must be enabled on a file row");

        // 真实点击：草稿编辑器收到 `./note.md`（fileReference 的形状：
        // 反引号包裹的相对路径）。
        QQuickItem *const row = qobject_cast<QQuickItem *>(insertItem);
        QVERIFY2(row, "Insert path at cursor item is not a QQuickItem");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          row->mapToScene(QPointF(row->width() / 2.0,
                                                  row->height() / 2.0)).toPoint());
        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        const QString draft = editor->property("text").toString();
        QVERIFY2(draft.contains(QStringLiteral("`./note.md`")),
                 qPrintable(QStringLiteral("draft does not contain the file ")
                            + QStringLiteral("reference: ") + draft.left(120)));
    }

    // --- 模板与收藏（M2 的界面冒烟） ---------------------------------------

    // 模板入口（2026-10 重组后）：左栏模板段退役，插入入口是工具栏的
    // 模板菜单（菜单条目 = 模板表）。这条用例守住「入口按钮在场 + 数据
    // 层的内置模板可取」——菜单的真实弹出与插入走 insertTemplate 主线
    // 用例（同一条链路）。
    void testTemplateListShowsBuiltins()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 插入/存模板/存收藏三个入口都在（无 Ribbon 构建的模板三件套）。
        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("insertTemplateButton"));
        QVERIFY2(button, "insertTemplateButton not found by objectName");
        QVERIFY2(window->findChild<QQuickItem *>(
                     QStringLiteral("saveTemplateButton")),
                 "saveTemplateButton not found by objectName");
        QVERIFY2(window->findChild<QQuickItem *>(
                     QStringLiteral("saveFavoriteButton")),
                 "saveFavoriteButton not found by objectName");
        QVERIFY2(button->isEnabled(), "the template entry is disabled");

        // 数据层带着随包的内置模板（菜单条目的数据源）。
        QTRY_VERIFY2_WITH_TIMEOUT(
            tools->property("templates").toList().size() >= 10,
            "the template table never showed the builtins",
            4000);
    }

    // 左栏的收起键收起的是**整个左栏**（2026-10 用户报告：段折叠开关是早期
    // 左栏多段布局的遗留，点它应当与把分隔条拖到最左边同效——左栏缩成
    // 窄边，窄边上的展开键是收起态唯一的还原入口）。断言三层：收起后左栏
    // 宽度就是窄边宽（26）、窄边展开键可见（还原入口在场）、再点它左栏
    // 回到展开宽。拖拽手柄的「拖到 140 以下即收起」与这个按钮收的是同一份
    // 状态（promptLeftOpen / documentLeftOpen）。
    void testLeftPanelCollapseAndRestore()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 提示词预设默认左栏开（本套件的窗口停在提示词模式）。
        QQuickItem *const panel = window->findChild<QQuickItem *>(
            QStringLiteral("leftPanel"));
        QQuickItem *const toggle = window->findChild<QQuickItem *>(
            QStringLiteral("workspaceSectionToggle"));
        QQuickItem *const restore = window->findChild<QQuickItem *>(
            QStringLiteral("leftPanelRestoreButton"));
        QVERIFY2(panel, "leftPanel not found by objectName");
        QVERIFY2(toggle, "workspaceSectionToggle not found by objectName");
        QVERIFY2(restore, "leftPanelRestoreButton not found by objectName");

        QTRY_VERIFY2_WITH_TIMEOUT(panel->width() > 200,
                                  "the left panel did not start expanded",
                                  4000);

        // 点收起：左栏缩成窄边，段头与开关随之退场（旧实现只折段、左栏
        // 留着一段空白——那就是用户报告的遗留行为）。
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          toggle->mapToScene(QPointF(
                              toggle->width() / 2.0,
                              toggle->height() / 2.0)).toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(panel->width(), 26.0, 4000);
        QVERIFY2(restore->isVisible(),
                 "the strip restore button must stay visible to re-expand");

        // 点窄边展开键：左栏回到展开宽。
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          restore->mapToScene(QPointF(
                              restore->width() / 2.0,
                              restore->height() / 2.0)).toPoint());
        QTRY_VERIFY2_WITH_TIMEOUT(panel->width() > 200,
                                  "the left panel did not expand back",
                                  4000);
    }

    // 无工作区时左栏的布局契约（2026-10 用户报告「标题与收起键悬在面板
    // 中间、组合框不贴顶」）。两个回归锚点：
    // 1. workspaceSection 要从面板顶部附近开始并一直填到底。Quick
    //    Layouts 把 visible:false 的子项整个排除出布局（shouldIgnoreItem），
    //    无工作区时段里唯一可增长项（文件树）不可见；空态若只有固定高，
    //    段落对上层上报的最大高度就塌缩成隐式高——Layout.fillHeight 无从
    //    伸长，剩余高度变成放大后的空格子、整段内容在格内居中。空态改
    //    fillHeight 后段落恢复可增长。
    // 2. 收起键要落在段头标题行里。ASectionHeader 的 extra 槽曾是普通
    //    alias（不是 default property），使用点声明的按钮被收进段头列自
    //    己的 data、排到分割线下方。
    void testLeftPanelPinnedWithoutWorkspace()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        QQuickItem *const panel = window->findChild<QQuickItem *>(
            QStringLiteral("leftPanel"));
        QQuickItem *const section = window->findChild<QQuickItem *>(
            QStringLiteral("workspaceSection"));
        QQuickItem *const toggle = window->findChild<QQuickItem *>(
            QStringLiteral("workspaceSectionToggle"));
        QVERIFY2(panel, "leftPanel not found by objectName");
        QVERIFY2(section, "workspaceSection not found by objectName");
        QVERIFY2(toggle, "workspaceSectionToggle not found by objectName");

        QTRY_VERIFY2_WITH_TIMEOUT(panel->height() > 400,
                                  "the left panel never got its height",
                                  4000);

        // 段落贴顶：起点紧随组合框行（行高 32 + 边距），异常时在 ~
        // 面板中部（368/759）。
        const qreal sectionTop =
                section->mapToItem(panel, QPointF(0, 0)).y();
        QTRY_VERIFY2_WITH_TIMEOUT(sectionTop <= 90.0,
                                  qPrintable(QStringLiteral(
                                      "the workspace section starts at ")
                                      + QString::number(sectionTop)
                                      + QStringLiteral(", not near the top")),
                                  4000);

        // 段落填满到底：fillHeight 要真的伸长（异常时只有隐式高 ~221）。
        QTRY_VERIFY2_WITH_TIMEOUT(
            sectionTop + section->height() >= panel->height() - 10.0,
            qPrintable(QStringLiteral(
                "the workspace section ends at ")
                + QString::number(sectionTop + section->height())
                + QStringLiteral(" of ")
                + QString::number(panel->height())
                + QStringLiteral(", it must fill the panel")),
            4000);

        // 收起键在段头标题行内（分割线之上）：相对段落起点应 ~0，
        // 异常时在 +29（分割线下方）。
        const qreal toggleTop = toggle->mapToItem(panel, QPointF(0, 0)).y();
        QVERIFY2(toggleTop - sectionTop < 20.0,
                 qPrintable(QStringLiteral(
                     "the collapse toggle sits at ")
                     + QString::number(toggleTop - sectionTop)
                     + QStringLiteral(" below the section top; it belongs in "
                                      "the header row, above the separator")));
    }

    // 点击模板行 → 变量表单弹出 → 确认 → 渲染产物进草稿编辑器。
    // 变量全部留空也没关系：跳过语义 = 占位符原样保留，渲染产物里看得见
    // 模板的第一行结构（「## Symptom」来自 fix-a-bug 模板正文）。
    void testTemplateInsertWithVariableForm()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 页面根（工作台页装在宿主视图里）：调它的 QML 函数要经根
        // 对象——宿主窗口上没有那些方法。
        QObject *const pageRoot = m_view->rootObject();
        QVERIFY2(pageRoot, "no page root in the host view");

        // 直接调窗口的 insertTemplate（行点击的视觉冒烟交给与表单同一条
        // 链路：先锁「点击模板 → 表单弹出 → 确认 → 产物落草稿」这条主线；
        // 行的可点击性由列表宽度契约保证，并入后续手动验证）。
        const QVariantList templates = tools->property("templates").toList();
        QVERIFY(templates.size() >= 1);
        const QString firstId = templates.at(0).toMap()
                .value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(pageRoot, "insertTemplate",
                                          Q_ARG(QVariant, QVariant(firstId))));

        // 变量表单弹出（fix-a-bug 有变量，一定走表单）。
        QObject *const form = window->findChild<QObject *>(
            QStringLiteral("variableForm"));
        QTRY_VERIFY2_WITH_TIMEOUT(form && form->property("opened").toBool(),
                                  "insertTemplate did not open the variable "
                                  "form",
                                  4000);

        // 确认（Insert 按钮）：不填任何字段——空值 = 跳过。
        QQuickItem *insert = nullptr;
        const auto children = form->findChildren<QQuickItem *>();
        for (QQuickItem *child : children) {
            if (child->property("text").toString()
                    == QStringLiteral("Insert")) {
                insert = child;
                break;
            }
        }
        QVERIFY2(insert, "Insert button not found in the variable form");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          insert->mapToScene(QPointF(
                              insert->width() / 2.0,
                              insert->height() / 2.0)).toPoint());

        // 表单关掉，草稿编辑器收到了渲染产物：fix-a-bug 的正文结构
        // （## Symptom）+ 留空的占位符（{{symptom}}）都在。
        QTRY_VERIFY2_WITH_TIMEOUT(!form->property("opened").toBool(),
                                  "the variable form never closed",
                                  4000);
        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        const QString text = editor->property("text").toString();
        QVERIFY2(text.contains(QStringLiteral("## Symptom")),
                 qPrintable(QStringLiteral("draft does not contain the "
                                           "template structure: ")
                            + text.left(120)));
        // 跳过语义：无默认值的变量（step-1）留空 → 占位符原样保留。
        QVERIFY2(text.contains(QStringLiteral("{{step-1}}")),
                 qPrintable(QStringLiteral("skipped variables must stay "
                                           "as placeholders: ")
                            + text.left(200)));
    }

    // 收藏插入（2026-10 重组后）：收藏列表在命令条里（Ribbon 构建的
    // gallery / 无 Ribbon 构建的存入按钮），列表行的点击路径在 tst_ribbonui
    // 的 gallery 冒烟里压。这里守数据与动作层：存一条 → 插入函数把 body
    // 送进草稿编辑器。
    void testFavoriteInsertFromList()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 页面根（工作台页装在宿主视图里）：调它的 QML 函数要经根
        // 对象——宿主窗口上没有那些方法。
        QObject *const pageRoot = m_view->rootObject();
        QVERIFY2(pageRoot, "no page root in the host view");

        // 数据层直接存一条（UI 存入路径的工具栏按钮冒烟在下面）。
        tools->addFavorite(QStringLiteral("Fav title"),
                           QStringLiteral("Favorite body text."));
        QCOMPARE(tools->property("favorites").toList().size(), 1);

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        QVERIFY(QMetaObject::invokeMethod(
            pageRoot, "insertTextAtCursor",
            Q_ARG(QVariant,
                  QVariant(QStringLiteral("Favorite body text.")))));
        QTRY_VERIFY2(editor->property("text").toString()
                         .contains(QStringLiteral("Favorite body text.")),
                     "insertTextAtCursor did not reach the editor",
                     4000);
    }

    // 工具栏「Save the draft to favorites」按钮真实点击：成稿入收藏。
    // 测试二进制没注册 SARibbon → 窗口装载降级工具栏 → 这条冒烟守的
    // 正是无 Ribbon 构建里模板/收藏三按钮的形态。
    void testToolbarSavesDraftToFavorites()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 先在草稿里打字（收藏按钮的 enabled 绑非空草稿）。
        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        const QPointF center = editor->mapToScene(QPointF(
            editor->width() / 2.0, editor->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        typeText(window, QStringLiteral("toolbar favorite body"));

        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("saveFavoriteButton"));
        QVERIFY2(button, "saveFavoriteButton not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(button->property("enabled").toBool(),
                                  "save-favorite button never became "
                                  "enabled for a non-empty draft",
                                  4000);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(
                              button->width() / 2.0,
                              button->height() / 2.0)).toPoint());
        const QVariantList favorites = tools->property("favorites").toList();
        QCOMPARE(favorites.size(), 1);
        QVERIFY2(favorites.at(0).toMap()
                     .value(QStringLiteral("body")).toString()
                     .contains(QStringLiteral("toolbar favorite body")),
                 "the saved favorite does not contain the typed text");

        // Insert Template 与 Save as Template 按钮也在场（同一排）。
        QVERIFY(window->findChild<QObject *>(
            QStringLiteral("insertTemplateButton")));
        QVERIFY(window->findChild<QObject *>(
            QStringLiteral("saveTemplateButton")));
    }

    // --- M3 冒烟 ------------------------------------------------------------

    /// B2：Ctrl+F 在提示词预设开草稿查找条（不是文档搜索条），Esc 关闭。
    // 统一查找（2026-10 重组后）：Ctrl+F 把焦点送进命令条的常驻查找框；
    // 打字 → 回车 → 命中在草稿编辑器里被选中。查找条已随旧交互退役。
    void testDraftFindBarOpensWithCtrlF()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 草稿先有一段正文（查找目标）。
        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          editor->mapToScene(QPointF(
                              editor->width() / 2.0,
                              editor->height() / 2.0)).toPoint());
        typeText(window, QStringLiteral("alpha beta gamma"));

        // Ctrl+F：焦点直达命令条查找框（无 Ribbon 构建的落点是工具栏
        // 上的常驻输入框）。
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QQuickItem *const field = window->findChild<QQuickItem *>(
            QStringLiteral("toolbarFindField"));
        QVERIFY2(field, "toolbarFindField not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(field->property("activeFocus").toBool(),
                                  "Ctrl+F did not focus the find field",
                                  4000);
        QVERIFY2(field->property("text").toString().isEmpty(),
                 "the find field starts with leftover text");

        // 打字 → 回车：命中在编辑器里选中。
        typeText(window, QStringLiteral("beta"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY2_WITH_TIMEOUT(
            editor->property("selectedText").toString()
                == QStringLiteral("beta"),
            "Enter in the find field did not select the hit in the editor",
            4000);
    }

    /// B1：一个新格式动作（编号列表）经工具栏真实点击后草稿文本变化。
    void testNumberedListButtonFormatsDraft()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        const QPointF center = editor->mapToScene(QPointF(
            editor->width() / 2.0, editor->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        typeText(window, QStringLiteral("step one"));

        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("numberedButton"));
        QVERIFY2(button, "numberedButton not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(button->property("enabled").toBool(),
                                  "numbered-list button never enabled",
                                  4000);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(
                              button->width() / 2.0,
                              button->height() / 2.0)).toPoint());
        QTRY_VERIFY2_WITH_TIMEOUT(
            editor->property("text").toString()
                .contains(QStringLiteral("1. ")),
            "numbered-list button did not prefix the line",
            4000);
    }

    /// Shift+Tab 走完整的 QML 键链路（Keys.onBacktabPressed → 页面门控 →
    /// MarkdownEdit.handleBacktab）：多行选区逐行退一个缩进单位，并成为
    /// 一次可撤销的编辑。
    void testShiftTabOutdentsSelectedLines()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        editor->setProperty("text", QStringLiteral("\taa\n\tbb"));
        const QPointF center = editor->mapToScene(
            QPointF(editor->width() / 2.0, editor->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        QVERIFY2(editor->hasActiveFocus(),
                 "clicking the editor did not give it focus");
        QVERIFY(QMetaObject::invokeMethod(editor, "selectAll"));

        QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_COMPARE(editor->property("text").toString(),
                     QStringLiteral("aa\nbb"));

        // 与 Tab 一样是一个撤销步：Ctrl+Z 整体回到缩进状态。
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(editor->property("text").toString(),
                     QStringLiteral("\taa\n\tbb"));
    }

    /// B9 + C4：体检面板在场可触发；预算估算出现在状态条。
    void testChecksPanelAndBudgetRun()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 草稿打一段「会命中 no-acceptance」的文本（没有任何验收词）。
        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        const QPointF center = editor->mapToScene(QPointF(
            editor->width() / 2.0, editor->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        typeText(window, QStringLiteral("please improve it somehow"));

        // 体检已改为非模态悬浮窗（2026-10）：工具栏按钮开窗（开窗顺路
        // 跑一次体检）。真实点击按钮。
        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("checkPromptToolbarButton"));
        QVERIFY2(button, "checkPromptToolbarButton not found by objectName");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(
                              button->width() / 2.0,
                              button->height() / 2.0)).toPoint());

        // 悬浮窗打开（挂在窗口 overlay，经 QObject 树找）。
        QObject *const popup = window->findChild<QObject *>(
            QStringLiteral("checksPopup"));
        QVERIFY2(popup, "checksPopup not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(popup->property("visible").toBool(),
                                  "the checks popup never opened",
                                  4000);

        // 结果列表出现且至少一条命中（这段文本同时命中 vague-wording
        // 与 no-acceptance）。列表挂在浮窗内容里（overlay 子树）。
        QQuickItem *const list = qobject_cast<QQuickItem *>(
            window->findChild<QQuickItem *>(
                QStringLiteral("checkList")));
        QVERIFY2(list, "checkList not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(list->property("count").toInt() > 0,
                                  "no check findings appeared after a click",
                                  4000);

        // 预算估算：状态条上的 Label（objectName: draftBudgetLabel）。
        QQuickItem *const budget = window->findChild<QQuickItem *>(
            QStringLiteral("draftBudgetLabel"));
        QVERIFY2(budget, "draftBudgetLabel not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(
            budget->property("text").toString()
                .contains(QStringLiteral("token")),
            "the budget label does not show a token estimate",
            4000);
    }

    // --- M4 冒烟 ------------------------------------------------------------

    /// 多草稿：新建（工具栏真实点击）→ 清单 +1 且编辑器清空；切回旧
    /// 草稿 → 编辑器换回旧正文（undo 栈随 text 重置不串草稿）。
    void testDraftCreateAndSwitch()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 页面根（工作台页装在宿主视图里）：调它的 QML 函数要经根
        // 对象——宿主窗口上没有那些方法。
        QObject *const pageRoot = m_view->rootObject();
        QVERIFY2(pageRoot, "no page root in the host view");

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        const QPointF center = editor->mapToScene(QPointF(
            editor->width() / 2.0, editor->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        typeText(window, QStringLiteral("first draft body"));

        const QVariantList before = tools->property("drafts").toList();
        QCOMPARE(before.size(), 1);

        // 新建草稿收进 tab 条右端（2026-10：Ribbon 不再放草稿面板）。
        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("newDraftTabButton"));
        QVERIFY2(button, "newDraftTabButton not found by objectName");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(
                              button->width() / 2.0,
                              button->height() / 2.0)).toPoint());
        QTRY_VERIFY2_WITH_TIMEOUT(
            tools->property("drafts").toList().size() == 2,
            "New Draft did not create a second draft",
            4000);
        // 新草稿是空的可写主体：编辑器清空、当前草稿换人。
        QTRY_VERIFY2_WITH_TIMEOUT(
            editor->property("text").toString().isEmpty(),
            qPrintable(QStringLiteral(
                "the editor did not switch to the empty new draft; text=")
                + editor->property("text").toString().left(120)),
            4000);

        // 切回第一条：正文回来。
        const QString firstId = before.at(0).toMap()
                .value(QStringLiteral("id")).toString();
        QVERIFY(QMetaObject::invokeMethod(pageRoot, "switchDraftTo",
                                          Q_ARG(QVariant,
                                                QVariant(firstId))));
        QTRY_VERIFY2_WITH_TIMEOUT(
            editor->property("text").toString()
                .contains(QStringLiteral("first draft body")),
            "switching back did not restore the first draft's text",
            4000);
    }

    /// 快照与回滚：存一版 → 历史列表有行 → 回滚（确认弹窗真实点击）→
    /// 编辑器回到那版正文（当前版被自动存为 rollback 快照，历史不清空）。
    void testSnapshotSaveAndRollback()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 页面根（工作台页装在宿主视图里）：调它的 QML 函数要经根
        // 对象——宿主窗口上没有那些方法。
        QObject *const pageRoot = m_view->rootObject();
        QVERIFY2(pageRoot, "no page root in the host view");

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        const QPointF center = editor->mapToScene(QPointF(
            editor->width() / 2.0, editor->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        typeText(window, QStringLiteral("version one body"));

        // 历史已改为非模态悬浮窗（2026-10）：先开窗（历史按钮在工具栏
        // 上，真实点击），再点窗里的 Save version。
        QQuickItem *const historyButton = window->findChild<QQuickItem *>(
            QStringLiteral("historyToolbarButton"));
        QVERIFY2(historyButton,
                 "historyToolbarButton not found by objectName");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          historyButton->mapToScene(QPointF(
                              historyButton->width() / 2.0,
                              historyButton->height() / 2.0)).toPoint());
        QObject *const popup = window->findChild<QObject *>(
            QStringLiteral("historyPopup"));
        QVERIFY2(popup, "historyPopup not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(popup->property("visible").toBool(),
                                  "the history popup never opened",
                                  4000);

        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("saveVersionButton"));
        QVERIFY2(button, "saveVersionButton not found by objectName");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(
                              button->width() / 2.0,
                              button->height() / 2.0)).toPoint());

        // 快照列表在浮窗里：count 经 QML 的 rows 属性（JS 数组模型）。
        QQuickItem *const list = window->findChild<QQuickItem *>(
            QStringLiteral("snapshotList"));
        QVERIFY2(list, "snapshotList not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(
            list->property("count").toInt() >= 1,
            "the snapshot list never showed the saved version",
            4000);

        // 改掉正文再回滚：回到「version one body」。
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
        typeText(window, QStringLiteral(" CHANGED"));
        QVERIFY(editor->property("text").toString()
                    .contains(QStringLiteral("CHANGED")));

        const QVariantList snapshots = tools->snapshots(
            tools->property("activeDraftId").toString());
        QVERIFY(!snapshots.isEmpty());
        const double createdAt = snapshots.at(0).toMap()
                .value(QStringLiteral("createdAt")).toDouble();
        QVERIFY(QMetaObject::invokeMethod(
            pageRoot, "rollbackDraftVersion",
            Q_ARG(QVariant, QVariant(createdAt))));

        // 回滚确认弹窗出现，真实点 Roll back。
        QObject *const dialog = window->findChild<QObject *>(
            QStringLiteral("rollbackConfirm"));
        QVERIFY2(dialog, "rollbackConfirm not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(dialog->property("opened").toBool(),
                                  "rollback did not open the confirm dialog",
                                  4000);
        // 找 AButton 本体（带 variant 属性的才是按钮）：弹窗标题 Label
        // 的 text 同样是 "Roll back"，只按文字匹配会点到标题上——
        // 按钮找成标题，点击落在标题区域，doRollback 永远不触发。
        QQuickItem *rollbackButton = nullptr;
        const auto children = dialog->findChildren<QQuickItem *>();
        for (QQuickItem *child : children) {
            if (child->property("text").toString()
                        == QStringLiteral("Roll back")
                    && child->property("variant").isValid()) {
                rollbackButton = child;
                break;
            }
        }
        QVERIFY2(rollbackButton, "Roll back button not found");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          rollbackButton->mapToScene(QPointF(
                              rollbackButton->width() / 2.0,
                              rollbackButton->height() / 2.0)).toPoint());
        QTRY_VERIFY2_WITH_TIMEOUT(
            editor->property("text").toString()
                .contains(QStringLiteral("version one body"))
                && !editor->property("text").toString()
                         .contains(QStringLiteral("CHANGED")),
            "rollback did not restore the saved version",
            4000);
    }

private:
    // 模板列表/收藏列表的第一行视觉行（ObjectModel 驱动的 ListView，
    // 行是模型子项：从列表的可视子项容器里下钻）。
    static QQuickItem *firstVisualRow(QObject *list)
    {
        QQuickItem *const view = qobject_cast<QQuickItem *>(list);
        if (!view) {
            return nullptr;
        }
        const auto containers = view->childItems();
        for (QQuickItem *container : containers) {
            const auto rows = container->childItems();
            for (QQuickItem *row : rows) {
                if (row->height() > 5) {
                    return row;
                }
            }
        }
        return nullptr;
    }

    // 模板列表行（与 firstVisualRow 同一形态；名字独立以便将来分化）。
    static QQuickItem *firstTemplateRow(QObject *list)
    {
        return firstVisualRow(list);
    }

private:
    // 文件树那一行的右键菜单。找它不能靠 window->findChild：ListView 的行
    // delegate 的 QObject 父项是视图内部的复用器，不在页面那条 QObject 链上
    // （从页面递归找不到），只能顺可视树走到行，再从行的 QObject 子项里取
    // ——菜单本身不是可视子项，它是那一行的普通 QObject 子项。
    static QObject *rowMenu(QQuickItem *tree)
    {
        const auto containers = tree->childItems();
        for (QQuickItem *container : containers) {
            const auto rows = container->childItems();
            for (QQuickItem *row : rows) {
                QObject *const menu = row->findChild<QObject *>(
                    QStringLiteral("treeRowMenu"));
                if (menu) {
                    return menu;
                }
            }
        }
        return nullptr;
    }

    // 造一个只含 note.md 的工作区，右键那一行让菜单弹出，按 expectPreview
    // 断言「Preview Markdown」条目的可用态（可见但按需禁用）；可用时再真实
    // 点一次，断言交出去的是这一行的绝对路径。两个用例共用它，只在装配前
    // 换一个开关。
    void runRowMenuCase(QQuickWindow *window,
                        ToolsFacade *tools, bool expectPreview)
    {
        const QString root = m_dataRoot->path() + QStringLiteral("/menuws");
        QVERIFY(QDir().mkpath(root));
        const QString docPath = QDir(root).filePath(QStringLiteral("note.md"));
        QFile doc(docPath);
        QVERIFY(doc.open(QIODevice::WriteOnly));
        doc.write("# hi\n");
        doc.close();
        QVERIFY(tools->addWorkspace(root).ok);

        QQuickItem *const tree = window->findChild<QQuickItem *>(
            QStringLiteral("fileTree"));
        QVERIFY(tree);
        QTRY_VERIFY2(tree->width() > 10 && tree->height() > 10,
                     "file tree never got a size from the layout");
        QTRY_COMPARE(tree->property("count").toInt(), 1);

        // 等到行真的建出来：模型有行数（count）与 delegate 实例是两件事，
        // 后者要等一轮 polish。菜单建不出来时这里会带着原因失败，而不是
        // 让后面的查找以「找不到条目」的假面目出现。
        QTRY_VERIFY2_WITH_TIMEOUT(rowMenu(tree) != nullptr,
                                  "the file tree never rendered a row delegate",
                                  4000);

        // 右键行中心：delegate 里那只吃右键的 MouseArea → pathMenu.popup。
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                          tree->mapToScene(QPointF(30, 12)).toPoint());
        QObject *const menu = rowMenu(tree);
        QVERIFY2(menu, "treeRowMenu not found by objectName");
        QTRY_VERIFY2(menu->property("opened").toBool(),
                     "right-click did not open the tree row menu");

        QObject *previewItem = nullptr;
        const auto items = menu->findChildren<QObject *>();
        for (QObject *child : items) {
            if (child->property("text").toString()
                    == QStringLiteral("Preview Markdown")) {
                previewItem = child;
                break;
            }
        }
        // 条目恒可见（禁用态由 AMenuItem 自绘），两种装配的差别就是
        // enabled 这一个布尔——「此处不可用」以禁用呈现，不再整条隐藏。
        QVERIFY2(previewItem, "Preview Markdown item not found in treeRowMenu");
        QVERIFY2(previewItem->property("visible").toBool(),
                 "Preview Markdown item must stay visible, disabled instead");
        QCOMPARE(previewItem->property("enabled").toBool(), expectPreview);

        // 问过 workbench 的正是这一行的路径：后缀判断留在纯层，QML 只问结果。
        const QString rowPath = tools->model()
                                    ->index(0, 0)
                                    .data(FileTreeModel::PathRole)
                                    .toString();
        QVERIFY(!rowPath.isEmpty());
        QVERIFY2(m_workbench->askedAbout.contains(rowPath),
                 qPrintable(QStringLiteral("canPreviewMarkdown was asked about ")
                            + m_workbench->askedAbout.join(QLatin1Char(';'))));

        if (!expectPreview) {
            QVERIFY(m_workbench->previewed.isEmpty());
            QTest::keyClick(window, Qt::Key_Escape);
            return;
        }

        // 真实点击条目：onTriggered 应当把同一个绝对路径交给 previewMarkdown
        // （交相对路径的话预览域无从定位文件，交错行则打开别的文档）。
        QQuickItem *const row = qobject_cast<QQuickItem *>(previewItem);
        QVERIFY2(row, "Preview Markdown item is not a QQuickItem");
        const QPointF center = row->mapToScene(
            QPointF(row->width() / 2.0, row->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        QVERIFY2(m_workbench->previewed.contains(rowPath),
                 qPrintable(QStringLiteral("previewMarkdown received ")
                            + m_workbench->previewed.join(QLatin1Char(';'))
                            + QStringLiteral(" instead of ") + rowPath));
        QTest::keyClick(window, Qt::Key_Escape);
    }

    // 装配成员按依赖顺序声明、在 loadPage/cleanup 里成对管理。
    bool m_previewCapable = false;
    std::unique_ptr<QTemporaryDir> m_dataRoot;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<ThemeRegistry> m_registry;
    std::unique_ptr<Theme> m_theme;
    std::unique_ptr<ToolsFacade> m_tools;
    std::unique_ptr<MarkdownEdit> m_markdownEdit;
    std::unique_ptr<MarkdownPreviewController> m_markdown;
    std::unique_ptr<WorkbenchStub> m_workbench;
    std::unique_ptr<UiStub> m_ui;
    std::unique_ptr<NavStub> m_nav;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQmlContext> m_context;
    std::unique_ptr<QQuickView> m_view;
};

// QQuickWindow 交互需要 QGuiApplication（共享注册表的 QCoreApplication
// 驱动不了键盘/鼠标投递），所以本套件自带 main。
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    TestToolsUi tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_toolsui.moc"
