// tst_markdownpreviewui — Markdown 工作台页（文档模式那条面）那份 QML 的
// 真实加载与往返。
//
// 这个目标存在的理由：MarkdownWorkbenchPage.qml 是「页面」那一层的代码，它
// 同时引用主题令牌、A* 组件、C++ 单例（含 tools 域）与经 Loader 惰性装载的
// WebEngine 预览面，而这四样在纯层测试里都不存在。tst_markdown 证明控制器算
// 得出块与命令，tst_markdownpreview 证明页面渲染得对，两者都碰不到这一段：
//   1. 文件本身能不能编译——引用不存在的组件属性、跨 Qt 版本不存在的信号名，
//      在这里会以 QQmlComponent 的加载错误当场暴露（这类错误在生产里表现为
//      「右键点了没反应」，页面冒烟从不点击，抓不到）；
//   2. 命令泵是不是真的在跑——pump() 只在页面里存在，而 find / serialize 的
//      往返必须穿过它才谈得上「导出有了文件」「搜索报出命中数」；
//   3. 页面与控制器之间的报到契约——notifyWindowOpened 没在 onCompleted 里
//      调到，控制器就永远不排命令（下一次预览什么也不会发生）。
//
// 与 tst_markdownpreview 的分工：那边用内联 QML 造一个裸视图直接问页面（页面层），
// 这边加载**产品那份 .qml**（窗口层）。因此本目标要把页面引用的组件、图标与
// 运行时资源按 app/CMakeLists.txt 的同款别名嵌进来——嵌错了测的就是另一份代码。
//
// 只在 Qt 6 + WebEngine 的构建里存在（tests/CMakeLists.txt 的条件）。
//
// workbench 与 ui 用替身：真实实现要把整个 app 装配进来（nav、shell、
// notifications、agents、webTabs），而页面只用到它们的几个动作。替身把用户
// 可见的后果（toast、剪贴板、外链、reveal）留在 C++ 侧可断言，同时不让本目标
// 依赖 UI 框架层。小写别名经上下文属性提供——测试工具的做法，app 侧禁用
// setContextProperty（见 tst_toolsui 的同款说明）。

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

#include "core/OpResult.h"
#include "core/Settings.h"
#include "markdown/MarkdownPreviewController.h"
#include "markdown/webengine/MarkdownPreviewProvider.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"
#include "markdownedit/MarkdownEdit.h"
#include "tools/ToolsFacade.h"
#include "web/webengine/WebEngineCompat.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtWebEngineQuick>
#else
#include <QtWebEngine>
#endif

using awb::core::Settings;
using awb::markdown::MarkdownPreviewController;
using awb::markdown::MarkdownPreviewProvider;
using awb::theme::Theme;
using awb::theme::ThemeRegistry;
using awb::web::WebEngineCompat;

namespace {

// 页面冷启动的上限：起渲染进程、取 doc.json、跑完模块图，Debug 二进制 + 冷
// Chromium 在慢机上到几十秒（与 tst_markdownpreview 同一口径）。
constexpr int kBootTimeoutMs = 40000;

// 一份够小的文档：一个标题（导出的判据）、一个只在正文里出现的词（搜索的判据）、
// 一个任务项（走自装的 markdown-it 规则，正文没挂上时搜索不会命中）。
const char *kFixture = R"MD(# Preview Probe

The distinct needle word lives in this paragraph.

- [x] a checked task
)MD";

// 与 Workspace.qml 同构的宿主：keepAlive Loader 在第一次 windowRequested
// 时实例化工作台页，此后常驻——与产品那条链一致（产品里导航到 tools 页
// 或右键预览都会把「来过一次」置真）。
//
// 这份内联 QML 存在的理由是：产品里工作台页从来不是直接 createObject 出来
// 的，而是被工作区里的 Loader 创建——keepAlive 页以 0x0 创建、变可见后才拿到
// 真实尺寸，布局走的是另一条路。只用 createObject 的用例看不见这条路上的
// 缺陷（内容区塌成 0 高），所以这里连宿主一起复刻，让被测的时序与真实入口
// 一致。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
static const char kHostImports[] =
    "import QtQuick\nimport QtQuick.Controls\n";
#else
static const char kHostImports[] =
    "import QtQuick 2.15\nimport QtQuick.Controls 2.15\n";
#endif
const char *kHostQml = R"QML(

ApplicationWindow {
    id: host
    width: 900
    height: 600
    visible: true

    Loader {
        id: workbenchPageLoader
        objectName: "workbenchPageLoader"
        anchors.fill: parent
        active: visited
        property bool visited: false
        source: "qrc:/qt/qml/AgentWorkbench/workbench/MarkdownWorkbenchPage.qml"

        Connections {
            target: markdown

            // 预览请求：页面第一次被需要时实例化（产品里由导航触发，
            // 这里用信号直接驱动，装配面更小而时序相同）。
            function onWindowRequested() {
                workbenchPageLoader.visited = true
            }
        }
    }
}
)QML";

/**
 * @brief workbench 的替身：记下窗口发出的每一次用户可见动作
 *
 * 方法名与形参形状要和 WorkbenchContext 的真身一致，否则 QML 里的调用会在
 * 这里以「is not a function」失败——那正是本套件要拦住的缺陷类别，所以宁可
 * 在这里报错，也不要在断言里放宽。
 */
class WorkbenchStub : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE void notify(const QString &level, const QString &title,
                            const QString &text)
    {
        notices.append(level + QLatin1Char('|') + title + QLatin1Char('|') + text);
    }
    Q_INVOKABLE void copyText(const QString &text)
    {
        copied = text;
    }
    Q_INVOKABLE void openExternalUrl(const QUrl &url)
    {
        external = url;
    }
    Q_INVOKABLE void openLocalFileExternal(const QString &path)
    {
        external = QUrl::fromLocalFile(path);
    }
    // 文件树右键菜单的可见性绑定会问这两个名字（树在窗口里常驻装配）。
    Q_INVOKABLE bool canPreviewMarkdown(const QString &)
    {
        return false;
    }
    Q_INVOKABLE void previewMarkdown(const QString &) {}
    // 会话恢复/落盘：页面 onCompleted 会调；桩给「无可恢复」的空
    // 现场，save 只吞掉（持久化由 workbench 域自己的用例覆盖）。
    Q_INVOKABLE QVariantMap restoreMarkdownSession()
    {
        QVariantMap result;
        result.insert(QStringLiteral("restored"), false);
        return result;
    }
    Q_INVOKABLE void saveMarkdownSession(const QVariantMap &) {}

public:
    QStringList notices;    ///< 每次 notify 的 level|title|text
    QString copied;         ///< 最后一次复制的文本
    QUrl external;          ///< 最后一次交给外部浏览器的地址
};

// nav 桩：工作台页是 keepAlive 页，快捷键与回页复查门控在读
// nav.currentPageId——桩恒答「当前页就是工作台页」。
class NavStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPageId READ currentPageId CONSTANT)
public:
    QString currentPageId() const { return QStringLiteral("tools"); }
};

/**
 * @brief ui 的替身：目录对话框与「在文件管理器里显示」都只记账
 */
class UiStub : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QString pickFolder(const QString &title)
    {
        folderTitle = title;
        return folder;
    }
    Q_INVOKABLE void revealFile(const QString &path)
    {
        revealed.append(path);
    }

    QString folder;         ///< 假装用户选中的目录
    QString folderTitle;    ///< 对话框标题（证明用户确实被问过）
    QStringList revealed;   ///< 被 reveal 过的路径
};

} // namespace

// tab 点击用例的消息钩子：ReferenceError 只以 qWarning 的形式存在，对象图
// 一切正常——不装钩子就静默漏过（tst_ribbonui 的同款理由）。
QStringList g_pageWarnings;
QtMessageHandler g_previousMessageHandler = nullptr;

void pageMessageHandler(QtMsgType type, const QMessageLogContext &context,
                        const QString &message)
{
    if ((type == QtWarningMsg || type == QtCriticalMsg)
        && (message.contains(QLatin1String("ReferenceError"))
            || message.contains(QLatin1String("TypeError"))
            || message.contains(QLatin1String("is not a function")))) {
        g_pageWarnings.append(message);
    }
    if (g_previousMessageHandler) {
        g_previousMessageHandler(type, context, message);
    }
}

// 视觉树递归查找（objectName 匹配）：Repeater 的 delegate 只有视觉父
//（QObject 父为 null），findChild 的 QObject 树搜不到——tab 条的 fileTab
// 一律走这里（tst_ribbonui 的同款做法）。
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

// 同名全部收齐：tab 条的每个 delegate 都叫 fileTab（名字只标类别不标序号），
// 草稿与文件 tab 靠 modelData.draft 区分。
void collectItemsByName(QQuickItem *root, const QString &name,
                        QList<QQuickItem *> *out)
{
    if (!root) {
        return;
    }
    if (root->objectName() == name) {
        out->append(root);
    }
    for (QQuickItem *child : root->childItems()) {
        collectItemsByName(child, name, out);
    }
}

// 当前在场的「文件」tab（modelData.draft 为假那个）；没有文件 tab 时为空。
QQuickItem *fileTabItem(QQuickItem *root)
{
    QList<QQuickItem *> tabs;
    collectItemsByName(root, QStringLiteral("fileTab"), &tabs);
    for (QQuickItem *tab : tabs) {
        if (!tab->property("modelData").toMap()
                 .value(QStringLiteral("draft")).toBool()) {
            return tab;
        }
    }
    return nullptr;
}

/**
 * @brief 一个用例一套完整现场：控制器 + 提供者 + 引擎 + 产品那份页面
 *
 * 现场对象由 init()/cleanup() 成对建拆（见那两个函数的注释）：页面挂着
 * WebEngineView，必须最先销毁，然后是上下文与引擎，最后才是 provider 与它
 * 引用到的控制器。
 */
class TestMarkdownPreviewUi : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testPageLoadsAndTakesOwnership();
    void testLoaderHostedPageFillsAndStaysResident();
    void testSearchRoundTripFindsTheRenderedText();
    void testExportWritesTheRenderedMarkup();
    void testFileTabClickSwitchesPresetWithoutScopeError();
    void testOutlineFloatCollapsesAndResizes();

private:
    /**
     * @brief 把夹具写进本用例的临时目录
     * @param error 输出：失败原因
     * @return 文档路径；写失败时为空
     */
    QString writeFixture(QString *error)
    {
        const QString path = QDir(m_dir->path())
                                 .filePath(QStringLiteral("note.md"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            *error = QStringLiteral("cannot write the fixture: ") + path;
            return QString();
        }
        file.write(kFixture);
        file.close();
        return path;
    }

    /**
     * @brief 装配现场并加载产品那份 MarkdownWorkbenchPage.qml
     * @param error 输出：失败原因（含 QML 的完整错误串）
     * @return 文档已打开且页面已装进宿主窗口则为真
     *
     * 先 openFile 再建页面，与真实入口同序（文件树右键 →
     * workbench.previewMarkdown → 控制器装载会话 → 导航到工作台页，
     * Loader 这才实例化）。页面 onCompleted 按控制器里的活跃文件会话
     * 推断出文档预设——顺序反了页面会在「还没有文档」的状态下落成提示词
     * 预设。
     */
    bool load(QString *error)
    {
        m_theme.applyTheme(QStringLiteral("mocha-dark"));
        if (!m_controller->available()) {
            *error = QStringLiteral("surface unavailable: ")
                     + m_controller->unavailableReason();
            return false;
        }
        const QString path = writeFixture(error);
        if (path.isEmpty()) {
            return false;
        }

        m_windowRequested.reset(new QSignalSpy(m_controller.get(),
            &MarkdownPreviewController::windowRequested));
        const awb::core::OpResult opened = m_controller->openFile(path);
        if (!opened) {
            *error = opened.error;
            return false;
        }

        m_context.reset(new QQmlContext(m_engine->rootContext()));
        m_context->setContextProperty(QStringLiteral("theme"), &m_theme);
        m_context->setContextProperty(QStringLiteral("markdown"), m_controller.get());
        m_context->setContextProperty(QStringLiteral("workbench"), &m_workbench);
        m_context->setContextProperty(QStringLiteral("ui"), &m_ui);
        m_context->setContextProperty(QStringLiteral("nav"), &m_nav);
        m_context->setContextProperty(QStringLiteral("tools"), m_tools.get());

        QQmlComponent component(m_engine.get());
        component.loadUrl(QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/workbench/MarkdownWorkbenchPage.qml")));
        if (component.isError()) {
            *error = component.errorString();
            return false;
        }
        // 页面根是普通 Item：装进 QQuickView 宿主（页面在产品里住在主窗
        // 工作区，这里用宿主窗口复刻那个容器）。
        std::unique_ptr<QQuickItem> page(
            qobject_cast<QQuickItem *>(component.create(m_context.get())));
        if (!page) {
            *error = QStringLiteral("the workbench page root is not a QQuickItem: ")
                     + component.errorString();
            return false;
        }
        m_page = page.get();
        m_view.reset(new QQuickView(m_engine.get(), nullptr));
        m_view->setResizeMode(QQuickView::SizeRootObjectToView);
        m_view->setContent(QUrl(), nullptr, page.release());
        m_view->resize(900, 600);
        m_view->show();
        if (!QTest::qWaitForWindowExposed(m_view.get())) {
            *error = QStringLiteral("the host window never became exposed");
            return false;
        }
        m_previewView = m_view->findChild<QObject *>(
            QStringLiteral("markdownPreviewView"));
        if (!m_previewView) {
            *error = QStringLiteral("no WebEngineView in the loaded page");
            return false;
        }
        return true;
    }

    /**
     * @brief 建出与工作区同构的宿主，并按真实信号时序第一次实例化页面
     * @param error 输出：失败原因
     * @return 宿主已建、文档已交给控制器则为真
     *
     * 与 load() 的区别只在「谁创建页面」：这里由宿主的 keepAlive Loader
     * 在收到 windowRequested 之后创建，产品路径就是这一条（0x0 创建、
     * 变可见后才拿到真实尺寸的布局路径也只有这条能测到）。
     */
    bool loadHost(QString *error)
    {
        m_theme.applyTheme(QStringLiteral("mocha-dark"));
        if (!m_controller->available()) {
            *error = QStringLiteral("surface unavailable: ")
                     + m_controller->unavailableReason();
            return false;
        }
        m_docPath = writeFixture(error);
        if (m_docPath.isEmpty()) {
            return false;
        }

        m_context.reset(new QQmlContext(m_engine->rootContext()));
        m_context->setContextProperty(QStringLiteral("theme"), &m_theme);
        m_context->setContextProperty(QStringLiteral("markdown"),
                                      m_controller.get());
        m_context->setContextProperty(QStringLiteral("workbench"), &m_workbench);
        m_context->setContextProperty(QStringLiteral("ui"), &m_ui);
        m_context->setContextProperty(QStringLiteral("nav"), &m_nav);
        m_context->setContextProperty(QStringLiteral("tools"), m_tools.get());

        QQmlComponent component(m_engine.get());
        component.setData(
            QByteArray(kHostImports) + QByteArray(kHostQml), QUrl());
        if (component.isError()) {
            *error = component.errorString();
            return false;
        }
        QObject *created = component.create(m_context.get());
        if (!created) {
            *error = component.errorString();
            return false;
        }
        m_host.reset(qobject_cast<QQuickWindow *>(created));
        if (!m_host) {
            *error = QStringLiteral("the host root is not a window: ")
                     + QString::fromLatin1(created->metaObject()->className());
            delete created;
            return false;
        }
        m_loader = m_host->findChild<QObject *>(
            QStringLiteral("workbenchPageLoader"));
        if (!m_loader) {
            *error = QStringLiteral("the host has no workbench Loader");
            return false;
        }
        m_windowRequested.reset(new QSignalSpy(m_controller.get(),
            &MarkdownPreviewController::windowRequested));
        const awb::core::OpResult opened = m_controller->openFile(m_docPath);
        if (!opened) {
            *error = opened.error;
            return false;
        }
        return true;
    }

    /**
     * @brief 取宿主 Loader 当前创建出来的那份工作台页
     * @return 页面对象；Loader 还没激活（没有内容项）时为空
     */
    QPointer<QObject> hostedPage() const
    {
        if (!m_loader) {
            return nullptr;
        }
        return m_loader->property("item").value<QObject *>();
    }

    /**
     * @brief 等预览视图拿到与宿主窗口相称的高度
     * @param window 承载页面的宿主窗口
     * @param reason 输出：失败时视图与窗口的实测尺寸
     * @return 视图高度不为 0 且至少占窗口高度一半则为真
     *
     * 判据是「内容区看得见」而不是「视图对象存在」：产品里的缺陷表现是只看到
     * 工具栏，即布局把 fillHeight 的那一栏挤成 0 高，而对象一个不少。
     */
    bool waitContentHeight(QObject *window, QString *reason)
    {
        QElapsedTimer clock;
        clock.start();
        qreal viewHeight = 0.0;
        qreal windowHeight = 0.0;
        QObject *view = nullptr;
        while (clock.elapsed() < kBootTimeoutMs) {
            view = window->findChild<QObject *>(
                QStringLiteral("markdownPreviewView"));
            viewHeight = view ? view->property("height").toReal() : 0.0;
            windowHeight = window->property("height").toReal();
            if (viewHeight > 0.0 && viewHeight >= windowHeight * 0.5) {
                return true;
            }
            QTest::qWait(100);
        }
        *reason = QStringLiteral("content collapsed: view height=%1 of window "
                                 "height=%2, view found=%3")
                      .arg(viewHeight).arg(windowHeight)
                      .arg(view ? QStringLiteral("yes") : QStringLiteral("no"));
        return false;
    }

    /**
     * @brief 等窗口自己宣布页面就绪（`pageReady`，命令泵的唯一闸门）
     * @param reason 输出：失败原因（带视图当时的加载状态）
     * @return pageReady 为真则页面可用
     *
     * 单独一条快判而不让 findAtLeastOne 兜底：页面没就绪时窗口一个命令都不发，
     * 搜索会一路空转到超时，报出来的是「搜不到 needle」这种和真实原因毫无关系
     * 的信息。等 pageReady 而不是等视图的加载状态——后者正是产品唯一的依赖：
     * 视图 loadSucceeded 之后泵才跑，第一次 setView/setTheme/find 全在它后面。
     *
     * 这条判据同时挡住预览最大的一个静默失效面：拦外链的 navigationRequested
     * 处理器把自家 shell.html 一起拦掉（QML 递来的 request.url 是字符串，拿
     * `.scheme` 判前缀恒为 undefined）——界面全白，而 C++ 侧的控制器、大纲、
     * 命令队列全部照常，只有这里能看见。
     */
    bool waitLoaded(QString *reason)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < kBootTimeoutMs) {
            if (m_page && m_page->property("previewReady").toBool()) {
                return true;
            }
            QTest::qWait(100);
        }
        *reason = QStringLiteral("the preview page never became ready;"
                                 " url=%1 loading=%2 progress=%3")
                      .arg(m_previewView ? m_previewView->property("url").toString()
                                         : QString())
                      .arg(m_previewView
                               ? m_previewView->property("loading").toBool() : false)
                      .arg(m_previewView
                               ? m_previewView->property("loadProgress").toInt() : 0);
        return false;
    }

    /**
     * @brief 驱动窗口内的命令泵跑一次文档内搜索，直到命中至少一处
     * @param word 要搜的词
     * @param reason 输出：失败原因
     * @return 页面报回 total>=1 则为真
     *
     * 反复问而不是一次：正文是分片挂进 DOM 的，第一次 find 很可能跑在内容进场
     * 之前（那时命中 0 处是正确答案，不是缺陷）。窗口自己负责把 find 命令送进
     * 页面，所以本函数只调控制器——一次往返走的就是产品里那条路径。
     */
    bool findAtLeastOne(const QString &word, QString *reason)
    {
        QSignalSpy spy(m_controller.get(), &MarkdownPreviewController::findResult);
        QElapsedTimer clock;
        clock.start();
        int total = 0;
        while (clock.elapsed() < kBootTimeoutMs) {
            const int before = spy.count();
            m_controller->requestFind(word, true, false, true);
            // wait() 只认「这次调用之后」的新信号，所以排一次问一次。
            spy.wait(1500);
            if (spy.count() > before) {
                total = spy.last().at(0).toInt();
                if (total >= 1) {
                    return true;
                }
            }
            QTest::qWait(150);
        }
        *reason = QStringLiteral("find('%1') never matched; last total=%2, "
                                 "page open=%3, view found=%4")
                      .arg(word).arg(total).arg(m_controller->active())
                      .arg(m_previewView ? QStringLiteral("yes")
                                         : QStringLiteral("no"));
        return false;
    }

    Settings m_settings;
    ThemeRegistry m_registry;
    Theme m_theme{&m_settings, &m_registry};
    // 控制器/提供者/引擎都是「一个用例一份」：成员只占位，实体在 init() 里建、
    // cleanup() 里拆。留成常驻对象的话，一次失败的 load()（窗口没建起来，
    // notifyWindowClosed 永远没人调）会把文档带进下一个用例，于是后面两个用例
    // 报的是「上一份文档残留」而不是真正的缺陷——排错时看到的是假现场。
    std::unique_ptr<MarkdownPreviewController> m_controller;
    std::unique_ptr<MarkdownPreviewProvider> m_provider;
    std::unique_ptr<QQmlEngine> m_engine;
    // 窗口还引用 tools 域（草稿、工作区树）与 MarkdownEdit（高亮），这里
    // 给真的：窗口是跨域的 L3 装配，替身只留给 workbench/ui 那几个动作。
    // WebEngineCompat 同样给真的：预览面的导航应答经它收口（Qt 5/Qt 6 的
    // 请求对象应答面不同），替身会把那条路径变成 ReferenceError。
    std::unique_ptr<awb::tools::ToolsFacade> m_tools;
    std::unique_ptr<awb::markdownedit::MarkdownEdit> m_markdownEdit;
    WebEngineCompat m_webCompat;
    WorkbenchStub m_workbench;
    UiStub m_ui;
    NavStub m_nav;
    std::unique_ptr<QQmlContext> m_context;
    // load() 路径：宿主视图（QQuickView 即 QQuickWindow）+ 它收养的工作台页。
    std::unique_ptr<QQuickView> m_view;
    QPointer<QQuickItem> m_page;
    std::unique_ptr<QQuickWindow> m_host;   ///< 复刻工作区结构的宿主窗（Loader 在它里面）
    QObject *m_loader = nullptr;    ///< 宿主的工作台 Loader（归宿主所有）
    QString m_docPath;              ///< 夹具写出的文档路径
    std::unique_ptr<QSignalSpy> m_windowRequested;
    QObject *m_previewView = nullptr;   ///< 页面里的那块视图（归页面所有）
    std::unique_ptr<QTemporaryDir> m_dir;   ///< 文档与导出目录
};

/**
 * @brief 每个用例独立一份现场：临时目录、控制器、提供者、引擎与单例注册全部重建
 */
void TestMarkdownPreviewUi::init()
{
    m_dir.reset(new QTemporaryDir());
    QVERIFY2(m_dir->isValid(), "a per-case temporary dir failed");

    m_controller.reset(new MarkdownPreviewController(&m_settings, &m_theme));
    // 提供者构造时就把 available 点亮（scheme 已在 main() 里注册）；它的 handler
    // 存的是控制器的裸指针，所以两者必须成对创建、成对销毁。
    m_provider.reset(new MarkdownPreviewProvider(m_controller.get()));
    // 工作区树与草稿走真的 ToolsFacade（独立数据根，不碰真实目录）。
    m_tools.reset(new awb::tools::ToolsFacade(m_dir->path()));
    m_markdownEdit.reset(new awb::markdownedit::MarkdownEdit(&m_theme));
    m_engine.reset(new QQmlEngine());
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // main.cpp 在 Qt 5 上补的模块导入路径（见 tst_ribbonui 的同款说明）：
    // 生成的 qmldir 在 qrc:/qt/qml 下，Qt 5 引擎的默认导入路径不含它。
    m_engine->addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif

    QVERIFY2(m_controller->blocks().isEmpty(),
             "the controller carried a document over from the previous case");

    // 单例名与 app/main.cpp 一致：窗口里那些大写名字（Theme / Markdown /
    // MarkdownPreview / Tools / MarkdownEdit）就是这么解析到的。引擎也是
    // 每用例新建，所以这里注册的一定是本用例那份实例——复用引擎时旧实例
    // 会留在类型缓存里。
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme", &m_theme);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Markdown",
                                 m_controller.get());
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "MarkdownPreview",
                                 m_provider.get());
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Tools",
                                 m_tools.get());
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "MarkdownEdit",
                                 m_markdownEdit.get());
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "WebEngineCompat",
                                 &m_webCompat);
}

/**
 * @brief 按「视图 → 引擎 → 提供者 → 控制器」的顺序拆现场
 *
 * WebEngineView 挂在窗口里，窗口必须先于引擎销毁；provider 的 handler 存着
 * 控制器的裸指针，所以 provider 又要先于控制器销毁。反序就是用例结束时一次
 * use-after-free，而且崩在下一个用例的半途，现场更难读。
 */
void TestMarkdownPreviewUi::cleanup()
{
    m_previewView = nullptr;
    m_page.clear();
    // 宿主先走：Loader 创建出来的那份工作台页是它的子对象，反序会让宿主
    // 拿着一个已经随引擎死掉的 item。
    m_host.reset();
    m_loader = nullptr;
    m_view.reset();
    m_context.reset();
    m_windowRequested.reset();
    m_engine.reset();
    // 引擎之后才轮到注册成单例的 tools/markdownEdit（渲染帧先停、注册再拆），
    // provider 先于控制器（handler 持有它的裸指针）。
    m_markdownEdit.reset();
    m_tools.reset();
    m_provider.reset();
    m_controller.reset();
    m_dir.reset();
}

/**
 * @brief 那份 QML 编译得动、装得进宿主、也把状态交回控制器
 *
 * 只有「加载成功」这一件事就值一个用例：页面文件引用了十余个共享组件与两个
 * C++ 单例，任何一处写错都是整份文件加载失败，而界面上只表现为右键没反应。
 */
void TestMarkdownPreviewUi::testPageLoadsAndTakesOwnership()
{
    QString error;
    QVERIFY2(load(&error), qPrintable(error));

    // 页面在 Component.onCompleted 里向控制器报到；这条链断了，控制器就永远
    // 以为没有窗口，命令一律不排（下一次预览什么也不会发生）。
    QCOMPARE(m_controller->active(), true);
    QCOMPARE(m_windowRequested->count(), 1);
    QVERIFY(!m_controller->filePath().isEmpty());
    QVERIFY2(!m_controller->outline().isEmpty(),
             "the outline never reached the controller after opening");

    // 视图建出来了但还没加载完，此时页面不许报错：三态默认 preview，只读与否
    // 由文档决定。
    QCOMPARE(m_controller->mode(), QStringLiteral("preview"));
    QCOMPARE(m_controller->dirty(), false);

    // 布局自查：内容区要真的拿到与宿主相称的高度。上一版缺陷的表现正是工具栏
    // 把自己撑成整窗、正文只剩 15 px，而对象一个不少——只有量尺寸看得见。
    QString contentError;
    QVERIFY2(waitContentHeight(m_view.get(), &contentError),
             qPrintable(contentError));
}

/**
 * @brief 由 keepAlive Loader 创建的工作台页要真的撑开内容区，且常驻不灭
 *
 * 两条判据各挡一类真实缺陷，且都只在「页面被工作区 Loader 创建」这条路径上
 * 才存在：
 *   1. 内容区塌陷——页面作为 Loader 的内容项时以 0x0 创建、变可见后才拿到
 *      真实尺寸，布局与直接 createObject 不同；产品里的表现是只看到工具栏、
 *      正文看不见，而对象一个不少，只有量高度才看得见；
 *   2. 常驻性——第二次预览（第二次 windowRequested）不得重建页面：keepAlive
 *      页一旦访问过就只在应用退出时销毁，重建等于丢掉编辑现场（这扇窗在
 *      独立窗口时期「关掉再开」的语义，页面化后由常驻性取代）。
 */
void TestMarkdownPreviewUi::testLoaderHostedPageFillsAndStaysResident()
{
    QString error;
    QVERIFY2(loadHost(&error), qPrintable(error));

    QPointer<QObject> first;
    QTRY_VERIFY_WITH_TIMEOUT(!(first = hostedPage()).isNull(), kBootTimeoutMs);
    QQuickItem *firstPage = qobject_cast<QQuickItem *>(first.data());
    QVERIFY2(firstPage, "the Loader's content item is not an Item");
    QTRY_VERIFY2_WITH_TIMEOUT(firstPage->width() > 100
                                  && firstPage->height() > 100,
                              "the hosted page never got a real size",
                              kBootTimeoutMs);
    QVERIFY2(waitContentHeight(m_host.get(), &error), qPrintable(error));
    QCOMPARE(m_controller->active(), true);
    QObject *const firstView = m_host->findChild<QObject *>(
        QStringLiteral("markdownPreviewView"));
    QVERIFY2(firstView, "no WebEngineView inside the hosted page");

    // 第二次预览另一份文件：页面必须还是同一个对象（常驻），并跟着换到
    // 新文档——keepAlive 的全部意义就在这里。
    const QString secondDoc = QDir(m_dir->path()).filePath(
        QStringLiteral("second.md"));
    {
        QFile file(secondDoc);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Second Probe\n\nmore text\n");
        file.close();
    }
    const awb::core::OpResult again = m_controller->openFile(secondDoc);
    QVERIFY2(again, qPrintable(again.error));
    QTest::qWait(200);
    QTRY_VERIFY_WITH_TIMEOUT(!hostedPage().isNull(), kBootTimeoutMs);
    QVERIFY2(hostedPage().data() == first.data(),
             "the keepAlive page was rebuilt instead of staying resident");
    QVERIFY2(waitContentHeight(m_host.get(), &error), qPrintable(error));
    QCOMPARE(m_controller->filePath(), secondDoc);
}

/**
 * @brief 搜索的完整往返：控制器排队 → 页面泵 → 渲染面 → 信标 → 控制器信号
 *
 * 命中的文本只存在于页面渲染出来的 DOM 里，所以「搜到一处」同时证明了：视图
 * 真的加载了 shell.html、doc.json 取到了载荷、分片管线把正文挂进了 DOM、
 * pump() 在跑、以及 find 的结果能顺着信标走回 C++。
 */
void TestMarkdownPreviewUi::testSearchRoundTripFindsTheRenderedText()
{
    QString error;
    QVERIFY2(load(&error), qPrintable(error));

    QSignalSpy renderFailed(m_controller.get(),
                            &MarkdownPreviewController::renderFailed);
    QString reason;
    QVERIFY2(waitLoaded(&reason), qPrintable(reason));
    QVERIFY2(findAtLeastOne(QStringLiteral("needle"), &reason), qPrintable(reason));
    QCOMPARE(renderFailed.count(), 0);
}

/**
 * @brief 点文件 tab：处理器走完、预设切换、没有任何 ReferenceError
 *
 * 2026-10 用户报告的缺陷：点文件 tab 抛 ReferenceError: scope is not defined
 *（页面 1700 行附近），applyPreset 死在 handler 半路——preset 留在 prompt、
 * leftOpen 却跟着 preset 的另一套状态走，左栏整个消失。机制：TapHandler 的
 * 处理器先调 markdown.activateSession()，它同步发 tabsChanged，tab 条的
 * Repeater 随之重建 delegate，当前 delegate 的作用域当场失效，其后对 scope
 * 的解析即抛错。修复把页面引用先取进局部变量（活在 JS 栈上，不受 delegate
 * 销毁影响）。
 *
 * 本用例按用户的操作序列驱动：页面先落在文档预设（load 打开了文件）→
 * 点草稿 tab 切去提示词预设 → 再点文件 tab 切回文档预设。两次点击各经过
 * 一条分支，消息钩子全程盯着 ReferenceError——这类错误不崩测试、不挂断言，
 * 只有钩子看得见（产品里它的表现就是静默的布局错乱）。
 */
void TestMarkdownPreviewUi::testFileTabClickSwitchesPresetWithoutScopeError()
{
    QString error;
    QVERIFY2(load(&error), qPrintable(error));

    // 页面按控制器的活跃文件会话推断出文档预设；草稿会话此时还没有建——
    // 先经页面动作切到提示词预设（等价命令条上的 Full Tools：applyPreset
    // 会把当前草稿的会话激活、草稿 tab 出现在条上）。
    QTRY_COMPARE(m_page->property("preset").toString(),
                 QStringLiteral("document"));
    QVERIFY2(QMetaObject::invokeMethod(m_page, "applyPreset",
                                       Q_ARG(QVariant,
                                             QStringLiteral("prompt"))),
             "applyPreset is not invokable on the page root");
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->isDraftActive(), 5000);
    QTRY_COMPARE(m_page->property("preset").toString(), QStringLiteral("prompt"));
    // 草稿会话的建立让 tab 条整个重建：旧 delegate 走 deleteLater，还会在
    // 视觉树上停留一小会儿——按它们的位置点击会打到旁边的 tab。等重建
    // 收尾再定位，点击才落在草稿 tab 本体上。
    QTest::qWait(500);

    g_pageWarnings.clear();
    g_previousMessageHandler = qInstallMessageHandler(pageMessageHandler);

    // 用户序列：点草稿 tab（这条分支调 switchDraftTo + applyPreset，草稿已
    // 激活时是幂等空转，但处理器的 scope 解析路径被真实走了一遍）。
    QList<QQuickItem *> allTabs;
    collectItemsByName(m_page, QStringLiteral("fileTab"), &allTabs);
    QQuickItem *draftRow = nullptr;
    for (QQuickItem *tab : allTabs) {
        if (tab->property("modelData").toMap()
                .value(QStringLiteral("draft")).toBool()) {
            draftRow = tab;
            break;
        }
    }
    QVERIFY2(draftRow != nullptr, "no draft tab on the strip");
    QVERIFY2(!draftRow->childItems().isEmpty(),
             "the draft tab has no visual content to click");
    QVERIFY2(draftRow->isVisible() && draftRow->width() > 10,
             "the draft tab is not visible or too small to click");

    const QPointF draftCenter = draftRow->mapToScene(
        QPointF(draftRow->width() * 0.25, draftRow->height() / 2));
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                      draftCenter.toPoint());
    QTRY_COMPARE_WITH_TIMEOUT(m_page->property("preset").toString(),
                              QStringLiteral("prompt"), 5000);

    // 文件 tab：切回文档预设——这正是用户日志里抛 scope 错的那条分支。
    // 点击前重新定位：草稿会话的建立已把 tab 条重建过一轮（点在 tab 左
    // 侧四分之一处，避开右缘的关闭按钮）。
    QQuickItem *fileRowNow = nullptr;
    QTRY_VERIFY2_WITH_TIMEOUT(
        (fileRowNow = fileTabItem(m_page)) != nullptr,
        "the file tab is not on the strip after the rebuild", 5000);
    const QPointF fileCenter = fileRowNow->mapToScene(
        QPointF(fileRowNow->width() * 0.25, fileRowNow->height() / 2));
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                      fileCenter.toPoint());
    QTRY_COMPARE_WITH_TIMEOUT(m_page->property("preset").toString(),
                              QStringLiteral("document"), 5000);
    QVERIFY2(!m_controller->isDraftActive(),
             "clicking the file tab did not activate the file session");
    QVERIFY(!m_controller->filePath().isEmpty());

    qInstallMessageHandler(g_previousMessageHandler);
    g_previousMessageHandler = nullptr;
    QVERIFY2(g_pageWarnings.isEmpty(),
             qPrintable(QStringLiteral("QML warnings during the tab clicks: ")
                        + g_pageWarnings.join(QLatin1Char(';'))));
}

/**
 * @brief 导出的完整往返：serialize 走 runJavaScript 的返回值落回磁盘
 *
 * 这是全功能里唯一一条「结果不经过信标」的路径（导出件远超信标能放的量），
 * 而它由页面里 pump() 的那个回调负责交回控制器——这条接线错了，导出会永远
 * 静默地什么都不写。
 */
void TestMarkdownPreviewUi::testExportWritesTheRenderedMarkup()
{
    QString error;
    QVERIFY2(load(&error), qPrintable(error));

    // 先等正文进场：在挂载完成前序列化，拿到的是占位符而不是文档内容。
    QString reason;
    QVERIFY2(waitLoaded(&reason), qPrintable(reason));
    QVERIFY2(findAtLeastOne(QStringLiteral("needle"), &reason), qPrintable(reason));

    const QString exportDir = QDir(m_dir->path()).filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(exportDir));

    QSignalSpy written(m_controller.get(), &MarkdownPreviewController::markupWritten);
    QSignalSpy writeFailed(m_controller.get(),
                           &MarkdownPreviewController::markupWriteFailed);
    m_controller->setExportDirectory(exportDir);
    m_controller->requestMarkup(QStringLiteral("export"));

    QVERIFY2(written.wait(kBootTimeoutMs),
             qPrintable(QStringLiteral("serialize never came back; a command is "
                                       "still queued=")
                        + (m_controller->hasPendingCommand()
                               ? QStringLiteral("yes") : QStringLiteral("no"))
                        + QStringLiteral(", write failures=")
                        + QString::number(writeFailed.count())));
    QCOMPARE(writeFailed.count(), 0);

    const QString path = written.at(0).at(0).toString();
    QCOMPARE(path, QDir(exportDir).filePath(QStringLiteral("note.html")));
    QCOMPARE(written.at(0).at(1).toString(), QStringLiteral("export"));

    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
    const QString html = QString::fromUtf8(file.readAll());
    QVERIFY2(html.contains(QStringLiteral("Preview Probe")),
             "the exported markup lost the document's own heading");
    QVERIFY2(html.contains(QStringLiteral("awb-block")),
             "the exported markup is not the block-mounted page DOM");
}

/**
 * @brief 悬浮大纲的收起与两个拖动自由度（2026-10 用户报告「通高悬浮遮挡
 * 正文」之后的形态回归）
 *
 * 压三条交互：
 *   1. 头部右侧的收起按钮把面板缩成一行标题（列表随之退场），再点一次
 *      还原——收起态下开关是唯一入口，丢了它就永远收着；
 *   2. 右下角手柄上下拖动调高度（QSizeGrip 形态）；
 *   3. 左边界左右拖动调宽度（面板右缘锚定在渲染窗右缘）。
 * 断言全部落在几何上（高度/宽度的实际变化），不以视觉截图为判据。
 */
void TestMarkdownPreviewUi::testOutlineFloatCollapsesAndResizes()
{
    QString error;
    QVERIFY2(load(&error), qPrintable(error));
    QString reason;
    QVERIFY2(waitLoaded(&reason), qPrintable(reason));

    // 打开大纲（默认关）：控制器是产品里唯一的状态源。
    m_controller->setShowOutline(true);

    QQuickItem *outline = nullptr;
    QTRY_VERIFY2_WITH_TIMEOUT(
        (outline = findItemByName(m_page, QStringLiteral("outlineFloat")))
            != nullptr,
        "the outline float never appeared", 5000);
    QVERIFY2(outline->isVisible(), "the outline float is not visible");
    QVERIFY2(outline->parentItem() != nullptr,
             "the outline has no host area");
    const qreal fullHeight = outline->height();
    QVERIFY2(fullHeight > 200,
             qPrintable(QStringLiteral("the expanded outline is ")
                        + QStringLiteral("suspiciously short: ")
                        + QString::number(fullHeight)));

    // --- 收起：头部右侧的开关把面板缩成一行标题 -----------------------
    QQuickItem *collapseButton = nullptr;
    QTRY_VERIFY2_WITH_TIMEOUT(
        (collapseButton = findItemByName(
             m_page, QStringLiteral("outlineCollapseButton"))) != nullptr,
        "the outline has no collapse button", 5000);
    const QPointF buttonCenter = collapseButton->mapToScene(
        QPointF(collapseButton->width() / 2, collapseButton->height() / 2));
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                      buttonCenter.toPoint());
    QTRY_VERIFY2_WITH_TIMEOUT(outline->height() < fullHeight / 2,
                              "collapsing the outline never shrank it",
                              5000);
    QVERIFY2(outline->height() < 70,
             qPrintable(QStringLiteral("the collapsed outline is still ")
                        + QStringLiteral("tall: ")
                        + QString::number(outline->height())));
    // 列表随之退场：收起态只剩标题行（可见性绑在折叠位上）。
    QQuickItem *const outlineList = findItemByName(
        m_page, QStringLiteral("outlineList"));
    QVERIFY2(outlineList != nullptr, "the outline list is missing");
    QVERIFY2(!outlineList->isVisible(),
             "the outline list is still visible while collapsed");

    // 再点一次：还原到拖出前的全高。
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                      buttonCenter.toPoint());
    QTRY_COMPARE_WITH_TIMEOUT(outline->height(), fullHeight, 5000);

    // --- 右下角手柄：向上拖调低高度 -----------------------------------
    // press 落在手柄上即被 MouseArea 抓取，此后移动即使越出 14×14 的
    // 命中区也继续派发（QQuickMouseArea 的 grab 语义）。
    QQuickItem *grip = nullptr;
    QTRY_VERIFY2_WITH_TIMEOUT(
        (grip = findItemByName(m_page,
                               QStringLiteral("outlineSizeGrip"))) != nullptr,
        "the outline has no size grip", 5000);
    const QPointF gripCenter = grip->mapToScene(
        QPointF(grip->width() / 2, grip->height() / 2));
    QTest::mouseMove(m_view.get(), gripCenter.toPoint());
    QTest::mousePress(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                      gripCenter.toPoint());
    const QPointF raised = gripCenter + QPointF(0, -120);
    QTest::mouseMove(m_view.get(), raised.toPoint());
    QTest::mouseRelease(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                        raised.toPoint());
    QTRY_VERIFY2_WITH_TIMEOUT(outline->height() < fullHeight - 90,
                              "dragging the grip never shrank the outline",
                              5000);
    const qreal resizedHeight = outline->height();

    // --- 左边界：向左拖调宽 -------------------------------------------
    QQuickItem *const edge = findItemByName(
        m_page, QStringLiteral("outlineWidthEdge"));
    QVERIFY2(edge != nullptr, "the outline has no width edge");
    const qreal widthBefore = outline->width();
    const QPointF edgeCenter = edge->mapToScene(
        QPointF(edge->width() / 2, edge->height() / 2));
    QTest::mouseMove(m_view.get(), edgeCenter.toPoint());
    QTest::mousePress(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                      edgeCenter.toPoint());
    const QPointF wider = edgeCenter + QPointF(-60, 0);
    QTest::mouseMove(m_view.get(), wider.toPoint());
    QTest::mouseRelease(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                        wider.toPoint());
    QTRY_VERIFY2_WITH_TIMEOUT(outline->width() > widthBefore + 40,
                              "dragging the left edge never widened the "
                              "outline", 5000);
    // 宽度拖动不动高度（两个自由度解耦）。
    QCOMPARE(outline->height(), resizedHeight);
}

// 与 app/main.cpp 同序：测试模式 → awbmd:// 注册 → WebEngine 初始化 →
// QGuiApplication。注册晚于 initialize() 时引擎拒绝写入 scheme 表，provider
// 会把 available 落成假，于是每个用例都在 load() 里报出同一个原因。
//
// 三个 Chromium 开关的原因同 tst_markdownpreview：本套件的宿主窗口在测试期间会被
// 其它窗口压住、也可能没有输入焦点，而 Chromium 对「被遮挡 / 后台」的渲染进程
// 冻住 requestAnimationFrame——预览面的挂载收尾正排在它上面。
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray flags = QByteArrayLiteral(
        "--disable-backgrounding-occluded-windows "
        "--disable-renderer-backgrounding "
        "--disable-background-timer-throttling");
    const QByteArray existing = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            existing.isEmpty() ? flags : existing + ' ' + flags);
    MarkdownPreviewProvider::registerScheme();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QtWebEngineQuick::initialize();
#else
    QtWebEngine::initialize();
#endif
    QGuiApplication app(argc, argv);
    // 与 app/main.cpp 同一兜底样式：原生（windows）样式不接受对 control 的
    // background/contentItem 定制，A* 组件全部是按 Basic 写的——不设为 Basic，
    // 这里加载的就是与产品不同的一套视觉，还会刷一屏「style does not support
    // customization」的警告，把真正的信号埋掉。
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    TestMarkdownPreviewUi tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_markdownpreviewui.moc"
