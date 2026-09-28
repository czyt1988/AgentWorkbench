import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·启动器分区：launcher 列表的增删改。编辑、删除确认与保存失败
// 提示三个对话框归本页私有（AgentGridPage 另有自己的编辑对话框实例）。
Item {
    id: page

    function openEditor(agentId) {
        editDialog.openFor(agentId)
    }

    AgentEditDialog {
        id: editDialog
    }

    // --- Delete confirmation -----------------------------------------------
    AConfirmDialog {
        id: deleteConfirmPopup
        danger: true
        titleText: qsTr("Delete Launcher")
        message: qsTr("Remove \"%1\" from the launcher list?")
                  .arg(deleteConfirmPopup.pendingName)
        confirmText: qsTr("Delete")
        cancelText: qsTr("Cancel")
        property string pendingId: ""
        property string pendingName: ""
        property bool pendingRunning: false
        property bool pendingBuiltin: false

        // Contextual warnings injected between message and buttons.
        Label {
            Layout.fillWidth: true
            visible: deleteConfirmPopup.pendingRunning
            text: qsTr("The agent is currently running. Deleting it does not stop the process; stop it via its own command if needed.")
            color: theme.warning
            font.pixelSize: theme.fontSizeBody
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            visible: deleteConfirmPopup.pendingBuiltin
            text: qsTr("This is a built-in launcher. You can bring it back later with \"Restore default launchers\".")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        onConfirmed: {
            if (!agents.removeAgent(deleteConfirmPopup.pendingId))
                errorPopup.open()
        }
    }

    // Shown when removeAgent could not write agents.json.
    AAlertDialog {
        id: errorPopup
        titleText: qsTr("Save failed")
        message: qsTr("Could not write the configuration file:")
        detail: agents.configFilePath()
        dismissText: qsTr("OK")
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical: AScrollBar {}

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: theme.spacingM

            PageHeader {
                title: qsTr("Launchers")
                subtitle: qsTr("The agents available on the launcher page")
                AButton {
                    text: qsTr("Add Launcher")
                    onClicked: page.openEditor("")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingS

                Repeater {
                    model: agents.model

                    delegate: AListRow {
                        rowHeight: 60
                        Layout.fillWidth: true
                        // Rows are delegates; deleting an agent while its
                        // status-dot tooltip is showing must not freeze the
                        // shared tooltip on screen.
                        Component.onDestruction: ToolTip.hide()

                        Image {
                            source: model.icon
                            sourceSize: Qt.size(28, 28)
                            fillMode: Image.PreserveAspectFit
                        }

                        ColumnLayout {
                            spacing: theme.spacingXs
                            Layout.fillWidth: true

                            Label {
                                text: model.name
                                color: theme.textPrimary
                                font.pixelSize: theme.fontSizeBody
                                font.bold: true
                            }
                            Label {
                                Layout.fillWidth: true
                                text: model.command
                                color: theme.textMuted
                                font.pixelSize: theme.fontSizeSmall
                                elide: Text.ElideMiddle
                            }
                        }

                        // Running-state dot (tooltip built in —
                        // never color-only).
                        AStatusDot {
                            diameter: 10
                            on: model.running
                            tooltip: model.running ? qsTr("Running")
                                                   : qsTr("Stopped")
                        }

                        AButton {
                            text: qsTr("Edit")
                            onClicked: page.openEditor(model.agentId)
                        }
                        AButton {
                            variant: "danger"
                            text: qsTr("Delete")
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
}
