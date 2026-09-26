import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App

ApplicationWindow {
    id: window

    // QML-facing name for the registered Theme singleton: Qt only accepts
    // uppercase singleton type names, the token contract stays `theme.*`
    // (specs/01 §8.2). Children resolve `theme` through this root alias.
    readonly property var theme: Theme

    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    // Brand name, deliberately not translated (see 02-ui-specification.md §13).
    title: "AgentWorkbench"

    color: theme.windowBg

    // Set to true when the user has already confirmed the exit dialog, so
    // onClosing lets the window close without re-prompting.
    property bool exitConfirmed: false

    onClosing: function(close) {
        if (exitConfirmed)
            return
        if (agents.hasLaunchedAgents()) {
            close.accepted = false
            exitConfirmPopup.open()
        }
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: homePage
    }

    // Home: title + responsive grid of agent cards.
    Component {
        id: homePage

        ScrollView {
            id: scrollView
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: scrollView.availableWidth
                spacing: theme.spacingS

                // Header row: title + subtitle on the left, runtime version
                // badges (Python / Node.js) on the right.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: theme.spacingXl
                    Layout.rightMargin: theme.spacingXl
                    Layout.topMargin: theme.spacingXl
                    spacing: theme.spacingM

                    ColumnLayout {
                        spacing: 0

                        Label {
                            text: qsTr("Agent Launcher")
                            color: theme.textPrimary
                            font.pixelSize: theme.fontSizePageTitle
                            font.bold: true
                            Layout.bottomMargin: theme.spacingS
                        }

                        Label {
                            text: qsTr("Launch AI coding agents and open their web UI")
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeBody
                            Layout.bottomMargin: theme.spacingS
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Runtime version badges
                    Row {
                        spacing: theme.spacingS
                        Layout.alignment: Qt.AlignTop | Qt.AlignRight

                        // Python badge
                        Rectangle {
                            id: pythonBadge
                            readonly property bool installed: agents.pythonInstalled
                            readonly property string version: agents.pythonVersion
                            radius: theme.radiusPill
                            implicitWidth: pyBadgeLayout.implicitWidth + 20
                            implicitHeight: 24
                            color: theme.badgeBg
                            border.color: installed ? theme.borderSubtle : theme.danger
                            border.width: 1

                            RowLayout {
                                id: pyBadgeLayout
                                anchors.centerIn: parent
                                spacing: theme.spacingXs

                                Text {
                                    text: "Python"
                                    color: theme.textMuted
                                    font.pixelSize: theme.fontSizeSmall
                                    font.bold: true
                                }
                                Text {
                                    text: pythonBadge.installed ? pythonBadge.version : "\u00D7"
                                    color: pythonBadge.installed ? theme.success : theme.danger
                                    font.pixelSize: theme.fontSizeSmall
                                    font.bold: true
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                ToolTip.text: pythonBadge.installed
                                    ? qsTr("Python %1").arg(pythonBadge.version)
                                    : qsTr("Python is not installed or not in PATH. Agents requiring Python may not work.")
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 300
                            }
                        }

                        // Node.js badge
                        Rectangle {
                            id: nodeBadge
                            readonly property bool installed: agents.nodeInstalled
                            readonly property string version: agents.nodeVersion
                            radius: theme.radiusPill
                            implicitWidth: nodeBadgeLayout.implicitWidth + 20
                            implicitHeight: 24
                            color: theme.badgeBg
                            border.color: installed ? theme.borderSubtle : theme.danger
                            border.width: 1

                            RowLayout {
                                id: nodeBadgeLayout
                                anchors.centerIn: parent
                                spacing: theme.spacingXs

                                Text {
                                    text: "Node"
                                    color: theme.textMuted
                                    font.pixelSize: theme.fontSizeSmall
                                    font.bold: true
                                }
                                Text {
                                    text: nodeBadge.installed ? nodeBadge.version : "\u00D7"
                                    color: nodeBadge.installed ? theme.success : theme.danger
                                    font.pixelSize: theme.fontSizeSmall
                                    font.bold: true
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                ToolTip.text: nodeBadge.installed
                                    ? qsTr("Node.js %1").arg(nodeBadge.version)
                                    : qsTr("Node.js is not installed or not in PATH. Agents requiring Node.js may not work.")
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 300
                            }
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: theme.spacingL
                    Layout.rightMargin: theme.spacingL
                    Layout.bottomMargin: theme.spacingXl
                    spacing: theme.spacingL

                    Repeater {
                        model: agents.model
                        delegate: AgentCard {
                            width: theme.cardMinWidth
                            onConfigureRequested: function(id) {
                                stack.push(agentEditPageComp, { "agentId": id })
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: settingsPageComp
        SettingsPage {}
    }

    Component {
        id: agentEditPageComp
        AgentEditPage {}
    }

    // Floating settings entry in the bottom-right corner. Hidden on sub-pages
    // (Settings / Edit) so it doesn't overlap their bottom-right buttons.
    Button {
        id: settingsButton
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: theme.spacingL
        width: 44
        height: 44
        visible: stack.depth === 1

        ToolTip.visible: hovered
        ToolTip.delay: 300
        ToolTip.text: qsTr("Settings")

        background: Rectangle {
            radius: theme.radiusOverlay
            color: parent.down ? theme.surfaceAltBg : (parent.hovered ? theme.surfaceHoverBg : theme.surfaceBg)
            border.color: theme.borderSubtle
        }
        contentItem: Image {
            source: "qrc:/icons/gear.svg"
            sourceSize: Qt.size(22, 22)
            fillMode: Image.PreserveAspectFit
        }
        onClicked: stack.push(settingsPageComp)
    }

    // One-shot notice shown on the first start after the legacy AgentLauncher
    // data directory was adopted into ~/.AgentWorkbench. Empty otherwise.
    Popup {
        id: legacyImportPopup
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 460
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.accent
            border.width: 1
            radius: theme.radiusOverlay
        }

        Component.onCompleted: {
            if (legacyImportNotice.length > 0)
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
                text: legacyImportNotice
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.surfaceAltBg : (parent.hovered ? theme.surfaceHoverBg : theme.surfaceBg); border.color: theme.borderSubtle }
                contentItem: Label { text: parent.text; color: theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: legacyImportPopup.close()
            }
        }
    }

    // Exit confirmation: shown when the user closes the window while one or
    // more agents were started from the launcher this session.
    Popup {
        id: exitConfirmPopup
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 440
        padding: 20
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

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Yes, close background terminals")
                    background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.pressed(theme.accent) : (parent.hovered ? theme.hover(theme.accent) : theme.accent) }
                    contentItem: Label { text: parent.text; color: theme.windowBg; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        agents.stopAll()
                        exitConfirmPopup.close()
                        window.exitConfirmed = true
                        window.close()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("No, just exit")
                    background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.surfaceAltBg : (parent.hovered ? theme.surfaceHoverBg : theme.surfaceBg) }
                    contentItem: Label { text: parent.text; color: theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        exitConfirmPopup.close()
                        window.exitConfirmed = true
                        window.close()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.surfaceAltBg : (parent.hovered ? theme.surfaceHoverBg : theme.surfaceBg) }
                    contentItem: Label { text: parent.text; color: theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: exitConfirmPopup.close()
                }
            }
        }
    }

    // Central error display for launch/stop failures. The matching card also
    // flashes red (see AgentCard.qml) for at-place feedback.
    Popup {
        id: errorPopup
        property string message: ""
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 500
        height: Math.min(errorColumn.implicitHeight + 2 * errorPopup.padding, 400)
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.danger
            border.width: 1
            radius: theme.radiusOverlay
        }

        ColumnLayout {
            id: errorColumn
            width: errorPopup.availableWidth
            spacing: theme.spacingM

            Label {
                text: qsTr("Launch failed")
                color: theme.danger
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Label {
                    Layout.fillWidth: true
                    text: errorPopup.message
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                    font.family: theme.monoFamily
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.surfaceAltBg : (parent.hovered ? theme.surfaceHoverBg : theme.surfaceBg); border.color: theme.borderSubtle }
                contentItem: Label { text: parent.text; color: theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: errorPopup.close()
            }
        }
    }

    Connections {
        target: agents
        function onLaunchFailed(id, message) {
            errorPopup.message = message
            errorPopup.open()
        }
        function onInstallFinished(id, success, message) {
            if (!success) {
                errorPopup.message = message
                errorPopup.open()
            }
        }
    }
}
