import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgentWorkbench
import AgentWorkbench.App

// The window skeleton: sidebar + workspace + status bar,
// global shortcuts, exit confirmation and the toast overlay.
//
// Root aliases bridge the uppercase singleton type names to the lowercase
// contract names — every descendant resolves `theme.`,
// `nav.` … through this root.
ApplicationWindow {
    id: window

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
    // Brand name, deliberately not translated.
    title: shell.windowTitle.length > 0 ? shell.windowTitle
                                        : "AgentWorkbench"
    color: theme.windowBg

    // 全局字体经 window 向所有 Controls2 子控件传播（字体继承链的根）。
    // theme.family 空串（用户清空且主题未指定）时绑 undefined，恢复系统
    // 默认继承；主题切换/设置改动经 theme.changed 实时生效。应用启动时的
    // 默认值由 main.cpp 的 QGuiApplication::setFont 铺底（引擎创建前）。
    font.family: theme.family.length > 0 ? theme.family : undefined

    // Set to true once the user confirmed the exit dialog, so onClosing
    // lets the window close without re-prompting.
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

    // --- Global shortcuts ---------------------------------
    Shortcut {
        sequence: "Ctrl+B"
        onActivated: shell.sidebarCollapsed = !shell.sidebarCollapsed
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: workbench.showPage("settings")
    }
    // Ctrl+1…9 switch to the Nth page in order.
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

    // --- Layout ----------------------------------------------------------
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

    // Toasts anchors itself bottom-right inside its own file.
    Toasts {}

    // --- Exit confirmation (0.3.0 behaviour preserved) --------------------
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

    // --- Legacy data import notice (one-shot) --------------------
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
