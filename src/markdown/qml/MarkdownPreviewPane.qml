import QtQuick
import QtWebEngine
import AgentWorkbench
import AgentWorkbench.App

// Markdown 预览渲染面：WebEngineView 与它身上那套引擎侧信号处理。
//
// 从 MarkdownPreviewWindow.qml 抽出来的原因：工作台窗口（workbench 域的
// MarkdownWorkbenchWindow.qml）要在没有 WebEngine 的构建里照常加载（提示词
// 模式是纯 QML 编辑器），而 `import QtWebEngine` 在那些构建里会让整份文件
// 解析失败——所以 WebEngine 相关的一切集中在本文件，它只在 Qt 6 + WebEngine
// 的构建里登记进资源清单，窗口经 Loader 按资源 URL 惰性装载（与
// WebEngineSurface.qml 同一条「按编译内容裁剪能力」的路线）。
//
// 与窗口的接触面刻意收窄为三个：dispatch()/evalJs()（把 JS 送进页面）、
// pageReady（页面加载完成且 AWB 可用的闸门）、loadingFailed 信号（提示
// toast 由窗口发，这里不重复）。窗口侧的命令泵、导出轮询都经这三个口。
Item {
    id: previewPane

    /// 页面加载完成且 AWB 可用：命令泵的唯一闸门。在这之前发出去的
    /// dispatch 会撞上 ReferenceError，而那条错误在 QML 侧是无声的。
    property bool pageReady: false

    /// 页面加载失败（窗口据此发 toast；这里只报事实）
    signal loadingFailed(string url, int errorCode)

    /// 把一条命令 JSON 交给页面执行。callback 可空；serialize 一类需要
    /// 返回值的命令在 callback 里接 reply。回调恒为函数：runJavaScript
    /// 传 null 的行为没承诺过，不拿它当「无回调」用。
    function dispatch(json, callback) {
        const sink = callback === undefined ? function () {} : callback
        view.runJavaScript("AWB.dispatch(" + json + ")", sink)
    }

    /// 在页面里求值一段 JS 表达式（搜索选区、空闲探测用）。
    function evalJs(expression, callback) {
        view.runJavaScript(expression, callback)
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
        // 拦导航用 navigationRequested。请求对象上只有 accept() /
        // reject() 两个 Q_INVOKABLE（Qt 6.7 的 QWebEngineNavigationRequest）：
        // 既没有 Widgets 那边的 linkClicked 信号，也没有 ignore()——前者写成
        // 信号处理器是整份文件加载失败（界面只表现为右键没反应），后者写成
        // 调用是运行时的 TypeError，而异常会让请求按默认值放行，导航照样
        // 发生，所以这类缺陷在界面上根本看不出来。
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
            if (target.indexOf("awbmd:") === 0) {
                request.accept()
                return
            }
            request.reject()
            if (target.indexOf("http:") === 0
                    || target.indexOf("https:") === 0
                    || target.indexOf("mailto:") === 0)
                workbench.openExternalUrl(target)
        }

        onLoadingChanged: function (info) {
            if (info.status === WebEngineView.LoadSucceededStatus) {
                previewPane.pageReady = true
                return
            }
            if (info.status === WebEngineView.LoadStartedStatus) {
                previewPane.pageReady = false
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
        onJavaScriptConsoleMessage: function (level, message,
                                             lineNumber, sourceID) {
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
