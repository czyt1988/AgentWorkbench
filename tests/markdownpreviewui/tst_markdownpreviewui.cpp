// tst_markdownpreviewui — 预览窗口那份 QML 的真实加载与往返。
//
// 这个目标存在的理由：MarkdownPreviewWindow.qml 是「窗口」那一层的代码，它同时
// 引用主题令牌、A* 组件、C++ 单例与 WebEngine 视图，而这四样在纯层测试里都不存在。
// tst_markdown 证明控制器算得出块与命令，tst_markdownpreview 证明页面渲染得对，
// 两者都碰不到这一段：
//   1. 文件本身能不能编译——引用不存在的组件属性、跨 Qt 版本不存在的信号名，
//      在这里会以 QQmlComponent 的加载错误当场暴露（这类错误在生产里表现为
//      「右键点了没反应」，页面冒烟从不点击，抓不到）；
//   2. 命令泵是不是真的在跑——pump() 只在窗口里存在，而 find / serialize 的
//      往返必须穿过它才谈得上「导出有了文件」「搜索报出命中数」；
//   3. 窗口与控制器之间的报到契约——notifyWindowOpened / Closed 的时序错了，
//      下一次预览就打不开窗。
//
// 与 tst_markdownpreview 的分工：那边用内联 QML 造一个裸视图直接问页面（页面层），
// 这边加载**产品那份 .qml**（窗口层）。因此本目标要把窗口引用的组件、图标与
// 运行时资源按 app/CMakeLists.txt 的同款别名嵌进来——嵌错了测的就是另一份代码。
//
// 只在 Qt 6 + WebEngine 的构建里存在（tests/CMakeLists.txt 的条件）。
//
// workbench 与 ui 用替身：真实实现要把整个 app 装配进来（nav、shell、
// notifications、agents、webTabs），而窗口只用到它们的三个动作。替身把用户
// 可见的后果（toast、剪贴板、外链、reveal）留在 C++ 侧可断言，同时不让本目标
// 依赖 UI 框架层。小写别名经上下文属性提供——测试工具的做法，app 侧禁用
// setContextProperty（见 tst_toolsui 的同款说明）。

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
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

#include <QtWebEngineQuick>

using awb::core::Settings;
using awb::markdown::MarkdownPreviewController;
using awb::markdown::MarkdownPreviewProvider;
using awb::theme::Theme;
using awb::theme::ThemeRegistry;

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

    QStringList notices;    ///< 每次 notify 的 level|title|text
    QString copied;         ///< 最后一次复制的文本
    QUrl external;          ///< 最后一次交给外部浏览器的地址
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

/**
 * @brief 一个用例一套完整现场：控制器 + 提供者 + 引擎 + 产品那份窗口
 *
 * 现场对象由 init()/cleanup() 成对建拆（见那两个函数的注释）：窗口挂着
 * WebEngineView，必须最先销毁，然后是上下文与引擎，最后才是 provider 与它
 * 引用到的控制器。
 */
class TestMarkdownPreviewUi : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testWindowLoadsAndTakesOwnership();
    void testSearchRoundTripFindsTheRenderedText();
    void testExportWritesTheRenderedMarkup();

private:
    /**
     * @brief 装配现场并加载产品那份 MarkdownPreviewWindow.qml
     * @param error 输出：失败原因（含 QML 的完整错误串）
     * @return 文档已打开且窗口对象已创建则为真
     *
     * 先 openFile 再建窗口，与真实入口同序（ToolsPage 的右键 →
     * workbench.previewMarkdown → 控制器发 windowRequested → MainWindow 的
     * Loader 创建窗口）。顺序反了窗口会在「还没有文档」的状态下加载页面。
     */
    bool load(QString *error)
    {
        m_theme.applyTheme(QStringLiteral("mocha-dark"));
        if (!m_controller->available()) {
            *error = QStringLiteral("surface unavailable: ")
                     + m_controller->unavailableReason();
            return false;
        }
        const QString path = QDir(m_dir->path())
                                 .filePath(QStringLiteral("note.md"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            *error = QStringLiteral("cannot write the fixture: ") + path;
            return false;
        }
        file.write(kFixture);
        file.close();

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

        QQmlComponent component(m_engine.get());
        component.loadUrl(QUrl(QStringLiteral(
            "qrc:/qt/qml/AgentWorkbench/markdown/MarkdownPreviewWindow.qml")));
        if (component.isError()) {
            *error = component.errorString();
            return false;
        }
        QObject *created = component.create(m_context.get());
        if (!created) {
            *error = component.errorString();
            return false;
        }
        m_window.reset(qobject_cast<QQuickWindow *>(created));
        if (!m_window) {
            *error = QStringLiteral("the preview window root is not a QQuickWindow: ")
                     + created->metaObject()->className();
            delete created;
            return false;
        }
        m_view = m_window->findChild<QObject *>(QStringLiteral("markdownPreviewView"));
        if (!m_view) {
            *error = QStringLiteral("no WebEngineView in the loaded window");
            return false;
        }
        return true;
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
            if (m_window->property("pageReady").toBool()) {
                return true;
            }
            QTest::qWait(100);
        }
        *reason = QStringLiteral("the preview page never became ready;"
                                 " url=%1 loading=%2 progress=%3")
                      .arg(m_view->property("url").toString())
                      .arg(m_view->property("loading").toBool())
                      .arg(m_view->property("loadProgress").toInt());
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
                                 "window open=%3, view found=%4")
                      .arg(word).arg(total).arg(m_controller->active())
                      .arg(m_view ? QStringLiteral("yes")
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
    WorkbenchStub m_workbench;
    UiStub m_ui;
    std::unique_ptr<QQmlContext> m_context;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QSignalSpy> m_windowRequested;
    QObject *m_view = nullptr;      ///< 窗口里的那块视图（归窗口所有）
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
    m_engine.reset(new QQmlEngine());

    QVERIFY2(m_controller->blocks().isEmpty(),
             "the controller carried a document over from the previous case");

    // 单例名与 app/main.cpp 一致：窗口里那些大写名字（Theme / Markdown /
    // MarkdownPreview）就是这么解析到的。引擎也是每用例新建，所以这里注册的
    // 一定是本用例那份实例——复用引擎时旧实例会留在类型缓存里。
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Theme", &m_theme);
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "Markdown",
                                 m_controller.get());
    qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "MarkdownPreview",
                                 m_provider.get());
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
    m_view = nullptr;
    m_window.reset();
    m_context.reset();
    m_windowRequested.reset();
    m_engine.reset();
    m_provider.reset();
    m_controller.reset();
    m_dir.reset();
}

/**
 * @brief 那份 QML 编译得动、开得了窗、也把状态交回控制器
 *
 * 只有「加载成功」这一件事就值一个用例：窗口文件引用了十余个共享组件与两个
 * C++ 单例，任何一处写错都是整份文件加载失败，而界面上只表现为右键没反应。
 */
void TestMarkdownPreviewUi::testWindowLoadsAndTakesOwnership()
{
    QString error;
    QVERIFY2(load(&error), qPrintable(error));

    // 窗口在 Component.onCompleted 里向控制器报到；这条链断了，控制器就永远
    // 以为没有窗口，命令一律不排（下一次预览什么也不会发生）。
    QCOMPARE(m_controller->active(), true);
    QCOMPARE(m_windowRequested->count(), 1);
    QVERIFY(!m_controller->filePath().isEmpty());
    QVERIFY2(!m_controller->outline().isEmpty(),
             "the outline never reached the controller after opening");

    // 视图建出来了但还没加载完，此时窗口不许报错：三态默认 preview，只读与否
    // 由文档决定。
    QCOMPARE(m_controller->mode(), QStringLiteral("preview"));
    QCOMPARE(m_controller->dirty(), false);
}

/**
 * @brief 搜索的完整往返：控制器排队 → 窗口泵 → 页面 → 信标 → 控制器信号
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
 * @brief 导出的完整往返：serialize 走 runJavaScript 的返回值落回磁盘
 *
 * 这是全功能里唯一一条「结果不经过信标」的路径（导出件远超信标能放的量），
 * 而它由窗口里 pump() 的那个回调负责交回控制器——这条接线错了，导出会永远
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

// 与 app/main.cpp 同序：测试模式 → awbmd:// 注册 → WebEngine 初始化 →
// QGuiApplication。注册晚于 initialize() 时引擎拒绝写入 scheme 表，provider
// 会把 available 落成假，于是每个用例都在 load() 里报出同一个原因。
//
// 三个 Chromium 开关的原因同 tst_markdownpreview：本套件的窗口在测试期间会被
// 其它窗口压住、也可能没有输入焦点，而 Chromium 对「被遮挡 / 后台」的渲染进程
// 冻住 requestAnimationFrame——窗口的挂载收尾正排在它上面。
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
    QtWebEngineQuick::initialize();
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
