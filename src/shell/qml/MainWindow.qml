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
    // 本轮退出已过「脏文档」关（保存或放弃过一次）：requestExit 只放行
    // 一轮，取消退出后由 exitNow 复位。
    property bool exitDocsDiscarded: false

    // 关闭选择未记住时，× 先显示一个二选一对话框。记住后才按保存的动作
    // 分流；退出路径仍保留后台终端确认。托盘可用期间 main.cpp 关掉了
    // quitOnLastWindowClosed，真正的退出必须经 exitNow() 显式结束事件循环。
    function exitNow(stopAgents) {
        if (stopAgents)
            agents.stopAll()
        exitConfirmed = true
        exitDocsDiscarded = false
        close()
        workbench.quit()
    }

    // 脏的文件会话数（草稿自动落盘不算脏；只数文件 tab）。工作台页收进
    // 主窗后，退出前有未保存文档的守卫由这里承担——页是常驻的，没有自己
    // 的 onClosing 可挡。
    function markdownDirtyCount() {
        const tabs = markdown.tabs
        let count = 0
        for (let i = 0; i < tabs.length; ++i) {
            if (tabs[i].dirty && !tabs[i].draft)
                ++count
        }
        return count
    }

    function requestExit() {
        // 本轮退出已过「脏文档」关（放弃或已保存）才不再拦。
        if (!exitDocsDiscarded && markdownDirtyCount() > 0) {
            unsavedDocsDialog.open()
            return
        }
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

    // 托盘菜单点「退出」：与点窗口 × 走同一条路（含脏文档与后台终端的
    // 两道确认）。窗口还收在托盘里就先唤回，确认弹窗才有地方显示。
    Connections {
        target: tray
        function onQuitRequested() {
            if (tray.hidden)
                tray.showWindow()
            window.requestExit()
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
        // 剩下的退出路径（后台终端确认 / 无需确认的普通关闭）统一经
        // requestExit：脏文档守卫在它里面，不在这里再判一遍。无需确认的
        // 关闭也必须显式退出——托盘在场时 quitOnLastWindowClosed 已被
        // main.cpp 关掉，放行 close 只会收掉窗口而留下事件循环（仅托盘
        // 可达）。托盘不可用时再调 quit() 无害。
        close.accepted = false
        window.requestExit()
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

    // --- 脏文档守卫（工作台页的文件 tab 未保存时的退出拦截） -----------------
    // 「保存 / 放弃 / 取消」三态：AConfirmDialog 只有两个按钮，所以直接基于
    // ADialog 写（与退出确认同形，见 designs.md）。守卫覆盖**所有**脏的文件
    // 会话，不只是激活的那个——静默丢掉一个后台 tab 的改动是这个应用能犯的
    // 最贵的错。工作台还是独立窗口的时期这条守卫在它自己的 onClosing 里；
    // 页面化之后统一收进主窗的退出路径。
    ADialog {
        id: unsavedDocsDialog
        width: 440
        closePolicy: Popup.NoAutoClose
        titleText: qsTr("Unsaved changes")

        property var dirtyTabs: {
            const dirty = []
            const tabs = markdown.tabs
            for (let i = 0; i < tabs.length; ++i) {
                if (tabs[i].dirty && !tabs[i].draft)
                    dirty.push(tabs[i])
            }
            return dirty
        }

        // 逐个按 id 存，不换激活（中途失败就地报因，剩下的不丢——用户
        // 取消退出还能补救）。
        function saveAllDirty() {
            let allOk = true
            for (let i = 0; i < dirtyTabs.length; ++i) {
                const result = markdown.saveSession(dirtyTabs[i].docId)
                if (!result.ok) {
                    allOk = false
                    workbench.notify(
                        "error", qsTr("Could not save"),
                        dirtyTabs[i].title + " — " + result.error)
                }
            }
            return allOk
        }

        ColumnLayout {
            width: unsavedDocsDialog.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: unsavedDocsDialog.dirtyTabs.length === 1
                      ? qsTr("Save the changes to %1 before exiting?")
                            .arg(unsavedDocsDialog.dirtyTabs[0].title)
                      : qsTr("%1 documents have unsaved changes. Save them before exiting?")
                            .arg(unsavedDocsDialog.dirtyTabs.length)
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
                    text: unsavedDocsDialog.dirtyTabs.length === 1
                          ? qsTr("Save and exit")
                          : qsTr("Save all and exit")
                    onClicked: {
                        unsavedDocsDialog.close()
                        if (unsavedDocsDialog.saveAllDirty())
                            window.requestExit()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Discard changes")
                    onClicked: {
                        window.exitDocsDiscarded = true
                        unsavedDocsDialog.close()
                        window.requestExit()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: unsavedDocsDialog.close()
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
