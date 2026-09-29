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
    readonly property var ui: Ui
    readonly property var toasts: Notifications
    readonly property var agents: Agents
    readonly property var web: Web
    readonly property var skills: Skills
    readonly property var tools: Tools
    readonly property var workbench: Workbench
    readonly property var environment: Environment

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

    onClosing: function(close) {
        shell.saveWindowSize(width, height)
        if (exitConfirmed)
            return
        if (agents.hasLaunchedAgents()) {
            close.accepted = false
            exitConfirmPopup.open()
        }
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
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                Layout.fillWidth: false
                Layout.fillHeight: true
                collapsed: shell.sidebarCollapsed
            }

            Workspace {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        StatusBar {
            Layout.fillWidth: true
        }
    }

    // toast 宿主：锚定在窗口右下角（逻辑在它自己的文件里）。
    Toasts {}

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
                        agents.stopAll()
                        exitConfirmPopup.close()
                        window.exitConfirmed = true
                        window.close()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("No, just exit")
                    onClicked: {
                        exitConfirmPopup.close()
                        window.exitConfirmed = true
                        window.close()
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
