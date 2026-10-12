// tst_markdownpreview — 预览页面在真实引擎里到底渲染成什么。
//
// 这个目标存在的理由：命令与信标都是字符串协议，而页面侧的执行只可能发生在
// Chromium 里。tst_markdown 能证明「C++ 算出的块、URL、命令 JSON 自洽」，
// tst_webengine 能证明「awbmd:// 供得出脚本与字体、fetch 通、信标接得住」，
// 但**逐块管线跑起来的内容对不对**只有把真的 shell.html 装进真的视图里才看得见。
// 这里覆盖的就是这一段：模块脚本图（bridge.js → renderer.js → …）、按块挂载、
// KaTeX/mermaid/highlight.js 的真实产物、增量替换保留未动的 DOM、搜索与序列化。
//
// 只在 Qt 6 + WebEngine 的构建里存在（tests/CMakeLists.txt 的条件），Qt 5 与
// AWB_ENABLE_WEBENGINE=OFF 的连接里既不建这个目标，也没有预览页面可言。
//
// 与 app 的分工：视图在这里是内联 QML 建的 WebEngineView，探针经
// runJavaScript 的回调回读。QML 侧的 profile 只能在 QML 里赋值（C++ 没有公开
// setter），所以预览 profile 经上下文属性递进去——上下文属性是**测试工具**的
// 做法，app 侧按 AGENTS.md 禁用。

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include "core/OpResult.h"
#include "core/Settings.h"
#include "markdown/MarkdownPreviewController.h"
#include "markdown/MarkdownRuntime.h"
#include "markdown/webengine/MarkdownPreviewProvider.h"
#include "theme/Theme.h"
#include "theme/ThemeRegistry.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtWebEngineQuick>
#else
#include <QtWebEngine>
#endif

using awb::core::Settings;
using awb::markdown::MarkdownPreviewController;
using awb::markdown::MarkdownPreviewProvider;
using awb::markdown::MarkdownRuntime;
using awb::theme::Theme;
using awb::theme::ThemeRegistry;

namespace {

// 页面运行时能跑到的最长等待：首次加载要起渲染进程、取 doc.json、执行模块图，
// Debug 二进制 + 冷启动的 Chromium 在慢机上能到几秒。超时是「页面没回话」，
// 而页面没回话在本套件里等价于功能不可用，所以宁可给足。
constexpr int kBootTimeoutMs = 40000;
constexpr int kProbeTimeoutMs = 20000;

// 覆盖全部渲染特性的一份文档。每一项都是页面管线里一个独立的分支：公式走
// KaTeX、mermaid 走延后渲染、cpp 围栏走 highlight.js、任务项与告警走自装的
// markdown-it 规则、图片走 awbmd://runtime/asset/、表格走 GFM 扩展。
// 少一项就等于那条分支从没在真实引擎上跑过。
const char *kFixture = R"MD(# Preview Probe

Inline math $E=mc^2$ and a [link](https://example.com/x).

$$\int_0^1 x\,dx = \tfrac12$$

- [x] done task
- [ ] open task

| a | b |
| - | - |
| 1 | 2 |

```cpp
int main() { return 0; }
```

> [!WARNING] careful here

![dot](dot.png)

```mermaid
graph TD
  A[Start] --> B{Check}
  B --> C[End]
```

The final paragraph mentions WARNING twice: warning.
)MD";

// 视图从内联 QML 建：只有 QML 能把预览 profile 赋给 WebEngineView。
// probe(id, script) 把 runJavaScript 的返回值 JSON 化，连同问题编号写进
// probeReply；C++ 侧轮询这一个字符串——用 string 而不是 JS 对象作回读容器，
// 是因为 JS 对象赋给 variant 属性后经 QVariant 取回的形状不受本测试控制，
// 「读不到」会被误判成「页面没答」。
//
// 视图必须装进一个 visible 的 Window，不能只 create() 一个游离的 view：
// 页面的挂载收尾走 requestAnimationFrame（renderer.js 的 flushBatch），而
// Chromium 把「所在窗口不可见」的 frame 的动画帧整条冻住。游离 view 上的表现
// 是分片全解析完、一个节点也没进 DOM，stats 却报 mounted=块数——即
// 「正文永远空白，而数据看着一切正常」。产品的预览窗本来就是可见窗口，所以
// 这条不是测试的额外要求，而是本套件必须复现的前提。
// Qt 5 的编译器要求库 import 带版本（qqmlirbuilder 的 "Library import
// requires a version"）；Qt 6 推荐无版本写法。两版各自的 import 头。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
static const char kViewImports[] = "import QtQuick\nimport QtWebEngine\n";
#else
static const char kViewImports[] =
    "import QtQuick 2.15\nimport QtQuick.Window 2.15\n"
    "import QtWebEngine 1.10\n";
#endif
const char *kViewQml = R"QML(

Window {
    id: probeWindow

    width: 1024
    height: 768
    visible: true
    title: "awb-markdown-preview-probe"
    color: "black"

    // 推入车道（fetch 不可用的引擎）的信标走 console（AWBEVT: 前缀），
    // 这里复刻 pane 的转发：把信标交回控制器的对应入口。裸视图不接这条，
    // find 的命中数永远回不到 findResult 信号。fetch 车道的信标走 /event
    // 像素请求，console 里只有普通日志。
    property var beaconSink: null
    function forwardBeacon(text) {
        const marker = "AWBEVT:"
        if (text.indexOf(marker) !== 0)
            return
        const rest = text.substring(marker.length)
        const split = rest.indexOf(":")
        if (split < 0)
            return
        const kind = rest.substring(0, split)
        const payload = rest.substring(split + 1)
        if (!beaconSink)
            return
        if (kind === "ready")
            beaconSink.reportReady(payload)
        else if (kind === "frame")
            beaconSink.reportFrame(payload)
        else if (kind === "error")
            beaconSink.reportError(payload)
        else if (kind === "find")
            beaconSink.reportFindResult(payload)
    }

    // 渲染进程的死亡计数：滚动用例的判据之一（renderer 崩了 runJavaScript
    // 只会沉默，计数把「崩过」变成可断言的事实，而不是只看探针超时）。
    property int rendererCrashes: 0

    WebEngineView {
        objectName: "awbProbeView"
        anchors.fill: parent
        profile: awbPreviewProfile
        property string probeReply: ""
        function probe(id, script) {
            runJavaScript(script, function (result) {
                probeReply = id + "\u0001"
                    + JSON.stringify(result === undefined ? null : result)
            })
        }
        onJavaScriptConsoleMessage: function (level, message,
                                              lineNumber, sourceID) {
            // 编译型 QML 的信号处理器里，跨对象根 id 会 ReferenceError——
            // 经 probeWindow 引用（AGENTS.md「QML 契约」同一条纪律）。
            probeWindow.forwardBeacon(String(message))
        }
        onRenderProcessTerminated: function (status, exitCode) {
            probeWindow.rendererCrashes += 1
        }
    }
}
)QML";

/**
 * @brief 把探针回答的 JSON 文本当字符串取出
 * @param json probeReply 里等号右半边的 JSON 值
 * @return 该值解出的字符串；不是字符串（数组/对象/null）时返回原 JSON 文本
 *
 * runJavaScript 的返回值经 JSON.stringify 回来，所以字符串两侧带引号、内部
 * 引号与换行都是转义形态。QJsonDocument 不接受裸标量（6.8 以前），套一层数组
 * 再取第 0 项是各版本都能做的解法。数组/对象没有解的必要，原样返回即可。
 */
QString jsonTextValue(const QString &json)
{
    const QByteArray wrapped = '[' + json.toUtf8() + ']';
    const QJsonArray array = QJsonDocument::fromJson(wrapped).array();
    if (!array.isEmpty() && array.at(0).isString()) {
        return array.at(0).toString();
    }
    return json;
}

/**
 * @brief 把 QString 编成 JS 字符串字面量（含两侧引号）
 *
 * 推入车道把 doc.json / theme.css 的文本喂给 AWB.push 时用：载荷里什么
 * 字符都可能有，手工只转义反斜杠/引号/换行不可靠。走 QJsonDocument 的
 * 数组序列化取回第一个元素即得标准的 JSON 字符串字面量（JSON 字符串与
 * JS 字符串字面量对这批文本完全同构）。
 */
QString jsStringLiteral(const QString &text)
{
    QJsonArray wrapper;
    wrapper.append(text);
    const QByteArray serialized =
            QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    // 形如 ["…"] 的数组文本，去掉方括号即字符串字面量。
    return QString::fromUtf8(serialized.mid(1, serialized.size() - 2));
}

} // namespace

/**
 * @brief 一个用例一套完整现场：控制器 + 提供者 + 视图 + 临时文档目录
 *
 * 成员声明顺序即析构反序：视图与引擎先走，再是装着 handler 的 provider，
 * 最后才是 controller 与它监视的临时目录——handler 里存的是控制器的裸指针，
 * 顺序反了就是 use-after-free，而且只在用例结束时崩。
 */
class Page
{
public:
    Page() = default;
    Q_DISABLE_COPY(Page)

    /**
     * @brief 打开一份 Markdown 并把页面装起来
     * @param markdown 文档正文（写进临时目录的 note.md）
     * @param error 输出：失败原因
     * @return 视图已创建且文档已交给控制器则为真
     *
     * 视图的 url 在 openFile 之后才设：doc.json 由 handler 现问控制器，
     * 早设一步会让页面拿到「还没有文档」的载荷。
     */
    bool open(const QString &markdown, QString *error)
    {
        dir.reset(new QTemporaryDir());
        if (!dir->isValid()) {
            *error = QStringLiteral("temporary document dir failed");
            return false;
        }
        const QString path = QDir(dir->path())
                                 .filePath(QStringLiteral("note.md"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            *error = QStringLiteral("cannot write the fixture: ") + path;
            return false;
        }
        file.write(markdown.toUtf8());
        file.close();

        // 文档目录里真放一张图片：asset 那条路由 handler 读磁盘，「src 形状
        // 对不对」只有配上真实文件才有意义（能不能解码由 asset-load 探针问）。
        QImage dot(8, 8, QImage::Format_ARGB32);
        dot.fill(Qt::red);
        const QString imagePath = QDir(dir->path())
                                      .filePath(QStringLiteral("dot.png"));
        if (!dot.save(imagePath, "PNG")) {
            *error = QStringLiteral("cannot write the fixture image");
            return false;
        }

        theme.applyTheme(QStringLiteral("mocha-dark"));
        if (!controller.available()) {
            *error = QStringLiteral("surface unavailable: ")
                     + controller.unavailableReason();
            return false;
        }
        const awb::core::OpResult opened = controller.openFile(path);
        if (!opened) {
            *error = opened.error;
            return false;
        }
        controller.notifyWindowOpened();

        context.reset(new QQmlContext(engine.rootContext()));
        context->setContextProperty(QStringLiteral("awbPreviewProfile"),
                                    provider.previewProfile());
        component.reset(new QQmlComponent(&engine));
        component->setData(
            QByteArray(kViewImports) + QByteArray(kViewQml), QUrl());
        if (component->isError()) {
            *error = component->errorString();
            return false;
        }
        topWindow.reset(component->create(context.data()));
        if (topWindow.isNull()) {
            *error = component->errorString();
            return false;
        }
        // console 信标的接收方（推入车道）：见 kViewQml 里 forwardBeacon。
        topWindow->setProperty("beaconSink", QVariant::fromValue(&controller));
        view.reset(topWindow->findChild<QObject *>("awbProbeView"));
        if (view.isNull()) {
            *error = QStringLiteral("the probe view is missing from the QML");
            return false;
        }
        // 与产品同源：url 绑 controller.pageUrl()（含 Qt < 6.6 的车道提示，
        // 页面侧的 lane 短路分支在本套件里也被真实行使）。
        view->setProperty("url", controller.pageUrl().toString());
        return true;
    }

    MarkdownPreviewController *state() { return &controller; }

    /// 渲染进程的死亡计数（kViewQml 的 onRenderProcessTerminated 记的）
    int rendererCrashes() const
    {
        return topWindow->property("rendererCrashes").toInt();
    }

    /**
     * @brief 问页面一个问题并等回答
     * @param tag 探针名（出现在失败消息里）
     * @param script 求值的 JS 表达式
     * @param jsonOut 输出：返回值的 JSON 文本
     * @param timeoutMs 等待上限
     * @return 页面回了话则为真
     *
     * 编号用「tag#序号」：probeReply 是单一容器，后一次回答会覆盖前一次，
     * 不带编号就无法区分「本次没答」与「上次的答案」——后者是**假通过**，
     * 比失败更糟。
     */
    bool probe(const QString &tag, const QString &script, QString *jsonOut,
               int timeoutMs = kProbeTimeoutMs)
    {
        const QString id = QStringLiteral("%1#%2").arg(tag).arg(++probeSeq);
        QMetaObject::invokeMethod(view.data(), "probe", Qt::AutoConnection,
                                  Q_ARG(QVariant, id),
                                  Q_ARG(QVariant, script));
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < timeoutMs) {
            const QString reply = view->property("probeReply").toString();
            if (reply.startsWith(id + QStringLiteral("\u0001"))) {
                if (jsonOut) {
                    *jsonOut = reply.mid(id.length() + 1);
                }
                return true;
            }
            QTest::qWait(50);
        }
        return false;
    }

    /**
     * @brief 探针 + 「必须回空清单」，直到通过或超时
     * @param tag 探针名
     * @param script 返回「缺了什么」数组的 JS
     * @param reasonOut 输出：失败原因
     * @return 数组为空则为真
     *
     * 反复问而不是问一次：图片解码、highlight.js 的 IntersectionObserver
     * 这些都排在浏览器自己的时间线上，一次采样会把「还没好」读成「坏了」。
     * 本函数的脚本必须是纯读的（只数 DOM、不写状态），否则重试会叠加副作用。
     */
    bool probeOk(const QString &tag, const QString &script,
                 QString *reasonOut = nullptr, int timeoutMs = kBootTimeoutMs)
    {
        QElapsedTimer clock;
        clock.start();
        QString json;
        while (clock.elapsed() < timeoutMs) {
            if (probe(tag, script, &json, 5000)
                && json.trimmed() == QLatin1String("[]")) {
                return true;
            }
            QTest::qWait(100);
        }
        if (reasonOut) {
            *reasonOut = QStringLiteral("probe '%1' missing: %2")
                             .arg(tag, json);
        }
        return false;
    }

    /**
     * @brief 反复问同一个问题，直到回答里出现 want 或超时
     * @param tag 探针名
     * @param script 求值的 JS
     * @param want 期望出现的子串
     * @param lastOut 输出：最后一次回答
     * @param timeoutMs 等待上限
     * @return 出现则真
     *
     * 图表补渲染这类「页面自己安排时间」的等待用它，而不是固定 sleep：
     * 固定等待在慢机上假失败、在快机上白等。
     */
    bool waitFor(const QString &tag, const QString &script,
                 const QString &want, QString *lastOut = nullptr,
                 int timeoutMs = kBootTimeoutMs)
    {
        QElapsedTimer clock;
        clock.start();
        QString json;
        while (clock.elapsed() < timeoutMs) {
            if (probe(tag, script, &json, 5000) && json.contains(want)) {
                if (lastOut) {
                    *lastOut = json;
                }
                return true;
            }
            QTest::qWait(100);
        }
        if (lastOut) {
            *lastOut = json;
        }
        return false;
    }

    /**
     * @brief 排空控制器的命令队列，逐条交给页面
     *
     * 这就是 app 里预览窗口那个泵要做的事（有命令 → runJavaScript
     * ("AWB.dispatch(…)")），连 serialize 的返回值处理也一样：导出件是字符串
     * 协议里唯一的大结果，必须从 runJavaScript 的返回值取回再交
     * reportMarkup——信标有 8 KB 上限，塞进去只会被截断成一份坏文件。
     */
    void pumpCommands()
    {
        while (controller.hasPendingCommand()) {
            const QString json = controller.takeCommand();
            // 推入车道的 setDocument 只带 rev（fetch 车道页面自己拉新正文），
            // 先把新载荷推进缓存——产品里 pane 的 dispatch 收口做同一件事。
            if (lane == "push"
                    && json.contains(QStringLiteral("\"cmd\":\"setDocument\""))) {
                pushDocumentOnce(/* documentOnly = */ true);
            }
            QString reply;
            probe(QStringLiteral("dispatch"),
                  QStringLiteral("AWB.dispatch(%1)").arg(json), &reply,
                  kProbeTimeoutMs);
            if (json.contains(QStringLiteral("\"cmd\":\"serialize\""))) {
                controller.reportMarkup(jsonTextValue(reply));
            }
        }
    }

    /// 把 doc.json / theme.css 推进页面（推入车道）。documentOnly 用于
    /// setDocument 前的重推：样式表不随文档变。
    void pushDocumentOnce(bool documentOnly = false)
    {
        const QString pushScript = QStringLiteral("AWB.push(%1, %2)");
        if (!documentOnly) {
            probe(QStringLiteral("push-css"),
                  pushScript.arg(QStringLiteral("\"stylesheet\""),
                                 jsStringLiteral(
                                     MarkdownRuntime::resourceTextForPath(
                                         QStringLiteral("app/theme.css")))),
                  nullptr, kProbeTimeoutMs);
        }
        probe(QStringLiteral("push-doc"),
              pushScript.arg(QStringLiteral("\"document\""),
                             jsStringLiteral(controller.documentBodyJson())),
              nullptr, kProbeTimeoutMs);
    }

    /// 车道结论（"" = 页面还在探；"fetch" / "push"）。waitMounted 轮询时
    /// 顺带驱动：读到 "push" 就把文档与样式表投进去（推入车道页面等它启动）。
    QString lane;

    /// 等页面把 blocks 全部挂上（分片管线跑完的客观标志）
    bool waitMounted(int blocks, QString *reasonOut = nullptr,
                     int timeoutMs = kBootTimeoutMs)
    {
        QElapsedTimer clock;
        clock.start();
        QString json;
        while (clock.elapsed() < timeoutMs) {
            // 车道驱动：结论没定格就再问一次 AWB.channel()（runJavaScript
            // 的回调等不到 Promise，页面把结论定格在同步返回值里）。
            if (lane.isEmpty()
                    && probe(QStringLiteral("channel"),
                             QStringLiteral("(typeof AWB === 'undefined' ? '' : AWB.channel())"),
                             &json, 5000)) {
                lane = jsonTextValue(json);
                if (lane == QStringLiteral("push")) {
                    pushDocumentOnce();
                }
            }
            // 问「AWB 在不在」而不是直接 AWB.stats()：模块脚本图要跑一会儿，
            // 直接问会让每次轮询都在控制台留一条 ReferenceError，把真正的
            // 页面报错埋在噪音里。
            if (probe(QStringLiteral("stats"),
                      QStringLiteral(
                          "(typeof AWB === 'undefined' ? null : AWB.stats())"),
                      &json, 5000)) {
                const QJsonObject stats =
                    QJsonDocument::fromJson(json.toUtf8()).object();
                if (stats.value(QStringLiteral("mounted")).toInt() >= blocks) {
                    return true;
                }
            }
            QTest::qWait(100);
        }
        if (reasonOut) {
            *reasonOut = QStringLiteral("the page never mounted %1 blocks, "
                                        "last stats: %2")
                             .arg(blocks)
                             .arg(jsonTextValue(json));
        }
        return false;
    }

private:
    Settings settings;                     ///< 视图偏好与落点（测试模式下重定向）
    ThemeRegistry registry;                ///< 随包主题
    Theme theme{&settings, &registry};     ///< CSS 变量的来源
    QScopedPointer<QTemporaryDir> dir;     ///< 文档目录（活得比 controller 久）
    MarkdownPreviewController controller{&settings, &theme};
    MarkdownPreviewProvider provider{&controller};  ///< 点亮 available，供 awbmd://
    QQmlEngine engine;
    QScopedPointer<QQmlContext> context;
    QScopedPointer<QQmlComponent> component;
    QScopedPointer<QObject> topWindow;     ///< 承载视图的可见窗口（QML 建的）
    QScopedPointer<QObject> view;          ///< 视图本体（QML 里 objectName 找到的）
    int probeSeq = 0;
};

/**
 * @brief 预览页面的渲染用例集
 */
class TestMarkdownPreview : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    /**
     * @brief 首屏：模块脚本图跑起来，文档里每一种特性都渲染成它该有的结构
     *
     * 这是计划里 S2/S3 的定案处：公式与图表「支持」的判据不是在 Node 里能跑通
     * markdown-it，而是真页面里 KaTeX 出了 .katex、mermaid 出了带状态的占位、
     * highlight.js 的钩子挂上了、文档相对路径的图片经 awbmd:// 真的解出了像素。
     */
    void testPageRendersEveryFeature()
    {
        Page page;
        QString error;
        QVERIFY2(page.open(QString::fromUtf8(kFixture), &error),
                 qPrintable(error));

        QString reason;
        QVERIFY2(page.waitMounted(page.state()->blocks().size(), &reason),
                 qPrintable(reason));

        const QString script = QStringLiteral(
            "(function () {"
            "  var miss = [];"
            "  function need(ok, name) { if (!ok) { miss.push(name); } }"
            "  var d = document.getElementById('awb-doc');"
            "  need(d !== null, 'root');"
            "  need(d.querySelectorAll('section.awb-block').length >= 9,"
            "       'blocks');"
            "  need(d.querySelector('h1')"
            "       && /Preview Probe/.test(d.querySelector('h1')"
            "                             .textContent), 'h1');"
            "  need(d.querySelectorAll('.katex').length >= 2, 'katex');"
            "  need(d.querySelector('.awb-math-block .katex-display') !== null,"
            "       'display-math');"
            "  need(d.querySelector('table thead th') !== null, 'table');"
            "  need(d.querySelector('.awb-code code.language-cpp') !== null,"
            "       'code-lang');"
            "  need(d.querySelector('.awb-code-lang')"
            "       && d.querySelector('.awb-code-lang').textContent === 'cpp',"
            "       'code-label');"
            "  need(d.querySelector('code[data-awb-hl]') !== null, 'hljs-hook');"
            "  need(d.querySelector('.awb-task-box[data-checked=\"1\"]')"
            "       !== null, 'task-checked');"
            "  need(d.querySelector('.awb-task-box[data-checked=\"0\"]')"
            "       !== null, 'task-open');"
            "  need(d.querySelector('blockquote.awb-alert-warning') !== null,"
            "       'alert');"
            "  need(d.querySelector('a[href=\"https://example.com/x\"]')"
            "       !== null, 'link');"
            "  var img = d.querySelector('img');"
            "  need(img && img.getAttribute('src')"
            "            .indexOf('awbmd://runtime/asset/') === 0, 'asset-src');"
            "  need(img && img.complete && img.naturalWidth > 0, 'asset-load');"
            "  need(d.querySelector('.awb-diagram[data-status]') !== null,"
            "       'diagram-slot');"
            "  need(!d.querySelector('.awb-math-error'), 'math-parse');"
            "  need(!d.querySelector('.awb-block-error'), 'block-error');"
            "  return miss;"
            "})()");
        QVERIFY2(page.probeOk(QStringLiteral("features"), script, &reason),
                 qPrintable(reason));

        // 大纲：C++ 从块序列算出来的锚点必须真的在 DOM 上存在——
        // 大纲面板「点一下跳到那一块」全靠这个同名约定（b<块索引>）。
        const QVariantList outline = page.state()->outline();
        QVERIFY2(outline.size() >= 1, "the fixture has a heading");
        for (const QVariant &entry : outline) {
            const QString anchor =
                entry.toMap().value(QStringLiteral("anchor")).toString();
            QVERIFY2(!anchor.isEmpty(), "outline entries need an anchor");
            QString answer;
            QVERIFY2(page.probe(QStringLiteral("anchor"),
                                QStringLiteral(
                                    "(document.getElementById('%1') !== null)")
                                    .arg(anchor),
                                &answer),
                     qPrintable(QStringLiteral("anchor probe failed: ")
                                + anchor));
            QVERIFY2(answer.trimmed() == QLatin1String("true"),
                     qPrintable(QStringLiteral("anchor ") + anchor
                                + " is not in the DOM: " + answer));
        }
    }

    /**
     * @brief 增量替换：只重渲染改动的那一块，其余 DOM 原样不动
     *
     * 分块管线的第二条腿。第一条腿（首屏按片挂载）由上一个用例覆盖，这条保证
     * 的是「敲一个字不重排全篇」：给未改动的块打一个 expando，改一次文末段落，
     * 命令走 C++ 的 diff → 队列 → dispatch，然后问那个标记还在不在。
     * 标记没了就意味着整篇重建——性能设计当场失效，而用户只会觉得「编辑时卡」。
     */
    void testEditingReplacesOnlyTheChangedBlock()
    {
        Page page;
        QString error;
        QVERIFY2(page.open(QString::fromUtf8(kFixture), &error),
                 qPrintable(error));

        QString reason;
        QVERIFY2(page.waitMounted(page.state()->blocks().size(), &reason),
                 qPrintable(reason));

        QString tagged;
        QVERIFY2(page.probe(QStringLiteral("tag"),
                            "(function () {"
                            "  var s = document.querySelector("
                            "      'section.awb-block');"
                            "  s.dataset.probeTag = 'kept';"
                            "  return s.className;"
                            "})()",
                            &tagged),
                 "could not tag the first block");

        const QString before = page.state()->editorText();
        page.state()->setEditorText(before
                                    + QStringLiteral(
                                          "\n\nAn appended tail paragraph.\n"));
        QVERIFY2(page.state()->dirty(),
                 "the document must be dirty after editing");

        // updateBlocks 是队列里的那条命令：pumpCommands 之后页面才动
        page.pumpCommands();

        QString kept;
        QVERIFY2(page.probe(QStringLiteral("kept"),
                            "(document.querySelector('section.awb-block')"
                            " .dataset.probeTag || 'gone')",
                            &kept),
                 "the untouched block vanished instead of staying mounted");
        QCOMPARE(jsonTextValue(kept), QStringLiteral("kept"));

        QVERIFY2(page.probeOk(QStringLiteral("tail"),
                              "(function () {"
                              "  var miss = [];"
                              "  var all = document.querySelectorAll("
                              "      'section.awb-block');"
                              "  var last = all[all.length - 1];"
                              "  if (!/An appended tail paragraph/.test("
                              "          last.textContent)) {"
                              "    miss.push('appended text');"
                              "  }"
                              "  if (all.length !== AWB.stats().blocks) {"
                              "    miss.push('count');"
                              "  }"
                              "  return miss;"
                              "})()",
                              &reason),
                 qPrintable(reason));
    }

    /**
     * @brief 搜索、补渲染与序列化：三条「页面把结果交回 C++」的路
     *
     * find 的命中数经信标回来（小结果，走 awbmd://runtime/event），所以断言落在
     * 控制器的 findResult 信号上；renderAll 让延后的 mermaid 全部画完（导出件里
     * 没有占位符的前提）；serialize 的整篇 HTML 走 pumpCommands 的返回值 →
     * reportMarkup → markupReady，即产品导出的同一条路。
     */
    void testSearchRenderAllAndSerialize()
    {
        Page page;
        QString error;
        QVERIFY2(page.open(QString::fromUtf8(kFixture), &error),
                 qPrintable(error));

        QString reason;
        QVERIFY2(page.waitMounted(page.state()->blocks().size(), &reason),
                 qPrintable(reason));

        QSignalSpy found(page.state(),
                         &MarkdownPreviewController::findResult);
        QVERIFY(found.isValid());
        page.state()->requestFind(QStringLiteral("warning"), true, false,
                                  true);
        page.pumpCommands();
        // 信标是页面发的异步请求，等它落地而不是等信号计数
        QTRY_COMPARE_WITH_TIMEOUT(found.count(), 1, kBootTimeoutMs);
        const QList<QVariant> hit = found.takeFirst();
        QVERIFY2(hit.at(0).toInt() >= 1,
                 qPrintable(QStringLiteral("find total: ")
                            + hit.at(0).toString()));

        QString marks;
        QVERIFY2(page.probe(QStringLiteral("marks"),
                            "(document.querySelectorAll('.awb-mark').length)",
                            &marks),
                 "no find probe");
        QVERIFY2(marks != QLatin1String("0"),
                 qPrintable(QStringLiteral("marked hits: ") + marks));

        // 补渲染：mermaid.min.js 从 awbmd://runtime/vendor/ 取来并真的画出 SVG。
        // Chromium 87（Qt 5 车道）装不下 mermaid 11 的构建（post-87 语法，
        // polyfill 接不住）——那是文档化的降级：图表块落 error 占位 + 源码，
        // 其余一切照常。旧引擎只断言降级形态；新引擎断言真画出了 SVG。
        page.state()->requestFullRender();
        page.pumpCommands();
        // 回答里带一个自造的词「rendered-svg」而不是「svg」：状态串里
        // 'no-svg' 也含 svg，用后者做子串判据会把「没画成」读成「画成了」。
        QString status;
        const bool drewSvg = page.waitFor(
            QStringLiteral("diagram"),
            QStringLiteral(
                "(function () {"
                "  var el = document.querySelector('.awb-diagram');"
                "  return el ? (el.querySelector('svg')"
                "      ? 'rendered-svg'"
                "      : el.getAttribute('data-status')) : 'none';"
                "})()"),
            QStringLiteral("rendered-svg"), &status, kBootTimeoutMs);
        if (!drewSvg) {
            // 旧引擎车道（Chromium 87）：mermaid 11 装载即败（post-87 语法），
            // 图表降级为 error 占位 + 源码——断言降级形态真的出现，而不是
            // 连占位都没有。
            QString degraded;
            QVERIFY2(page.waitFor(
                         QStringLiteral("diagram-degraded"),
                         QStringLiteral(
                             "(function () {"
                             "  var el = document.querySelector("
                             "      '.awb-diagram');"
                             "  return el ? (el.getAttribute('data-status')"
                             "      === 'error' ? 'degraded'"
                             "      : el.getAttribute('data-status')) : 'none';"
                             "})()"),
                         QStringLiteral("degraded"), &degraded,
                         kBootTimeoutMs),
                     qPrintable(QStringLiteral("diagram: ") + status));
        }

        QSignalSpy markup(page.state(),
                          &MarkdownPreviewController::markupReady);
        QVERIFY(markup.isValid());
        page.state()->requestMarkup(QStringLiteral("export"));
        page.pumpCommands();
        QTRY_COMPARE_WITH_TIMEOUT(markup.count(), 1, kBootTimeoutMs);
        const QString body = markup.at(0).at(0).toString();
        QCOMPARE(markup.at(0).at(1).toString(), QStringLiteral("export"));

        QVERIFY2(body.contains(QStringLiteral("<!doctype html>")),
                 qPrintable(body.left(200)));
        QVERIFY2(body.contains(QStringLiteral("awb-export")),
                 "export must be tagged");
        QVERIFY2(body.contains(QStringLiteral(":root{")),
                 "export must inline the theme");
        QVERIFY2(body.contains(QStringLiteral("<math")),
                 "export must keep the MathML branch of KaTeX");
        QVERIFY2(!body.contains(QStringLiteral("awbmd://runtime/asset/")),
                 "export must rewrite asset URLs back to document-relative");
        // 判据一律带 class=" 前缀：theme.css 本身就含 .awb-diagram-hint 与
        // .awb-mark 的选择器，而导出件是要把整张样式表内联进去的——只数子串
        // 等于永远失败，且失败原因和页面无关。
        QVERIFY2(!body.contains(QStringLiteral("class=\"awb-diagram-hint\"")),
                 "export must not keep the click-to-render hint");
        QVERIFY2(!body.contains(QStringLiteral("class=\"awb-mark")),
                 "export must not carry search highlights");
    }

    /**
     * @brief 中段一块拆成两块：非连续批的参照点必须是「已挂载」的节点
     *
     * 2026-10 用户报告的渲染丢块：源码区编辑后页面顶部内容消失，页面侧报
     * Uncaught NotFoundError: Failed to execute 'insertBefore'。机制：一处
     * 中段编辑（删 1 块插 2 块）让 pending 里出现非连续下标（2、3），它们
     * 同一趟解析进 batch——flush 的乱序分支里 place(2) 向后找参照点时命中了
     * **同批未挂载**的 nodes[3]（已解析、还没上树），insertBefore 对非子节点
     * 抛 NotFoundError，整个 flush 宏任务被打断，batch 里的剩余块从此既不在
     * DOM 也不在 pending——永久丢失。修复让参照点核对 parentNode。
     *
     * 断言不依赖微观调度（无法保证「批里恰好攒着两个乱序块」）：编辑后等
     * 挂载数回到块总数，再核对每一段正文都在 DOM 里——丢块的表现正是
     * 「中间某段没了」，而 stats 的 mounted 只数 nodes[]，元素没上树它也
     * 照样数满，只有按文字找才暴露。
     */
    void testMidEditSplitKeepsEveryBlockMounted()
    {
        // 四块文档：中段（下标 2）将被拆成两块——removeCount=1、inserted=2，
        // 正是乱序 flush 的最小触发形状。
        const char *document = R"MD(# Head

First paragraph.

Second paragraph to be split.

Fourth paragraph.
)MD";
        Page page;
        QString error;
        QVERIFY2(page.open(QString::fromUtf8(document), &error),
                 qPrintable(error));

        QString reason;
        QVERIFY2(page.waitMounted(page.state()->blocks().size(), &reason),
                 qPrintable(reason));
        const int blocksBefore = page.state()->blocks().size();

        // 编辑面里把中段一分为二。
        const QString needle = QStringLiteral("Second paragraph to be split.");
        QString edited = page.state()->editorText();
        QVERIFY(edited.contains(needle));
        edited.replace(needle,
                       QStringLiteral("Second paragraph, part one.\n\n"
                                      "Second paragraph, part two."));
        page.state()->setEditorText(edited);
        page.pumpCommands();

        QVERIFY2(page.waitMounted(blocksBefore + 1, &reason),
                 qPrintable(reason));

        QVERIFY2(page.probeOk(QStringLiteral("allText"),
                              "(function () {"
                              "  var miss = [];"
                              "  var body = document.body.textContent;"
                              "  ['First paragraph.',"
                              "   'Second paragraph, part one.',"
                              "   'Second paragraph, part two.',"
                              "   'Fourth paragraph.'].forEach("
                              "      function (t) {"
                              "    if (body.indexOf(t) < 0) {"
                              "      miss.push(t);"
                              "    }"
                              "  });"
                              "  var mounted = document.querySelectorAll("
                              "      'section.awb-block').length;"
                              "  if (mounted !== AWB.stats().blocks) {"
                              "    miss.push('dom ' + mounted + ' != blocks '"
                              "               + AWB.stats().blocks);"
                              "  }"
                              "  return miss;"
                              "})()",
                              &reason),
                 qPrintable(reason));
    }

    /**
     * @brief 滚到最底不崩渲染进程（Chromium 87 的 content-visibility 合成缺陷）
     *
     * 2026-10 用户报告（Qt 5.15.16 / Chromium 87）：预览拉到最底部，渲染进程
     * 以 compositing_requirements_updater.cc 的 CHECK 失败整只终止——content-
     * visibility: auto 让屏外块被 display-lock 锁回，而 87 的合成需求更新器
     * 在锁回与合成状态同步上有缺陷，滚出视口的块一多就撞上 DCHECK。bridge.js
     * 现按引擎代次退掉旧引擎上的 content-visibility（html.awb-no-cv，见
     * theme.css）。本用例把现场钉住：长文档挂载完，滚到底、回顶、再到底，
     * 渲染进程必须全程应答（探针回话 + 死亡计数为零）。修复缺失时（旧引擎
     * + auto），程序化滚动驱动的是同一批视口相交转换，这一滚就是对实弹。
     */
    void testScrollToBottomKeepsRendererAlive()
    {
        // 混合形状：标题、段落、代码围栏、表格（README 的成分——用户崩的
        // 就是它）。96 块足够让「滚到底」真的滚过一批屏外块。
        QString text;
        for (int index = 0; index < 16; ++index) {
            text += QStringLiteral(
                        "## Section %1\n\n"
                        "Paragraph %1: the quick brown fox jumps over the "
                        "lazy dog while a renderer counts block hashes.\n\n"
                        "```cpp\nint f%1() { return %1; }\n```\n\n"
                        "| a | b |\n| - | - |\n| %1 | %1 |\n\n")
                        .arg(index);
        }

        Page page;
        QString error;
        QVERIFY2(page.open(text, &error), qPrintable(error));

        const int blocks = page.state()->blocks().size();
        QString reason;
        QVERIFY2(page.waitMounted(blocks, &reason), qPrintable(reason));

        // 滚动高度必须反映真实布局（每块至少 40px 的下限——含标题与表格，
        // 两种引擎上的真实布局都远高于它）。退化的现场：content-visibility
        // 挂在 auto 而引擎不认 contain-intrinsic-size 的 auto 关键字
        //（Chromium 87–97），锁死块零高度占位，整篇 96 块被压进一屏半——
        // 用户拖滚动条「永远拉不到真正的底」，87 还会在这条锁转换序列上
        // 崩渲染进程（见用例头注释）。
        QString heightJson;
        QVERIFY2(page.probe(QStringLiteral("height"),
                            QStringLiteral(
                                "document.documentElement.scrollHeight"),
                            &heightJson),
                 "the page did not answer the height probe");
        const int height = static_cast<int>(jsonTextValue(heightJson).toDouble());
        QVERIFY2(height >= blocks * 40,
                 qPrintable(QStringLiteral("scroll height %1 for %2 blocks "
                                           "means offscreen blocks take no "
                                           "space (content-visibility "
                                           "regression)")
                                .arg(height)
                                .arg(blocks)));

        // 滚动往返：每步都给合成一帧的时间——display-lock 的锁与解锁排在
        // 滚动后的渲染更新里，滚完立刻断言会把「还没崩」读成「不会崩」。
        const char *steps[] = {"document.documentElement.scrollHeight",
                               "0",
                               "document.documentElement.scrollHeight"};
        for (const char *step : steps) {
            QString answer;
            QVERIFY2(page.probe(QStringLiteral("scroll"),
                                QStringLiteral("window.scrollTo(0, %1); "
                                                "'scrolled'")
                                    .arg(QString::fromLatin1(step)),
                                &answer),
                     "the page did not answer the scroll probe");
            QTest::qWait(400);
        }

        // 崩了的 renderer：探针不再回话（上面已挡住一半），窗口的死亡
        // 计数把另一半也钉死。
        QString stats;
        QVERIFY2(page.probe(QStringLiteral("alive"), QStringLiteral(
                                "AWB.stats().blocks"), &stats),
                 "the renderer stopped answering after scrolling");
        QCOMPARE(page.rendererCrashes(), 0);
    }

    /**
     * @brief 大文档的挂载耗时（只在 AWB_MD_PERF=1 时跑）
     *
     * 它存在的原因是计划里的 S2：分片管线的收益必须能在一个几百 KB 的真实形状
     * 上被量出来，数字进日志、进 docs/research/markdown-preview-runtime.md。
     * 断言有两处，都不是「本机要多快」：一份文档最终必须挂完（管线不会卡在
     * 半路），以及挂完它的时间必须还在同一个量级里。时间不写成绝对值——那样
     * 换一台机器就红——只留一个十倍余量的粗上限，它防的是一件事：排期方式退回
     * 等帧、等空闲，那份文档重新变成几十秒，而「最终挂完了」照样绿。
     */
    void testLargeDocumentMounts()
    {
        if (qEnvironmentVariableIsEmpty("AWB_MD_PERF")) {
            QSKIP("set AWB_MD_PERF=1 to run the large-document timing case");
        }

        QString text;
        for (int index = 0; index < 3000; ++index) {
            if (index % 20 == 0) {
                // 单次 .arg() 会把所有 %1 一起填掉，多写一次是「Argument
                // missing」警告的来源（填不出第二个占位符）。
                text += QStringLiteral("## Section %1\n\n"
                                       "Body under heading %1.\n\n")
                            .arg(index);
                continue;
            }
            text += QStringLiteral(
                "Paragraph %1: the quick brown fox jumps over the lazy dog "
                "while a renderer counts block hashes and slices idle time "
                "into predictable frames for document number %1.\n\n")
                .arg(index);
        }
        const QByteArray utf8 = text.toUtf8();
        qInfo() << "markdown perf fixture:" << utf8.size() << "bytes";

        Page page;
        QString error;
        QElapsedTimer clock;
        clock.start();
        QVERIFY2(page.open(text, &error), qPrintable(error));

        // 挂完的判据是页面自己的计数，不是这边等了几秒
        QString reason;
        QVERIFY2(page.waitMounted(page.state()->blocks().size(), &reason),
                 qPrintable(reason));
        const int blocks = page.state()->blocks().size();
        const qint64 mountedMs = clock.elapsed();

        QString stats;
        QVERIFY(page.probe(QStringLiteral("stats"),
                           QStringLiteral("AWB.stats()"), &stats));
        qInfo() << "markdown perf:" << blocks << "blocks /" << utf8.size()
                << "bytes mounted in" << mountedMs << "ms, stats"
                << jsonTextValue(stats);

        // 这一条断言防的不是慢机器，是排期方式退回去：管线改回「每帧一片」或
        // 「等空闲」之后，这份文档会重新变成几十秒而不是几秒，而「最终挂完了」
        // 那种断言照样绿。留 10 倍余量，慢到十倍以下的机器不会碰到它。
        QVERIFY2(mountedMs < 30000,
                 qPrintable(QStringLiteral("%1 blocks mounted in %2 ms: the "
                                           "slicing schedule has regressed "
                                           "(see renderer.js)")
                                .arg(blocks)
                                .arg(mountedMs)));

        // 增量那一刀也要在大文档上量：改最后一块，等 diff 命令送出
        const qint64 editStart = clock.elapsed();
        page.state()->setEditorText(text + QStringLiteral("\nTail.\n"));
        page.pumpCommands();
        qInfo() << "markdown perf: appended one block,"
                << (clock.elapsed() - editStart) << "ms round trip";
    }
};

// 与 app/main.cpp 同序：测试模式 → awbmd:// 注册 → WebEngine 初始化 →
// QGuiApplication。注册晚于 initialize() 时引擎拒绝写入 scheme 表，provider
// 会把 available 落成假，本套件的每个用例都会在 open() 里报出原因。
//
// 三个 Chromium 开关都不是为了「跑快点」：本套件的探针窗在测试期间会被其它窗口
// 压住、也可能没有输入焦点，而 Chromium 对「被遮挡 / 后台」的渲染进程会冻住
// requestAnimationFrame 并降频定时器——renderer.js 的挂载收尾正好排在 rAF 上，
// 于是页面看起来永远空白。产品窗口由用户盯着看，不会有这个环境问题；测试里
// 把它关掉，测的才是页面逻辑而不是浏览器的省电策略。
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
    TestMarkdownPreview tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_markdownpreview.moc"
