import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    // Brand name, deliberately not translated (see 02-ui-specification.md §13).
    title: "AgentWorkbench"

    color: "#1e1e2e"

    // Set to true when the user has already confirmed the exit dialog, so
    // onClosing lets the window close without re-prompting.
    property bool exitConfirmed: false

    onClosing: function(close) {
        if (exitConfirmed)
            return
        if (launcher.hasLaunchedAgents()) {
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
                spacing: 8

                // Header row: title + subtitle on the left, runtime version
                // badges (Python / Node.js) on the right.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: 24
                    spacing: 12

                    ColumnLayout {
                        spacing: 0

                        Label {
                            text: qsTr("Agent Launcher")
                            color: "#cdd6f4"
                            font.pixelSize: 26
                            font.bold: true
                            Layout.bottomMargin: 8
                        }

                        Label {
                            text: qsTr("Launch AI coding agents and open their web UI")
                            color: "#7f849c"
                            font.pixelSize: 13
                            Layout.bottomMargin: 8
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Runtime version badges
                    Row {
                        spacing: 8
                        Layout.alignment: Qt.AlignTop | Qt.AlignRight

                        // Python badge
                        Rectangle {
                            id: pythonBadge
                            readonly property bool installed: launcher.pythonInstalled
                            readonly property string version: launcher.pythonVersion
                            radius: 11
                            implicitWidth: pyBadgeLayout.implicitWidth + 20
                            implicitHeight: 24
                            color: "#313244"
                            border.color: installed ? "#45475a" : "#f38ba8"
                            border.width: 1

                            RowLayout {
                                id: pyBadgeLayout
                                anchors.centerIn: parent
                                spacing: 4

                                Text {
                                    text: "Python"
                                    color: "#7f849c"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                Text {
                                    text: pythonBadge.installed ? pythonBadge.version : "\u00D7"
                                    color: pythonBadge.installed ? "#a6e3a1" : "#f38ba8"
                                    font.pixelSize: 11
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
                            readonly property bool installed: launcher.nodeInstalled
                            readonly property string version: launcher.nodeVersion
                            radius: 11
                            implicitWidth: nodeBadgeLayout.implicitWidth + 20
                            implicitHeight: 24
                            color: "#313244"
                            border.color: installed ? "#45475a" : "#f38ba8"
                            border.width: 1

                            RowLayout {
                                id: nodeBadgeLayout
                                anchors.centerIn: parent
                                spacing: 4

                                Text {
                                    text: "Node"
                                    color: "#7f849c"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                Text {
                                    text: nodeBadge.installed ? nodeBadge.version : "\u00D7"
                                    color: nodeBadge.installed ? "#a6e3a1" : "#f38ba8"
                                    font.pixelSize: 11
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
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 24
                    spacing: 20

                    Repeater {
                        model: agentModel
                        delegate: AgentCard {
                            width: 260
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
        anchors.margins: 20
        width: 44
        height: 44
        visible: stack.depth === 1

        ToolTip.visible: hovered
        ToolTip.delay: 300
        ToolTip.text: qsTr("Settings")

        background: Rectangle {
            radius: 12
            color: parent.down ? "#45475a" : (parent.hovered ? "#4a4d62" : "#313244")
            border.color: "#45475a"
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
            color: "#1e1e2e"
            border.color: "#89b4fa"
            border.width: 1
            radius: 12
        }

        Component.onCompleted: {
            if (legacyImportNotice.length > 0)
                open()
        }

        ColumnLayout {
            width: legacyImportPopup.availableWidth
            spacing: 14

            Label {
                text: qsTr("Configuration imported")
                color: "#89b4fa"
                font.pixelSize: 16
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: legacyImportNotice
                color: "#cdd6f4"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                background: Rectangle { radius: 8; color: parent.down ? "#45475a" : (parent.hovered ? "#4a4d62" : "#313244"); border.color: "#45475a" }
                contentItem: Label { text: parent.text; color: "#cdd6f4"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
            color: "#1e1e2e"
            border.color: "#89b4fa"
            border.width: 1
            radius: 12
        }

        ColumnLayout {
            width: exitConfirmPopup.availableWidth
            spacing: 14

            Label {
                text: qsTr("Confirm Exit")
                color: "#89b4fa"
                font.pixelSize: 16
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Background terminals were launched via AgentWorkbench this session. Close them before exiting?")
                color: "#cdd6f4"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Yes, close background terminals")
                    background: Rectangle { radius: 8; color: parent.down ? Qt.darker("#89b4fa", 1.3) : (parent.hovered ? Qt.darker("#89b4fa", 1.15) : "#89b4fa") }
                    contentItem: Label { text: parent.text; color: "#1e1e2e"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        launcher.stopAll()
                        exitConfirmPopup.close()
                        window.exitConfirmed = true
                        window.close()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("No, just exit")
                    background: Rectangle { radius: 8; color: parent.down ? "#45475a" : (parent.hovered ? "#4a4d62" : "#313244") }
                    contentItem: Label { text: parent.text; color: "#cdd6f4"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        exitConfirmPopup.close()
                        window.exitConfirmed = true
                        window.close()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    background: Rectangle { radius: 8; color: parent.down ? "#45475a" : (parent.hovered ? "#4a4d62" : "#313244") }
                    contentItem: Label { text: parent.text; color: "#cdd6f4"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
            color: "#1e1e2e"
            border.color: "#f38ba8"
            border.width: 1
            radius: 12
        }

        ColumnLayout {
            id: errorColumn
            width: errorPopup.availableWidth
            spacing: 14

            Label {
                text: qsTr("Launch failed")
                color: "#f38ba8"
                font.pixelSize: 16
                font.bold: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Label {
                    Layout.fillWidth: true
                    text: errorPopup.message
                    color: "#cdd6f4"
                    font.pixelSize: 12
                    font.family: "Consolas, Monaco, monospace"
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                background: Rectangle { radius: 8; color: parent.down ? "#45475a" : (parent.hovered ? "#4a4d62" : "#313244"); border.color: "#45475a" }
                contentItem: Label { text: parent.text; color: "#cdd6f4"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: errorPopup.close()
            }
        }
    }

    Connections {
        target: launcher
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
