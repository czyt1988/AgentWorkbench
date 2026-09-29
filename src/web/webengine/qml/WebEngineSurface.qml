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
            if (loadingInfo.status === WebEngineView.LoadSucceededStatus) {
                web.setTabProgress(tab.id, 100)
                web.setTabState(tab.id, "ready")
            } else if (loadingInfo.status === WebEngineView.LoadFailedStatus) {
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
                web.setTabState(tab.id, "ready")
            }
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

        // --- 弹窗：回环地址 → 应用内新标签；其余 → 系统浏览器。
        // request.accepted 必须置位，否则请求静默失败。
        onNewWindowRequested: function(request) {
            request.accepted = true
            const target = String(request.requestedUrl)
            const hostMatch = /^https?:\/\/([^\/?#:]+)/.exec(target)
            const host = hostMatch ? hostMatch[1].toLowerCase() : ""
            const loopback = host === "127.0.0.1" || host === "localhost"
                             || host === "::1" || host.startsWith("127.")
            if (loopback) {
                web.openDetachedTab(tab.agentId, target, host)
            } else {
                workbench.openExternalUrl(target)
            }
        }

        // --- 全屏：接受请求，让页面隐藏自己的标签栏。
        onFullScreenRequested: function(request) {
            request.accepted = true
            surface.fullScreenToggled(request.fullScreen)
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
            if (surface.hasTab)
                web.closeTab(tab.id)
        }

        // --- 权限：v1 一律拒绝并给可见提示。
        // denyFeature 经 WebEngineCompat：Qt 6 是 view.rejectFeature，
        // Qt 5 是 grantFeaturePermission(origin, feature, false)。
        onFeaturePermissionRequested: function(securityOrigin, feature) {
            WebEngineCompat.denyFeature(view, securityOrigin, feature)
            workbench.notify("warning", qsTr("Permission denied"),
                             qsTr("This page requested a browser permission; the current version does not support it."))
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
    // devToolsUrl/devToolsView 两版名字不同，经 WebEngineCompat 取版本中立
    // 的地址；Qt 5 那边另有 attachDevTools 挂接检查器视图。
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
                font.pixelSize: theme.fontSizeSmall
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
                font.pixelSize: theme.fontSizeSmall
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
