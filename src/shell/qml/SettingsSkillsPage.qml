import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·Skills 分区：扫描根的启停/增删与统计。输入到一半切走再回来
// 不丢内容——页面常驻（见 SettingsPage 的 StackLayout）。
ScrollView {
    id: page

    contentWidth: availableWidth
    ScrollBar.vertical: AScrollBar {}

    ColumnLayout {
        width: page.availableWidth
        spacing: theme.spacingM

        PageHeader {
            title: qsTr("Skills")
            subtitle: qsTr("Directories scanned for SKILL.md entries")
            AButton {
                text: qsTr("Rescan")
                onClicked: skills.refresh()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingS

            Repeater {
                // Property (not roots()): re-evaluates when a root is
                // added/removed/toggled via the NOTIFY signal.
                model: skills.roots
                delegate: AListRow {
                    required property var modelData

                    rowHeight: 44
                    Layout.fillWidth: true

                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Label {
                            text: modelData.label
                            color: theme.textPrimary
                            font.pixelSize: theme.fontSizeBody
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.path
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                            font.family: theme.monoFamily
                            elide: Text.ElideMiddle
                        }
                    }
                    Label {
                        text: skills.kindLabel(modelData.kind)
                        color: theme.textSecondary
                        font.pixelSize: theme.fontSizeCaption
                    }
                    Switch {
                        checked: modelData.enabled
                        onToggled: skills.setRootEnabled(
                            modelData.id, checked)
                    }
                    AIconButton {
                        iconSource: "qrc:/icons/close.svg"
                        tooltip: qsTr("Remove this root")
                        onClicked: skills.removeRoot(modelData.id)
                    }
                }
            }

            // Add a custom root.
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                ATextField {
                    id: newRootField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Add a skill root directory...")
                    font.family: theme.monoFamily
                    font.pixelSize: theme.fontSizeSmall
                    onAccepted: {
                        if (skills.addRoot(text.trim()))
                            text = ""
                    }
                }
                AButton {
                    text: qsTr("Add")
                    enabled: newRootField.text.trim().length > 0
                    onClicked: {
                        if (skills.addRoot(newRootField.text.trim()))
                            newRootField.text = ""
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: skills.statsText
                color: skills.partialFailure ? theme.warning
                                             : theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Plugin caches keep several versions of the same plugin; only the highest is listed.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.WordWrap
            }
        }
    }
}
