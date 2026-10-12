import QtQuick
import QtWebEngine
import AgentWorkbench
import AgentWorkbench.App

// Markdown 预览渲染面：WebEngineView 与它身上那套引擎侧信号处理。
//
// 从 MarkdownPreviewWindow.qml 抽出来的原因：工作台窗口（workbench 域的
// MarkdownWorkbenchWindow.qml）要在没有 WebEngine 的构建里照常加载（提示词
// 模式是纯 QML 编辑器），而 `import QtWebEngine` 在那些构建里会让整份文件
// 解析失败——所以 WebEngine 相关的一切集中在本文件，它只在带 WebEngine 的
// 构建里登记进资源清单，窗口经 Loader 按资源 URL 惰性装载（与
// WebEngineSurface.qml 同一条「按编译内容裁剪能力」的路线）。
//
// 与窗口的接触面刻意收窄为三个：dispatch()/evalJs()（把 JS 送进页面）、
// pageReady（页面加载完成且 AWB 可用的闸门）、loadingFailed 信号（提示
// toast 由窗口发，这里不重复）。窗口侧的命令泵、导出轮询都经这三个口。
//
// 数据通道按引擎分两道（见 bridge.js 头注释）：fetch 可用的引擎
// （Qt ≥ 6.6，FetchApiAllowed）页面自己拉 doc.json；fetch 不可用的
// （Qt 5 与 Qt 6.2–6.5——fetch 对自定义 scheme 直接拒绝）走**推入车道**：
// 这里在页面装载后经 runJavaScript 把文档与样式表文本推进页面
//（AWB.push），信标（ready/frame/error/find）也改走 console 打点
// （AWBEVT: 前缀，onJavaScriptConsoleMessage 解析后交给控制器），因为
// /event 的 GET 像素请求本身也是一次 fetch。
Item {
    id: previewPane

    /// 页面加载完成且 AWB 可用：命令泵的唯一闸门。在这之前发出去的
    /// dispatch 会撞上 ReferenceError，而那条错误在 QML 侧是无声的。
    property bool pageReady: false

    // 本引擎车道上页面是否走推入（fetch 对自定义 scheme 不可用）。车道由
    // 页面自探（boot 试一次 fetch 即知）——QML 轮询 AWB.channel() 读结论，
    // 读到 "push" 才投递。不能用回调：runJavaScript 的回调在旧引擎上等
    // 不到 Promise。
    property bool pushChannel: false

    /// 页面加载失败（窗口据此发 toast；这里只报事实）
    signal loadingFailed(string url, int errorCode)

    /// 把一条命令 JSON 交给页面执行。callback 可空；serialize 一类需要
    /// 返回值的命令在 callback 里接 reply。回调恒为函数：runJavaScript
    /// 传 null 的行为没承诺过，不拿它当「无回调」用。
    function dispatch(json, callback) {
        const sink = callback === undefined ? function () {} : callback
        // 推入车道的 setDocument 只带 rev（fetch 车道页面自己拉新正文），
        // 这里先把新载荷推进缓存，页面侧 reload 才解析得到最新正文。
        // 命令名经 JSON 解析取，与页面侧命令泵同一条纪律（子串匹配会被
        // 序列化的 spacing 变化静默击穿）。
        if (pushChannel) {
            try {
                const cmd = JSON.parse(json).cmd
                if (cmd === "setDocument")
                    pushDocument()
            } catch (e) {
                // 队列里进了解析不了的命令：页面侧会经 dispatch 的
                // unknown-command 分支自己报错，这里不重复。
            }
        }
        view.runJavaScript("AWB.dispatch(" + json + ")", sink)
    }

    /// 在页面里求值一段 JS 表达式（搜索选区、空闲探测用）。
    function evalJs(expression, callback) {
        view.runJavaScript(expression, callback)
    }

    // 把一段文本安全地嵌进 JS 字符串字面量（推入车道用；body 是 JSON 或
    // CSS，都可能带引号与换行）。
    function jsLiteral(text) {
        return JSON.stringify(String(text))
    }

    // 推入车道的一次完整投递：样式表 + 文档载荷 → AWB.push。fetch 车道
    // 页面没人调 push（探测结果为「有 fetch」时这里也不推）。
    function pushDocument() {
        if (!pushChannel)
            return
        view.runJavaScript(
                    "AWB.push(" + jsLiteral("stylesheet") + ", "
                    + jsLiteral(markdown.pageStylesheetText()) + ");"
                    + "AWB.push(" + jsLiteral("document") + ", "
                    + jsLiteral(markdown.documentBodyJson()) + ")")
    }

    // console 信标（推入车道）：解析 AWBEVT:<kind>:<payload> 并交给控制器
    // 的对应入口——与 handler 的 /event 路由同一张表（kind 集合见
    // MarkdownRuntime::eventKinds）。
    function forwardConsoleBeacon(text) {
        const marker = "AWBEVT:"
        if (text.indexOf(marker) !== 0)
            return false
        const rest = text.substring(marker.length)
        const split = rest.indexOf(":")
        if (split < 0)
            return false
        const kind = rest.substring(0, split)
        const payload = rest.substring(split + 1)
        if (kind === "ready")
            markdown.reportReady(payload)
        else if (kind === "frame")
            markdown.reportFrame(payload)
        else if (kind === "error")
            markdown.reportError(payload)
        else if (kind === "find")
            markdown.reportFindResult(payload)
        return true
    }

    // 车道轮询：LoadSucceeded 后每 100ms 问一次 AWB.channel()，结论定格
    // 即停。probe 期（""）继续等；"push" 标记车道并立即投递。
    Timer {
        id: lanePoll

        interval: 100
        repeat: true
        onTriggered: {
            evalJs("AWB.channel()", function (lane) {
                const answer = String(lane)
                if (answer.length === 0)
                    return
                lanePoll.stop()
                if (answer === "push") {
                    previewPane.pushChannel = true
                    previewPane.pushDocument()
                }
            })
        }
    }

    WebEngineView {
        id: view

        // objectName 是 tst_markdownpreviewui 的定位锚点：测试要从窗口
        // 对象图里找到这块视图驱动页面。窗口迁移后名字不变。
        objectName: "markdownPreviewView"
        anchors.fill: parent

        url: markdown.pageUrl()
        profile: MarkdownPreview.previewProfile()

        // 页面自己就是渲染器：脚本必须开。写明而不是靠默认值，因为
        // 「脚本被禁」的表现是正文一片空白，而 C++ 侧一切照常。
        settings.javascriptEnabled: true

        // 预览页没有「切走就冻结」的语义：它要么在屏上，要么整栏被
        // 源码模式挤掉。Frozen 会让分片管线与图表观察器一起停摆，
        // 切回来时用户看到半篇文档。
        lifecycleState: WebEngineView.LifecycleState.Active

        // 链接一律不由本视图导航：它加载的是随包的 shell.html，跟着
        // 链接走等于把渲染面换成别人的页面。站内锚点同样不支持——
        // 标题不生成 slug id，理由见 markdown-config.js。
        //
        // 拦导航用 navigationRequested（信号两版同名）。应答面随大版本
        // 不同：Qt 6 的请求对象是 accept()/reject() 方法，Qt 5 是 action
        // 属性——QML 里直接调方法在 Qt 5 上是运行时 TypeError，而异常会让
        // 请求按默认值放行，导航照样发生，所以应答一律经 WebEngineCompat.
        // respondNavigation 收口（两版形状差异见 WebEngineCompat.cpp）。
        //
        // url 要按字符串比：这个类型没注册成 QML 值类型，QML 递过来的
        // url 是字符串，`request.url.scheme` 一律 undefined——拿它判前缀
        // 会把我们自己的 shell.html 也当成外链拒掉，视图永远空白
        // （`url` 属性赋值同样走这条信号，所以判据必须放行 awbmd）。
        //
        // 判据是白名单：只有 http/https/mailto 值得交给外部浏览器，
        // 其余（file:、javascript:、data: 与一切未知 scheme）一律拒掉
        // 且不转发——本地文档里写什么链接，都不该由这扇窗替用户执行。
        onNavigationRequested: function (request) {
            const target = String(request.url)
            const isOurs = target.indexOf("awbmd:") === 0
            WebEngineCompat.respondNavigation(request, isOurs)
            if (isOurs)
                return
            if (target.indexOf("http:") === 0
                    || target.indexOf("https:") === 0
                    || target.indexOf("mailto:") === 0)
                workbench.openExternalUrl(target)
        }

        onLoadingChanged: function (info) {
            if (info.status === WebEngineView.LoadSucceededStatus) {
                previewPane.pageReady = true
                // 车道轮询：页面自己试一次 fetch 并把结论定格在
                // AWB.channel()（"" = 还在探）。读到 "push" 就投递——
                // 页面侧 push 到来即启动。
                lanePoll.restart()
                return
            }
            if (info.status === WebEngineView.LoadStartedStatus) {
                previewPane.pageReady = false
                lanePoll.stop()
                return
            }
            if (info.status === WebEngineView.LoadFailedStatus) {
                console.error("markdown: preview page failed to load",
                              String(info.url), "code:", info.errorCode)
                previewPane.loadingFailed(String(info.url), info.errorCode)
            }
        }

        // JS 控制台：连接此信号后引擎不再走默认的 [js] 日志路由
        // （检测到有接收者即 return），转发由这里负责。没有 token 要
        // 抹（本地文档，URL 里不带凭证）；Info 级不转发，与 Web 页
        // 同一口径——默认路由的 js 日志分类缺省就是 Warning。
        // AWBEVT: 前缀是推入车道的信标（fetch 车道的信标走 /event 请求，
        // 不出现在 console 里），先于普通日志处理。
        onJavaScriptConsoleMessage: function (level, message,
                                             lineNumber, sourceID) {
            if (previewPane.forwardConsoleBeacon(String(message)))
                return
            if (level === WebEngineView.InfoMessageLevel)
                return
            console.warn("[markdown js]", String(sourceID) + ":"
                         + lineNumber, message)
        }

        // 渲染进程死了不自动重载：重载会再死一次，崩溃循环比手动
        // 重开这一窗更糟。
        onRenderProcessTerminated: function (status, exitCode) {
            console.error("markdown: render process terminated",
                          status, exitCode)
            workbench.notify("error", qsTr("Preview crashed"),
                             qsTr("The renderer was terminated. Reopen the file to try again."))
        }
    }
}
