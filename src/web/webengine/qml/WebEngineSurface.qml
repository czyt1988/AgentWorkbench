import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtWebEngine
import AgentWorkbench
import AgentWorkbench.App

// 单个 Web 标签的内嵌视图：WebEngineView + 各状态覆盖层。所有平台事件
// （弹窗、下载、全屏、崩溃、权限）都在这里处理——不允许「点了没反应」。
// Web 标签数据在 web.* 门面；profile 经 WebProfiles 按 agent 建立。
Item {
    id: surface

    // 本视图渲染的 WebTab（标签模型的一个 role 对象）。
    property var tab: null
    // 全屏会隐藏标签栏；Esc 先退出全屏。
    signal fullScreenToggled(bool active)

    // 是否有绑定标签（无标签时不渲染视图内容）。
    readonly property bool hasTab: tab !== null && tab !== undefined

    // 本次加载期间页面抛出的未捕获 JS 异常条数（onJavaScriptConsoleMessage
    // 统计；LoadStarted 清零）——白屏检测的证据之一。
    property int uncaughtErrors: 0

    // 白屏探测脚本：无可见文本且无媒体元素即视为空白。SPA 的根容器
    // （div#root 之类）永远存在，不能当「渲染过」的证据；反过来，
    // body 里的内联 <script>/<style> 源码会被 textContent 计入，也不能
    // 当「有内容」的证据——用 TreeWalker 只数可见文本节点。
    readonly property string blankProbeScript:
        "(function () {"
        + "var body = document.body;"
        + "if (!body)"
        + "    return JSON.stringify({ blank: true, ua: navigator.userAgent });"
        + "var walker = document.createTreeWalker("
        + "    body, NodeFilter.SHOW_TEXT, null);"
        + "var hasText = false;"
        + "var node;"
        + "while (!hasText && (node = walker.nextNode())) {"
        + "    var parent = node.parentElement;"
        + "    if (parent) {"
        + "        var tag = parent.tagName;"
        + "        if (tag === 'SCRIPT' || tag === 'STYLE'"
        + "            || tag === 'TEMPLATE' || tag === 'NOSCRIPT')"
        + "            continue;"
        + "    }"
        + "    hasText = node.textContent.replace(/\\s+/g, '').length > 0;"
        + "}"
        + "var hasMedia = !!body.querySelector('canvas, svg, img, video, iframe');"
        + "return JSON.stringify({"
        + "    blank: !hasText && !hasMedia, ua: navigator.userAgent });"
        + "})()"

    // 抹掉 URL 里的 token 值（?token= 与 #token= 两种拼写）——控制台
    // 转发日志里不允许出现明文 token（约定见 WebTabsFacade 的
    // redactedUrl）。
    function redactedSource(sourceID) {
        return String(sourceID).replace(/token=[^&#]*/g, "token=[redacted]")
    }

    WebEngineView {
        id: view
        anchors.fill: parent

        visible: surface.hasTab && tab.state !== "released"
                 && tab.state !== "offline"
        url: surface.hasTab ? tab.url : ""
        zoomFactor: surface.hasTab ? tab.zoom : 1.0
        profile: surface.hasTab
                 ? WebProfiles.createProfile(tab.agentId) : null

        // 内存策略：切换走的标签在加载稳定后冻结（会话保留）；超过
        // web.maxLiveTabs 时按 LRU 释放最旧的。旧条件踩错的两个坑：
        // 加载中的视图绝不能冻结——Frozen 会挂起页面，卡在隐藏
        // "loading" 的标签永远停在加载中，激活它们时会在已渲染的页面上
        // 闪一层加载覆盖层；ACTIVE 标签无论状态如何必须保持 Active——
        // 冻结它会被 Qt 以 "page is visible" 拒绝，每个错误态转换都报。
        // LifecycleState 是 scoped 枚举：裸写 WebEngineView.Active 是
        // undefined，赋值每次都静默失效。
        lifecycleState: {
            if (!surface.hasTab
                    || !web.freezeInactiveTabs
                    || web.activeTabId === tab.id
                    || tab.state === "loading")
                return WebEngineView.LifecycleState.Active
            return WebEngineView.LifecycleState.Frozen
        }

        onUrlChanged: {
            if (surface.hasTab)
                web.setTabUrl(tab.id, String(url))
        }
        // 标签状态机驱动加载：`loading` 表示「应当正在加载」。
        // reloadTab()/markOnlineForAgent() 只翻转状态、不碰 URL（上面的
        // url 绑定只在 URL 变化时触发），所以转到 loading 的实际加载必须
        // 从这里发起。没有这一步转圈会转个不停，覆盖层的「取消」也没有
        // 东西可停——空闲视图上的 view.stop() 永远不会发
        // LoadStoppedStatus。
        Connections {
            target: surface.tab
            function onStateChanged() {
                if (surface.hasTab && tab.state === "loading")
                    view.reload()
            }
        }
        onTitleChanged: {
            if (surface.hasTab)
                web.setTabTitle(tab.id, title)
        }
        onLoadProgressChanged: {
            if (surface.hasTab)
                web.setTabProgress(tab.id, loadProgress)
        }
        // Qt 6 没有 loadFinished——加载结果经 loadingChanged 的
        // LoadStatus 枚举（状态机）到达。
        onLoadingChanged: function(loadingInfo) {
            if (!surface.hasTab)
                return
            if (loadingInfo.status === WebEngineView.LoadStartedStatus) {
                // 新一轮加载开始：清零未捕获异常计数、撤掉待决的白屏
                // 检查（两者都只针对「本次加载」的结果）。
                surface.uncaughtErrors = 0
                blankCheckTimer.stop()
            } else if (loadingInfo.status === WebEngineView.LoadSucceededStatus) {
                web.setTabProgress(tab.id, 100)
                web.setTabState(tab.id, "ready")
                blankCheckTimer.restart()
            } else if (loadingInfo.status === WebEngineView.LoadFailedStatus) {
                blankCheckTimer.stop()
                console.error("WebEngine: failed to load",
                              String(loadingInfo.url),
                              "domain:", loadingInfo.errorDomain,
                              "code:", loadingInfo.errorCode)
                // 正的错误码是 HTTP 状态（如 token 门禁 harness 的
                // 401）；网络错误为负。
                web.setTabLastError(tab.id,
                    loadingInfo.errorCode > 0
                        ? qsTr("Failed to load %1 (HTTP %2)")
                              .arg(String(loadingInfo.url))
                              .arg(loadingInfo.errorCode)
                        : qsTr("Failed to load %1")
                              .arg(String(loadingInfo.url)))
                web.setTabState(tab.id, "error")
            } else if (loadingInfo.status === WebEngineView.LoadStoppedStatus) {
                // 主动停止（工具栏 ✕ / 覆盖层「取消」）也要落定状态机——
                // 否则转圈永远不停。
                blankCheckTimer.stop()
                web.setTabState(tab.id, "ready")
            }
        }
        // --- JS 控制台：连接本信号后引擎不再走默认的 [js] 日志路由
        // （两版都是检测到 receivers 即 return），日志转发由这里自己
        // 负责，且 sourceID 必须先脱敏——默认路由会把 #token=/?token=
        // 原样写进日志文件。Info 级不转发：默认路由的 js 日志分类缺省
        // 级别就是 Warning，保持同样的日志量。
        // 未捕获异常（"Uncaught ..." 前缀）另行计数，作为白屏检测的
        // 证据——HTTP 200 但脚本全挂的页面（旧引擎跑新 bundle）状态机
        // 停在 ready，不加检测就是一张没有任何提示的空白页。
        onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {
            if (level === WebEngineView.InfoMessageLevel)
                return
            const where = surface.redactedSource(sourceID) + ":" + lineNumber
            if (level === WebEngineView.ErrorMessageLevel)
                console.error("[js]", where, message)
            else
                console.warn("[js]", where, message)
            if (/^Uncaught\b/i.test(message))
                surface.uncaughtErrors += 1
        }
        // 渲染进程崩溃后绝不自动重载——崩溃循环比手动重载更糟。
        onRenderProcessTerminated: function(status, exitCode) {
            console.error("WebEngine: render process terminated", status,
                          exitCode)
            if (surface.hasTab) {
                web.setTabLastError(tab.id, qsTr("The render process was terminated (code %1)").arg(exitCode))
                web.setTabState(tab.id, "crashed")
            }
        }

        // --- 弹窗：Qt 大版本间 new-window 信号名不同（Qt 6
        // newWindowRequested / Qt 5 newViewRequested），请求对象的应答
        // 方式也不同——QML 两个名字都不能声明（另一版本会因未知属性拒绝
        // 整个表面的加载，即白标签 bug）。由 compat 对象在 C++ 侧连对的
        // 信号并以 popupRequested 转发；路由逻辑在表面根部的 Connections。
        //
        // --- polyfill：旧引擎兼容脚本同样经 compat 注入（Qt 5 的 view 级
        // userScripts；Qt 6 在 profile 级、此调用为空操作）。必须在完成
        // 阶段调：Qt 5 的适配器初始化经 singleShot(0) 排在其后，此时追加
        // 仍赶得上首次加载。
        Component.onCompleted: {
            WebEngineCompat.watchPopups(view)
            WebEngineCompat.installCompatScript(view)
        }

        // --- 全屏：接受请求，让页面隐藏自己的标签栏。请求对象在两版上
        // 都是同形状的 gadget——toggleOn（方向）+ accept()（应答）。此前
        // 的 request.accepted / request.fullScreen 名字两版都不存在，
        // 全屏在运行时静默失效。
        onFullScreenRequested: function(request) {
            request.accept()
            surface.fullScreenToggled(request.toggleOn)
        }

        // --- JS alert()/confirm()/prompt()：引擎会阻塞页面 JS 直到应答。
        // 不处理时 Qt 弹自己的对话框，但那种对话框若被以一种永不应答的
        // 方式关掉，之后的所有交互都静默卡死。自己处理：主题化对话框
        // 永远应答（OK / 取消），页面不可能一直被阻塞。
        onJavaScriptDialogRequested: function(request) {
            request.accepted = true
            jsDialog.request = request
            jsDialog.open()
        }

        // --- HTTP Basic 认证（opencode 的登录提示）：与 JS 对话框同一
        // 形状——永远应答，「取消」即拒绝。
        onAuthenticationDialogRequested: function(request) {
            request.accepted = true
            authDialog.request = request
            authDialog.open()
        }

        // --- 页面的 window.close() 关闭标签（页面自己要求的）。此前该
        // 请求被静默忽略，卡住的页面无法关掉。
        onWindowCloseRequested: {
            if (surface.hasTab) {
                web.closeTab(tab.id)
            }
        }

        // --- 权限：v1 一律拒绝并给可见提示。
        // denyFeature 经 WebEngineCompat 收口：两版都是
        // grantFeaturePermission(origin, feature, false)，无需分支。
        onFeaturePermissionRequested: function(securityOrigin, feature) {
            WebEngineCompat.denyFeature(view, securityOrigin, feature)
            workbench.notify("warning", qsTr("Permission denied"),
                             qsTr("This page requested a browser permission; the current version does not support it."))
        }
    }

    // --- 弹窗路由 -------------------------------------------------------
    // compat 侧已应答/丢弃引擎请求；URL 的去向在这里决定：回环地址 →
    // 应用内新标签，其余 → 系统浏览器。每个标签一个表面，只处理属于
    // 本表面 view 的请求。
    Connections {
        target: WebEngineCompat

        function onPopupRequested(sourceView, target) {
            if (sourceView !== view || !surface.hasTab)
                return
            const targetUrl = String(target)
            const hostMatch = /^https?:\/\/([^\/?#:]+)/.exec(targetUrl)
            const host = hostMatch ? hostMatch[1].toLowerCase() : ""
            const loopback = host === "127.0.0.1" || host === "localhost"
                             || host === "::1" || host.startsWith("127.")
            if (loopback) {
                web.openDetachedTab(tab.agentId, targetUrl, host)
            } else {
                workbench.openExternalUrl(targetUrl)
            }
        }
    }

    // --- 白屏兜底 ---------------------------------------------------------
    // HTTP 层成功但页面脚本抛过未捕获异常、且延迟检查时 body 仍是空白
    // （SPA 没起来——典型场景：旧引擎解析不了新语法的 bundle，polyfill
    // 救不了语法级缺口），把标签落到 error 态。否则用户面对一张零提示
    // 的空白页（状态机停在 ready，dsh 在 Chromium 87 上的实际症状），
    // error 覆盖层自带「在浏览器中打开」的外置出口。
    // 仅在「有过未捕获异常」时检查：正常空白页（如刚启动的服务）不受
    // 影响，部分渲染成功但有零星报错的页面也不会被覆盖层盖住。
    Timer {
        id: blankCheckTimer
        interval: 3000
        onTriggered: {
            if (!surface.hasTab || surface.uncaughtErrors === 0
                    || tab.state !== "ready")
                return
            view.runJavaScript(surface.blankProbeScript, function(result) {
                let info = null
                try {
                    info = JSON.parse(result)
                } catch (e) {
                    return
                }
                if (!info || !info.blank || !surface.hasTab
                        || tab.state !== "ready")
                    return
                const chrome = /Chrome\/(\d+)/.exec(String(info.ua))
                web.setTabLastError(tab.id, qsTr("The page stayed blank because its scripts failed to run. The embedded browser engine (Chromium %1) is too old for this page; open it in an external browser.").arg(chrome ? chrome[1] : "?"))
                web.setTabState(tab.id, "error")
            })
        }
    }

    // --- JS alert()/confirm()/prompt() 对话框 --------------------------------
    // 引擎默认对话框的主题化替代。要点：打开期间页面的 JS 被阻塞，按钮
    // 必须永远应答请求——经对话框自身的关闭途径（如 closePolicy）关掉
    // 也必须拒绝，绝不能把请求悬着。所以 closePolicy 是 NoAutoClose，
    // 只由这两个按钮落定。
    Popup {
        id: jsDialog

        // 待应答的引擎请求（dialogAccept/dialogReject 必须恰好调用一次）。
        property var request: null
        // alert() 没有取消键；prompt() 有输入框。
        readonly property bool isAlert:
            request && request.type === JavaScriptDialogRequest.DialogTypeAlert
        readonly property bool isPrompt:
            request && request.type === JavaScriptDialogRequest.DialogTypePrompt

        // 应答并关闭：accept 时 prompt 回传输入框内容；随后清空请求。
        function settle(accept) {
            if (!request)
                return
            if (accept) {
                if (isPrompt)
                    request.dialogAccept(promptField.text)
                else
                    request.dialogAccept()
            } else {
                request.dialogReject()
            }
            request = null
            close()
        }

        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        width: 380
        padding: theme.spacingL

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.accent
            border.width: 1
            radius: theme.radiusOverlay
        }

        onOpened: {
            promptField.text = request ? request.defaultText : ""
            if (isPrompt)
                promptField.forceActiveFocus()
        }

        ColumnLayout {
            width: jsDialog.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: jsDialog.request ? jsDialog.request.title : ""
                color: theme.accent
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: jsDialog.request ? jsDialog.request.message : ""
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            TextField {
                id: promptField
                Layout.fillWidth: true
                visible: jsDialog.isPrompt
                color: theme.textPrimary
                placeholderTextColor: theme.textMuted
                font.pixelSize: theme.fontSizeBody
                background: Rectangle {
                    radius: theme.radiusControl
                    color: theme.surfaceAltBg
                    border.color: promptField.activeFocus
                                  ? theme.focusRing : theme.borderSubtle
                    border.width: promptField.activeFocus ? 2 : 1
                }
                onAccepted: jsDialog.settle(true)
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Item { Layout.fillWidth: true }
                AButton {
                    visible: !jsDialog.isAlert
                    text: qsTr("Cancel")
                    onClicked: jsDialog.settle(false)
                }
                AButton {
                    variant: "primary"
                    // confirm() 语义：OK 返回 true。
                    text: qsTr("OK")
                    onClicked: jsDialog.settle(true)
                }
            }
        }
    }

    // --- HTTP Basic / 代理认证 ----------------------------------------------
    // opencode 式登录：引擎阻塞页面直到拿到凭据。与 JS 对话框同一条
    // 规则：永远应答。
    Popup {
        id: authDialog

        // 待应答的引擎请求。
        property var request: null

        // 应答并关闭：accept 回传用户名/密码，否则拒绝。
        function settle(accept) {
            if (!request)
                return
            if (accept)
                request.dialogAccept(userField.text, passwordField.text)
            else
                request.dialogReject()
            request = null
            close()
        }

        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        width: 380
        padding: theme.spacingL

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.accent
            border.width: 1
            radius: theme.radiusOverlay
        }

        onOpened: {
            userField.text = ""
            passwordField.text = ""
            userField.forceActiveFocus()
        }

        ColumnLayout {
            width: authDialog.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Sign in")
                color: theme.accent
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: authDialog.request && authDialog.request.realm.length > 0
                      ? qsTr("The site \"%1\" requires authentication.")
                        .arg(authDialog.request.realm)
                      : qsTr("This site requires authentication.")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            TextField {
                id: userField
                Layout.fillWidth: true
                placeholderText: qsTr("Username")
                color: theme.textPrimary
                placeholderTextColor: theme.textMuted
                font.pixelSize: theme.fontSizeBody
                background: Rectangle {
                    radius: theme.radiusControl
                    color: theme.surfaceAltBg
                    border.color: userField.activeFocus
                                  ? theme.focusRing : theme.borderSubtle
                    border.width: userField.activeFocus ? 2 : 1
                }
                onAccepted: passwordField.forceActiveFocus()
            }
            TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                color: theme.textPrimary
                placeholderTextColor: theme.textMuted
                font.pixelSize: theme.fontSizeBody
                background: Rectangle {
                    radius: theme.radiusControl
                    color: theme.surfaceAltBg
                    border.color: passwordField.activeFocus
                                  ? theme.focusRing : theme.borderSubtle
                    border.width: passwordField.activeFocus ? 2 : 1
                }
                onAccepted: authDialog.settle(true)
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Item { Layout.fillWidth: true }
                AButton {
                    text: qsTr("Cancel")
                    onClicked: authDialog.settle(false)
                }
                AButton {
                    variant: "primary"
                    text: qsTr("Sign in")
                    onClicked: authDialog.settle(true)
                }
            }
        }
    }

    // --- 独立窗口的 DevTools（仅 Debug 构建） -----------------------------
    // 挂接经 WebEngineCompat.attachDevTools：给 devToolsView 设
    // inspectedView，它随后自行加载检查器前端（两版同构，地址不参与）。
    Window {
        id: devToolsWindow
        width: 900
        height: 600
        visible: false
        title: qsTr("Developer tools")
        color: theme.windowBg

        WebEngineView {
            id: devToolsView
            anchors.fill: parent
            url: devToolsWindow.visible ? WebEngineCompat.devToolsUrl(view)
                                        : ""
        }
    }

    // 打开 DevTools 窗口（WebTabsPage 工具栏的调试入口调用）。
    function openDevTools() {
        WebEngineCompat.attachDevTools(view, devToolsView)
        devToolsWindow.show()
        devToolsWindow.raise()
        devToolsWindow.requestActivate()
    }

    // 工具栏的「停止加载」（⟳ 重载 / ✕ 停止加载）。
    function stopLoading() {
        view.stop()
    }

    // --- 下载：两个 Qt 版本的信号都在 profile 上；条目类型 Qt 6 叫
    // WebEngineDownloadRequest、Qt 5 叫 WebEngineDownloadItem，状态枚举
    // 常量经 WebEngineCompat 取。一律接受，落 web.downloadDir。
    Connections {
        target: view.profile
        enabled: view.profile !== null
        function onDownloadRequested(download) {
            download.downloadDirectory = web.downloadDir
            download.accept()
            workbench.notify("info", qsTr("Download started"),
                             download.downloadFileName)
            download.stateChanged.connect(function() {
                if (download.state === WebEngineCompat.downloadCompleted) {
                    workbench.notify("success", qsTr("Download finished"),
                                     String(download.path))
                } else if (download.state === WebEngineCompat.downloadCancelled
                           || download.state
                              === WebEngineCompat.downloadInterrupted) {
                    workbench.notify("warning", qsTr("Download interrupted"),
                                     download.downloadFileName)
                }
            })
        }
    }

    // --- 状态覆盖层 ---------------------------------------------------------
    // loading / offline / crashed / error 四态的居中提示与动作按钮。
    Rectangle {
        id: overlay
        anchors.fill: parent
        visible: surface.hasTab && (tab.state === "loading"
                                    || tab.state === "offline"
                                    || tab.state === "crashed"
                                    || tab.state === "error")
        color: theme.alpha(theme.windowBg, 0.7)

        ColumnLayout {
            anchors.centerIn: parent
            spacing: theme.spacingM
            width: Math.min(parent.width - 2 * theme.spacingXl, 460)

            // loading 态：转圈 + 目标地址提示。 ---------------------------
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: tab.state === "loading"
                running: visible
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: tab.state === "loading"
                text: qsTr("Loading %1...").arg(String(tab.url).replace(/^https?:\/\//, "").split("/")[0])
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: tab.state === "offline"
                text: qsTr("This agent is not running")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: tab.state === "offline"
                text: String(tab.url).replace(/#.*$/, "")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                font.family: theme.monoFamily
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: tab.state === "crashed"
                text: qsTr("The page crashed")
                color: theme.danger
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: tab.state === "error"
                text: qsTr("Failed to load the page")
                color: theme.danger
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: (tab.state === "crashed" || tab.state === "error")
                         && tab.lastError.length > 0
                text: tab.lastError
                color: theme.textSecondary
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.WrapAnywhere
                horizontalAlignment: Text.AlignHCenter
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: theme.spacingS

                AButton {
                    visible: tab.state === "loading"
                    text: qsTr("Cancel")
                    onClicked: view.stop()
                }
                AButton {
                    visible: tab.state === "offline"
                    variant: "primary"
                    text: qsTr("Restart agent")
                    onClicked: workbench.launchAgent(tab.agentId)
                }
                AButton {
                    visible: tab.state === "offline" || tab.state === "error"
                    text: qsTr("Retry")
                    onClicked: web.reloadTab(tab.id)
                }
                AButton {
                    visible: tab.state === "crashed" || tab.state === "error"
                    text: qsTr("Reload")
                    onClicked: web.reloadTab(tab.id)
                }
                AButton {
                    visible: tab.state === "crashed" || tab.state === "error"
                    text: qsTr("Open in browser")
                    onClicked: web.openExternal(tab.id)
                }
            }
        }
    }
}
