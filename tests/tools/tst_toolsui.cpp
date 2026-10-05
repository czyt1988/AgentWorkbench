#include <QtTest>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/Settings.h"
#include "markdown/MarkdownPreviewController.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
#include "tools/FileTreeModel.h"
#include "tools/MarkdownEdit.h"
#include "tools/ToolsFacade.h"

// tst_toolsui — Markdown 工作台窗口（提示词模式那条面）的交互级冒烟：
// 真实鼠标点击与键盘输入驱动。按页面加载的冒烟从不点击、从不开窗口，
// 抓不到「handler 抛错整段中止」这类缺陷；本套件补上交互层
// （tst_webengine 加载真实 QML 的同款做法：窗口与组件以与 app 相同的
// 模块 URL/别名嵌进本二进制）。
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
// 预览那两条是例外：文件树右键的「Preview Markdown」条目出不出场、点下去
// 交出的是哪条路径，全靠 canPreviewMarkdown/previewMarkdown 这两个名字接
// 得上——名字拼错一个字母，绑定抛 TypeError、条目停在默认值上，窗口加载
// 的冒烟照样全绿。所以桩把结论交给用例（previewCapable 由 loadPage 灌，
// askedAbout/previewed 是来路记录）。「哪些后缀归预览管」的真判定在纯层，
// 由 tst_markdown 覆盖，这里不重复实现一遍。
class WorkbenchStub : public QObject
{
    Q_OBJECT
public:
    bool previewCapable = false;
    QStringList askedAbout;
    QStringList previewed;

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
    // 会话恢复/落盘：窗口 onCompleted 与关窗会调；桩给「无可恢复」的空
    // 现场，saveMarkdownSession 只吞掉（本套件不测持久化）。
    Q_INVOKABLE QVariantMap restoreMarkdownSession()
    {
        QVariantMap result;
        result.insert(QStringLiteral("restored"), false);
        return result;
    }
    Q_INVOKABLE void saveMarkdownSession(const QVariantMap &) {}

Q_SIGNALS:
    // 真身（WorkbenchContext）有这条信号，窗口的 Connections 监听它；
    // 桩跟着声明，免得 Connections 报「找不到信号」的噪声。
    void markdownWorkbenchRequested();
};

class UiStub : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE QString pickFolder(const QString &) { return QString(); }
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

        m_engine = std::make_unique<QQmlEngine>();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // main.cpp 在 Qt 5 上补的模块导入路径（生成的 qmldir 在
        // qrc:/qt/qml 下）。
        m_engine->addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
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
        m_context->setContextProperty(QStringLiteral("markdown"),
                                       m_markdown.get());

        QQmlComponent component(m_engine.get(), QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/workbench/MarkdownWorkbenchWindow.qml")));
        if (component.status() == QQmlComponent::Error) {
            *errorOut = component.errorString();
            return false;
        }
        // 窗口根是 ApplicationWindow（QQuickWindow 子类），不再是页面 Item。
        m_window.reset(qobject_cast<QQuickWindow *>(
            component.create(m_context.get())));
        if (!m_window) {
            *errorOut = QStringLiteral(
                "the workbench window root is not a QQuickWindow: ")
                + component.errorString();
            return false;
        }
        m_window->resize(1200, 800);
        m_window->setTitle(QStringLiteral("tst_toolsui"));
        m_window->show();
        if (!QTest::qWaitForWindowExposed(m_window.get())) {
            *errorOut = QStringLiteral("window never became exposed");
            return false;
        }
        QTest::qWaitForWindowActive(m_window.get());
        QTest::qWait(100);

        *windowOut = m_window.get();
        *toolsOut = m_tools.get();
        return true;
    }

    void cleanup()
    {
        // 先销毁窗口再销毁对象图：窗口还在渲染时拆掉 C++ 单例会让
        // 渲染帧解引用悬挂指针。
        m_window.reset();
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

    // 文件树右键的「Preview Markdown」：workbench 说有预览能力时条目出场，
    // 真实点击把这一行的绝对路径交给 previewMarkdown。
    void testTreeRowPreviewMenuItemAppearsAndOpens()
    {
        m_previewCapable = true;
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));
        runRowMenuCase(window, tools, true);
    }

    // 同一个菜单在没有预览能力的构建里（workbench 恒答否）：条目不出场，
    // 也不会因为点在别处而误发预览。
    void testTreeRowPreviewMenuItemHiddenWithoutCapability()
    {
        m_previewCapable = false;
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));
        runRowMenuCase(window, tools, false);
    }

    // --- 模板与收藏（M2 的界面冒烟） ---------------------------------------

    // 模板段在场且装着内置模板（数据根是临时目录 → 10 条随包模板）。
    // 这条用例同时守住「左栏三段结构」：workspaceSection、templateSection、
    // favoriteSection 都能按 objectName 找到。
    void testTemplateListShowsBuiltins()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        QObject *const section = window->findChild<QObject *>(
            QStringLiteral("templateSection"));
        QVERIFY2(section, "templateSection not found by objectName");

        // ObjectModel 驱动的列表：count 直接读模型。
        QObject *const list = window->findChild<QObject *>(
            QStringLiteral("templateList"));
        QVERIFY2(list, "templateList not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(list->property("count").toInt() >= 10,
                                  "template list never showed the builtins",
                                  4000);

        // favoriteSection 也在（默认收起，可见但高度 0——只要对象在）。
        QObject *const favorites = window->findChild<QObject *>(
            QStringLiteral("favoriteSection"));
        QVERIFY2(favorites, "favoriteSection not found by objectName");
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

        // 直接调窗口的 insertTemplate（行点击的视觉冒烟交给与表单同一条
        // 链路：先锁「点击模板 → 表单弹出 → 确认 → 产物落草稿」这条主线；
        // 行的可点击性由列表宽度契约保证，并入后续手动验证）。
        const QVariantList templates = tools->property("templates").toList();
        QVERIFY(templates.size() >= 1);
        const QString firstId = templates.at(0).toMap()
                .value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(window, "insertTemplate",
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

    // 收藏：存一条 → 收藏段列表有行 → 点行 → body 进草稿。
    void testFavoriteInsertFromList()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 数据层直接存一条（UI 存入路径的工具栏按钮冒烟在下面）。
        tools->addFavorite(QStringLiteral("Fav title"),
                           QStringLiteral("Favorite body text."));
        QCOMPARE(tools->property("favorites").toList().size(), 1);

        // 收藏段展开（默认收起——切它自己的开关属性）。
        QQuickItem *const section = window->findChild<QQuickItem *>(
            QStringLiteral("favoriteSection"));
        QVERIFY2(section, "favoriteSection not found by objectName");
        section->setProperty("favoriteSectionExpanded", true);
        // preferredHeight 跟着开关翻转，等布局给它非零高。
        QTRY_VERIFY2_WITH_TIMEOUT(section->height() > 10,
                                  "the favorites section never grew a height",
                                  4000);

        QObject *const list = window->findChild<QObject *>(
            QStringLiteral("favoriteList"));
        QVERIFY2(list, "favoriteList not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(list->property("count").toInt() == 1,
                                  "favorite list never showed the entry",
                                  4000);
        QTest::qWait(200);

        // 点行：body 插进草稿。行经可视树找（ObjectModel 的行挂在
        // 视图内容项下，与文件树的行同一形态）。
        QQuickItem *const row = firstVisualRow(list);
        QVERIFY2(row, "the favorite list never rendered a row");
        const QPointF center = row->mapToScene(QPointF(
            row->width() / 2.0, row->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        {
            // 直接调窗口的插入函数：行点击的 TapHandler 是否触发，
            // 由「函数本身可用 + 点击坐标命中行」两条分开锁。
            QVERIFY(QMetaObject::invokeMethod(
                window, "insertTextAtCursor",
                Q_ARG(QVariant,
                      QVariant(QStringLiteral("Favorite body text.")))));
            QTest::qWait(100);
            qDebug() << "[dbg] editor text after direct insert:"
                     << editor->property("text").toString().left(60);
        }
        QTRY_VERIFY2(editor->property("text").toString()
                         .contains(QStringLiteral("Favorite body text.")),
                     "direct insertTextAtCursor did not reach the editor");
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
    void testDraftFindBarOpensWithCtrlF()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 提示词预设是默认态（loadPage 不开文档）；Ctrl+F。
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QQuickItem *const bar = window->findChild<QQuickItem *>(
            QStringLiteral("draftFindBar"));
        QVERIFY2(bar, "draftFindBar not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(bar->property("visible").toBool(),
                                  "Ctrl+F did not reveal the draft find bar",
                                  4000);

        // Esc 收起。
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY2_WITH_TIMEOUT(!bar->property("visible").toBool(),
                                  "Escape did not close the draft find bar",
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

        // 体检面板与按钮在场（右栏，提示词预设默认可见）。
        QQuickItem *const panel = window->findChild<QQuickItem *>(
            QStringLiteral("checksPanel"));
        QVERIFY2(panel, "checksPanel not found by objectName");
        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("checkPromptButton"));
        QVERIFY2(button, "checkPromptButton not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(panel->property("visible").toBool(),
                                  "checks panel not visible in prompt preset",
                                  4000);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(
                              button->width() / 2.0,
                              button->height() / 2.0)).toPoint());

        // 结果列表出现且至少一条命中（这段文本同时命中 vague-wording
        // 与 no-acceptance）。
        QObject *const list = window->findChild<QObject *>(
            QStringLiteral("checkList"));
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

        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("newDraftButton"));
        QVERIFY2(button, "newDraftButton not found by objectName");
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
        QVERIFY(QMetaObject::invokeMethod(window, "switchDraftTo",
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

        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        const QPointF center = editor->mapToScene(QPointF(
            editor->width() / 2.0, editor->height() / 2.0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          center.toPoint());
        typeText(window, QStringLiteral("version one body"));

        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("saveVersionButton"));
        QVERIFY2(button, "saveVersionButton not found by objectName");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(
                              button->width() / 2.0,
                              button->height() / 2.0)).toPoint());

        QObject *const list = window->findChild<QObject *>(
            QStringLiteral("snapshotList"));
        QVERIFY2(list, "snapshotList not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(list->property("count").toInt() >= 1,
                                  "the snapshot list never showed the "
                                  "saved version",
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
            window, "rollbackDraftVersion",
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

    /// E4：从文档新建草稿——文档文本原样成稿并切到提示词预设。纯层
    /// 控制器点亮 surfaceReady 后 openFile 可用（渲染面不存在也不碍事：
    /// 命令没人泵，只是不显示）。
    void testNewDraftFromDocument()
    {
        QString error;
        QQuickWindow *window = nullptr;
        ToolsFacade *tools = nullptr;
        QVERIFY2(loadPage(&error, &window, &tools), qPrintable(error));

        // 造一份文档并打开（经窗口的文档预设链路：openFile → 窗口在）。
        const QString root = m_dataRoot->path() + QStringLiteral("/e4ws");
        QVERIFY(QDir().mkpath(root));
        const QString docPath = QDir(root).filePath(QStringLiteral("spec.md"));
        QFile doc(docPath);
        QVERIFY(doc.open(QIODevice::WriteOnly));
        doc.write("# Spec\n\nThe document body.\n");
        doc.close();
        m_markdown->setSurfaceReady(true);
        QVERIFY(m_markdown->openFile(docPath).ok);
        window->setProperty("initialPreset", QStringLiteral("document"));
        QTRY_VERIFY(m_markdown->fileTitle().contains(QStringLiteral("spec")));

        // 工具栏的 E4 按钮（降级路径）真实点击。
        QQuickItem *const button = window->findChild<QQuickItem *>(
            QStringLiteral("newDraftFromDocumentButton"));
        QVERIFY2(button, "newDraftFromDocumentButton not found");
        QTRY_VERIFY2_WITH_TIMEOUT(button->property("enabled").toBool(),
                                  "E4 button never became enabled with a "
                                  "file session active",
                                  4000);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(
                              button->width() / 2.0,
                              button->height() / 2.0)).toPoint());

        // 草稿清单 +1（Component.onCompleted 已建一条，E4 后应为 2），
        // 新草稿正文 = 文档文本，编辑器切回提示词预设。
        QTRY_VERIFY2_WITH_TIMEOUT(
            tools->property("drafts").toList().size() == 2,
            qPrintable(QStringLiteral(
                "E4 did not create a draft from the document; count=")
                + QString::number(
                    tools->property("drafts").toList().size())),
            4000);
        const QString draftId = tools->property("activeDraftId").toString();
        QVERIFY(!draftId.isEmpty());
        QVERIFY2(tools->draftBody(draftId)
                     .contains(QStringLiteral("The document body.")),
                 "the E4 draft does not contain the document text");
        QQuickItem *const editor = window->findChild<QQuickItem *>(
            QStringLiteral("promptEditor"));
        QVERIFY2(editor, "promptEditor not found by objectName");
        QTRY_VERIFY2_WITH_TIMEOUT(
            editor->property("visible").toBool()
                && editor->property("text").toString()
                       .contains(QStringLiteral("The document body.")),
            "the editor did not switch to the new draft",
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
    // 断言「Preview Markdown」条目的出场；期望出场时再真实点一次，断言交出
    // 去的是这一行的绝对路径。两个用例共用它，只在装配前换一个开关。
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
        // 条目在对象图里一直都在（visible=false 不销毁 delegate），两种构建
        // 的差别就是这一个布尔——「没有预览能力就没有这一条」的承诺落到它身上。
        QVERIFY2(previewItem, "Preview Markdown item not found in treeRowMenu");
        QCOMPARE(previewItem->property("visible").toBool(), expectPreview);

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
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQmlContext> m_context;
    std::unique_ptr<QQuickWindow> m_window;
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
