import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgentWorkbench
import AgentWorkbench.App

// 窗口骨架：侧栏 + 工作区 + 状态栏，全局快捷键、退出确认与 toast 覆盖层
// 也都在这里组装。
//
// 根部的小写别名把大写的单例类型名桥接成 QML 侧的小写契约名——所有后代
// 都经这个根解析 `theme.`、`nav.` …。
ApplicationWindow {
    id: window

    // 根部别名组：QML 契约定死的小写名字（theme/nav/shell/ui/toasts/
    // agents/web/skills/tools/workbench/environment），见 AGENTS.md。
    readonly property var theme: Theme
    readonly property var nav: Nav
    readonly property var shell: Shell
    readonly property var tray: Tray
    readonly property var ui: Ui
    readonly property var toasts: Notifications
    readonly property var agents: Agents
    readonly property var web: Web
    readonly property var skills: Skills
    readonly property var tools: Tools
    readonly property var workbench: Workbench
    readonly property var environment: Environment
    // Markdown 预览的状态门面（每个构建都在，能力位由 available 决定）。
    readonly property var markdown: Markdown

    // 工作台窗口的开关。窗口本体归 workbench 域（提示词 + 预览合体后的
    // 独立窗），装载条件不含 markdown.available——提示词模式是纯 QML
    // 编辑器，没有 WebEngine 的构建里照常可用，预览面由窗口按能力位
    // 自己裁剪。唯一置真的来源是两条意图信号（预览文档 / 打开工作台），
    // 唯一置假的来源是 markdown.active 变假（窗口自己报到关闭）。窗口
    // 活不长——关掉即由 Loader 销毁，WebEngineView 是渲染进程与合成层
    // 的入口，常驻会让每次启动都付这份钱。
    property bool markdownWindowOpen: false
    // 本轮开窗的意图：previewMarkdown() 给 "document"（主窗 Loader 的
    // onLoaded 写进窗口的 initialPreset），openMarkdownWorkbench() 给
    // "prompt"。窗口自身在提示词/文档两种预设间切换另有入口。
    property string pendingWorkbenchMode: "prompt"

    visible: true
    // 恢复的窗口尺寸按可用屏幕区域钳制：在大屏上保存过的窗口换到小屏打开
    // 会超出屏幕，被裁掉的下缘（状态栏、页面尾部）永远够不着。
    width: Math.min(shell.windowWidth, Screen.desktopAvailableWidth)
    height: Math.min(shell.windowHeight, Screen.desktopAvailableHeight)
    minimumWidth: 1024
    // 低到足以容纳缩小的逻辑高度（小屏、高 DPI）：页面内部自己滚动，窗口
    // 只需要装得下外壳。
    minimumHeight: 540
    // 品牌名，刻意不翻译。
    title: shell.windowTitle.length > 0 ? shell.windowTitle
                                        : "AgentWorkbench"
    color: theme.windowBg

    // 全局字体经 window 向所有 Controls2 子控件传播（字体继承链的根）。
    // theme.family 空串（用户清空且主题未指定）时绑 undefined，恢复系统
    // 默认继承；主题切换/设置改动经 theme.changed 实时生效。应用启动时的
    // 默认值由 main.cpp 的 QGuiApplication::setFont 铺底（引擎创建前）。
    font.family: theme.family.length > 0 ? theme.family : undefined

    // 用户在退出确认弹窗里点过「退出」后置真，onClosing 据此放行关闭、
    // 不再重复弹窗。
    property bool exitConfirmed: false

    // 关闭选择未记住时，× 先显示一个二选一对话框。记住后才按保存的动作
    // 分流；退出路径仍保留后台终端确认。托盘可用期间 main.cpp 关掉了
    // quitOnLastWindowClosed，真正的退出必须经 exitNow() 显式结束事件循环。
    function exitNow(stopAgents) {
        if (stopAgents)
            agents.stopAll()
        exitConfirmed = true
        close()
        workbench.quit()
    }

    function requestExit() {
        if (agents.hasLaunchedAgents())
            exitConfirmPopup.open()
        else
            exitNow(false)
    }

    function chooseCloseAction(hideToTray) {
        if (closeChoicePopup.rememberChoice) {
            shell.closeToTray = hideToTray
            shell.rememberCloseAction = true
        }
        closeChoicePopup.close()
        if (hideToTray)
            tray.hideToTray()
        else
            requestExit()
    }

    // 托盘菜单点「退出」：与点窗口 × 走同一条路（含后台终端确认）。窗口
    // 还收在托盘里就先唤回，确认弹窗才有地方显示。
    Connections {
        target: tray
        function onQuitRequested() {
            if (tray.hidden)
                tray.showWindow()
            if (!exitConfirmed && agents.hasLaunchedAgents()) {
                exitConfirmPopup.open()
            }
            else {
                window.exitNow(false)
            }
        }
    }

    onClosing: function(close) {
        shell.saveWindowSize(width, height)
        if (exitConfirmed)
            return
        if (tray.available && !shell.rememberCloseAction) {
            close.accepted = false
            closeChoicePopup.open()
            return
        }
        if (tray.canHideToTray()) {
            close.accepted = false
            tray.hideToTray()
            return
        }
        if (agents.hasLaunchedAgents()) {
            close.accepted = false
            exitConfirmPopup.open()
            return
        }
        // 无需确认的普通关闭也必须显式退出：托盘在场时
        // quitOnLastWindowClosed 已被 main.cpp 关掉，放行 close 只会收掉
        // 窗口而留下事件循环（仅托盘可达）。托盘不可用时再调 quit() 无害。
        workbench.quit()
    }

    // --- 全局快捷键 -----------------------------------------
    Shortcut {
        sequence: "Ctrl+B"
        onActivated: shell.sidebarCollapsed = !shell.sidebarCollapsed
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: workbench.showPage("settings")
    }
    // Ctrl+1…9 按模型顺序切到第 N 个页面（钉底页排在最后，属预期行为）。
    function goToPageNumber(n) {
        const ids = nav.pageIdsInOrder()
        if (n >= 0 && n < ids.length)
            nav.setCurrentPageId(ids[n])
    }
    Shortcut { sequence: "Ctrl+1"; onActivated: window.goToPageNumber(0) }
    Shortcut { sequence: "Ctrl+2"; onActivated: window.goToPageNumber(1) }
    Shortcut { sequence: "Ctrl+3"; onActivated: window.goToPageNumber(2) }
    Shortcut { sequence: "Ctrl+4"; onActivated: window.goToPageNumber(3) }
    Shortcut { sequence: "Ctrl+5"; onActivated: window.goToPageNumber(4) }
    Shortcut { sequence: "Ctrl+6"; onActivated: window.goToPageNumber(5) }
    Shortcut { sequence: "Ctrl+7"; onActivated: window.goToPageNumber(6) }
    Shortcut { sequence: "Ctrl+8"; onActivated: window.goToPageNumber(7) }
    Shortcut { sequence: "Ctrl+9"; onActivated: window.goToPageNumber(8) }

    // --- 布局 --------------------------------------------------------------
    // 侧栏整条通到窗口底部（钉底的系统页/折叠手柄贴住窗口底边）；状态栏
    // 只占工作区列、从侧栏右缘开始，不再横跨全宽。
    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillWidth: false
            Layout.fillHeight: true
            collapsed: shell.sidebarCollapsed
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Workspace {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            StatusBar {
                Layout.fillWidth: true
            }
        }
    }

    // toast 宿主：锚定在窗口右下角（逻辑在它自己的文件里）。
    Toasts {}

    // --- Markdown 工作台窗口（按需创建，关掉即销毁） -------------------------
    // 挂在这里而不是页面里：Loader 是主窗的内联对象，被它创建的窗口因此继承
    // 主窗的 QML 上下文，窗口文件里的 `theme` / `markdown` / `tools` /
    // `workbench` 别名才解析得到（与各页面用别名的方式一致）。
    //
    // 用 source（URL 字符串）而不是 sourceComponent（类型名）：窗口里惰性
    // 装载的预览面与命令条同理——那些 .qml 只在部分构建里登记进模块，
    // 写成类型名会在其余构建里留下一个解析不到的引用；字符串的代价要到
    // 真正加载时才付，由窗口按能力位自己处理装载失败。
    Loader {
        id: markdownWindowLoader
        active: window.markdownWindowOpen
        source: "qrc:/qt/qml/AgentWorkbench/workbench/MarkdownWorkbenchWindow.qml"
        onLoaded: item.initialPreset = window.pendingWorkbenchMode
        // 与 Workspace 的页面 Loader 同一做法：加载失败要喊出来。这里失败的表现
        // 不是白屏而是什么都没弹出来——右键点了没反应，比一个错误弹窗难查得多。
        onStatusChanged: {
            if (status === Loader.Error) {
                console.error("workbench: failed to load the workbench window",
                              source)
                workbench.notify("error", qsTr("Workbench failed to open"),
                                 String(source))
                // 失败的 Loader 留在 active 上没有意义：它没有内容，而
                // markdown.active 也永远不会变真，下一次请求同样不开窗。
                window.markdownWindowOpen = false
            }
        }
    }

    Connections {
        target: markdown

        // 预览文档的请求：开窗落在文档模式（文档由控制器先行装载）。
        function onWindowRequested() {
            window.pendingWorkbenchMode = "document"
            window.markdownWindowOpen = true
        }

        // 窗口报到关闭 / 销毁 → 放下 Loader，连带释放渲染进程那一侧的一切。
        function onActiveChanged() {
            if (!markdown.active)
                window.markdownWindowOpen = false
        }
    }

    Connections {
        target: workbench

        // 打开工作台的请求（启动页主按钮）：开窗落在提示词模式；窗口已
        // 开着时由窗口自己接管（raise + 切预设）。
        function onMarkdownWorkbenchRequested() {
            window.pendingWorkbenchMode = "prompt"
            window.markdownWindowOpen = true
        }
    }

    // --- 窗口关闭选择 --------------------------------------------------------
    ADialog {
        id: closeChoicePopup
        width: 440
        titleText: qsTr("Close AgentWorkbench?")
        property bool rememberChoice: false

        onClosed: rememberChoice = false

        ColumnLayout {
            width: closeChoicePopup.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Do you want to exit or keep AgentWorkbench running in the tray?")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            CheckBox {
                text: qsTr("Remember my choice")
                // Basic/Default 样式的标签色取自系统调色板，与应用主题无关：
                // 深色主题 + 系统浅色时是黑字黑底，显式钉到主题令牌上。
                palette.windowText: theme.textPrimary
                checked: closeChoicePopup.rememberChoice
                onToggled: closeChoicePopup.rememberChoice = checked
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Exit")
                    onClicked: window.chooseCloseAction(false)
                }
                AButton {
                    Layout.fillWidth: true
                    variant: "primary"
                    text: qsTr("Minimize to Tray")
                    onClicked: window.chooseCloseAction(true)
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: closeChoicePopup.close()
                }
            }
        }
    }

    // --- 退出确认（保留 0.3.0 行为） ----------------------------------------
    ADialog {
        id: exitConfirmPopup
        width: 440
        closePolicy: Popup.NoAutoClose
        titleText: qsTr("Confirm Exit")

        ColumnLayout {
            width: exitConfirmPopup.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Background terminals were launched via AgentWorkbench this session. Close them before exiting?")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                AButton {
                    Layout.fillWidth: true
                    variant: "primary"
                    text: qsTr("Yes, close background terminals")
                    onClicked: {
                        exitConfirmPopup.close()
                        window.exitNow(true)
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("No, just exit")
                    onClicked: {
                        exitConfirmPopup.close()
                        window.exitNow(false)
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: exitConfirmPopup.close()
                }
            }
        }
    }

    // --- 旧数据导入提示（一次性） -------------------------------------------
    AAlertDialog {
        id: legacyImportPopup
        danger: false
        width: 460
        titleText: qsTr("Configuration imported")
        message: workbench.legacyImportNotice
        dismissText: qsTr("OK")

        Component.onCompleted: {
            if (workbench.legacyImportNotice.length > 0)
                open()
        }
    }
}
