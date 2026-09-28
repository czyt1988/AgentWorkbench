import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·插件分区：实验性插件的开关与清单。
ScrollView {
    id: page

    contentWidth: availableWidth
    ScrollBar.vertical: AScrollBar {}

    ColumnLayout {
        width: page.availableWidth
        spacing: theme.spacingM

        PageHeader {
            title: qsTr("Plugins")
            subtitle: qsTr("Experimental extensions dropped into the plugins folder")
        }

        // Trust notice — required by
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            text: workbench.pluginTrustNotice()
            color: theme.warning
            font.pixelSize: theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Enable plugins (experimental)")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            Switch {
                checked: workbench.pluginsEnabled()
                onToggled: workbench.setPluginsEnabled(checked)
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            visible: workbench.pluginList().length === 0
            text: qsTr("No plugins found. Drop one into the plugins folder (Settings -> data directory).")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingS

            Repeater {
                model: workbench.pluginList()
                delegate: AListRow {
                    required property var modelData

                    rowHeight: 56
                    Layout.fillWidth: true

                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Label {
                            text: (modelData.name || modelData.id)
                                  + "  v" + (modelData.version || "?")
                            color: theme.textPrimary
                            font.pixelSize: theme.fontSizeBody
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.description || ""
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                            elide: Text.ElideRight
                        }
                    }
                    // Effective on the next start.
                    Switch {
                        checked: modelData.enabled
                        onToggled: workbench.setPluginEnabled(
                            modelData.id, checked)
                    }
                }
            }
        }
    }
}
