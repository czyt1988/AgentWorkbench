import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The window skeleton (specs/02 §2): sidebar + workspace + status bar,
// global shortcuts, exit confirmation and the toast overlay.
//
// Root aliases bridge the uppercase singleton type names to the lowercase
// contract names (specs/01 §8.2) — every descendant resolves `theme.`,
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
    readonly property var workbench: Workbench
    readonly property var environment: Environment

    visible: true
    width: shell.windowWidth
    height: shell.windowHeight
    minimumWidth: 1024
    minimumHeight: 640
    // Brand name, deliberately not translated (02-ui-specification.md §13).
    title: shell.windowTitle.length > 0 ? shell.windowTitle
                                        : "AgentWorkbench"
    color: theme.windowBg

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

    // --- Global shortcuts (specs/02 §11) ---------------------------------
    Shortcut {
        sequence: "Ctrl+B"
        onActivated: shell.sidebarCollapsed = !shell.sidebarCollapsed
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: workbench.showPage("settings")
    }
    // Ctrl+1…9 switch to the Nth page in order (specs/02 §11).
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
    Popup {
        id: exitConfirmPopup
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 440
        padding: theme.spacingL
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.accent
            border.width: 1
            radius: theme.radiusOverlay
        }

        ColumnLayout {
            width: exitConfirmPopup.availableWidth
            spacing: theme.spacingM

            Label {
                text: qsTr("Confirm Exit")
                color: theme.accent
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
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

    // --- Legacy data import notice (one-shot, 01 §7.3) --------------------
    Popup {
        id: legacyImportPopup
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 460
        padding: theme.spacingL
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.accent
            border.width: 1
            radius: theme.radiusOverlay
        }

        Component.onCompleted: {
            if (workbench.legacyImportNotice.length > 0)
                open()
        }

        ColumnLayout {
            width: legacyImportPopup.availableWidth
            spacing: theme.spacingM

            Label {
                text: qsTr("Configuration imported")
                color: theme.accent
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: workbench.legacyImportNotice
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            AButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                onClicked: legacyImportPopup.close()
            }
        }
    }
}
