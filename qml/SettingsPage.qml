import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Settings entry: launcher management. Structured as sections inside a
// scrollable column so more settings can be added below "Launchers" later.
Page {
    id: page
    background: Rectangle { color: "#1e1e2e" }

    function openEditor(agentId) {
        page.StackView.view.push(agentEditComp, { "agentId": agentId })
    }

    Component {
        id: agentEditComp
        AgentEditPage {}
    }

    // --- Delete confirmation -----------------------------------------------
    Popup {
        id: deleteConfirmPopup
        property string pendingId: ""
        property string pendingName: ""
        property bool pendingRunning: false
        property bool pendingBuiltin: false
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 420
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#1e1e2e"
            border.color: "#f38ba8"
            border.width: 1
            radius: 12
        }

        ColumnLayout {
            width: deleteConfirmPopup.availableWidth
            spacing: 12

            Label {
                text: qsTr("Delete Launcher")
                color: "#f38ba8"
                font.pixelSize: 16
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Remove \"%1\" from the launcher list?")
                      .arg(deleteConfirmPopup.pendingName)
                color: "#cdd6f4"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: deleteConfirmPopup.pendingRunning
                text: qsTr("The agent is currently running. Deleting it does not stop the process; stop it via its own command if needed.")
                color: "#f9e2af"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: deleteConfirmPopup.pendingBuiltin
                text: qsTr("This is a built-in launcher. You can bring it back later with \"Restore default launchers\".")
                color: "#7f849c"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Delete")
                    background: Rectangle { radius: 8; color: parent.down ? Qt.darker("#f38ba8", 1.3) : (parent.hovered ? Qt.darker("#f38ba8", 1.15) : "#f38ba8") }
                    contentItem: Label { text: parent.text; color: "#1e1e2e"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        if (!agents.removeAgent(deleteConfirmPopup.pendingId))
                            errorPopup.open()
                        deleteConfirmPopup.close()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    background: Rectangle { radius: 8; color: parent.down ? "#45475a" : (parent.hovered ? "#4a4d62" : "#313244"); border.color: "#45475a" }
                    contentItem: Label { text: parent.text; color: "#cdd6f4"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: deleteConfirmPopup.close()
                }
            }
        }
    }

    // Shown when removeAgent/restoreDefaults could not write agents.json.
    Popup {
        id: errorPopup
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 420
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#1e1e2e"
            border.color: "#f38ba8"
            border.width: 1
            radius: 12
        }

        ColumnLayout {
            width: errorPopup.availableWidth
            spacing: 12

            Label {
                text: qsTr("Save failed")
                color: "#f38ba8"
                font.pixelSize: 16
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Could not write the configuration file:")
                color: "#cdd6f4"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: agents.configFilePath()
                color: "#89b4fa"
                font.pixelSize: 12
                font.family: "Consolas, Monaco, monospace"
                wrapMode: Text.WrapAnywhere
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                background: Rectangle { radius: 8; color: parent.down ? "#45475a" : "#313244"; border.color: "#45475a" }
                contentItem: Label { text: parent.text; color: "#cdd6f4"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: errorPopup.close()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 12

        RowLayout {
            spacing: 12

            Button {
                text: qsTr("\u2190 Back")
                background: Rectangle { color: "transparent" }
                contentItem: Label { text: parent.text; color: "#89b4fa"; font.pixelSize: 16 }
                onClicked: page.StackView.view.pop()
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            text: qsTr("Settings")
            color: "#cdd6f4"
            font.pixelSize: 24
            font.bold: true
        }

        // --- Launchers section -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: qsTr("Launchers")
                color: "#a6adc8"
                font.pixelSize: 16
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Add Launcher")
                background: Rectangle { radius: 8; color: parent.down ? Qt.darker("#89b4fa", 1.3) : (parent.hovered ? Qt.darker("#89b4fa", 1.15) : "#89b4fa") }
                contentItem: Label { text: parent.text; color: "#1e1e2e"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: page.openEditor("")
            }
        }

        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: scrollView.availableWidth
                spacing: 8

                Repeater {
                    model: agents.model

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 60
                        radius: 10
                        color: "#313244"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 12

                            Image {
                                source: model.icon
                                sourceSize: Qt.size(28, 28)
                                fillMode: Image.PreserveAspectFit
                            }

                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true

                                Label {
                                    text: model.name
                                    color: "#cdd6f4"
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: model.command
                                    color: "#7f849c"
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                }
                            }

                            // Running-state dot with tooltip.
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: model.running ? "#a6e3a1" : "#585b70"
                                ToolTip.visible: dotArea.containsMouse
                                ToolTip.delay: 300
                                ToolTip.text: model.running ? qsTr("Running") : qsTr("Stopped")

                                MouseArea {
                                    id: dotArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }

                            Button {
                                text: qsTr("Edit")
                                background: Rectangle { radius: 8; color: parent.down ? "#45475a" : (parent.hovered ? "#4a4d62" : "#313244"); border.color: "#45475a" }
                                contentItem: Label { text: parent.text; color: "#89b4fa"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                onClicked: page.openEditor(model.agentId)
                            }
                            Button {
                                text: qsTr("Delete")
                                background: Rectangle { radius: 8; color: parent.down ? "#45475a" : (parent.hovered ? "#4a4d62" : "#313244"); border.color: "#45475a" }
                                contentItem: Label { text: parent.text; color: "#f38ba8"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                onClicked: {
                                    deleteConfirmPopup.pendingId = model.agentId
                                    deleteConfirmPopup.pendingName = model.name
                                    deleteConfirmPopup.pendingRunning = model.running
                                    deleteConfirmPopup.pendingBuiltin =
                                        agents.isDefaultAgent(model.agentId)
                                    deleteConfirmPopup.open()
                                }
                            }
                        }
                    }
                }
            }
        }

        Button {
            text: qsTr("Restore default launchers")
            background: Rectangle { color: "transparent" }
            contentItem: Label { text: parent.text; color: "#7f849c"; font.pixelSize: 12 }
            onClicked: {
                if (!agents.restoreDefaults())
                    errorPopup.open()
            }
        }
    }
}
