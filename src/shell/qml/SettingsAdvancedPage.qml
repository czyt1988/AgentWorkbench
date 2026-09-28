import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·高级分区：配置文件路径与恢复默认启动器。写失败的错误提示
// 归本页私有。
Item {
    id: page

    // Shown when restoreDefaults could not write agents.json.
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
                title: qsTr("Advanced")
                subtitle: qsTr("Configuration storage and reset actions")
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: agents.configFilePath()
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeSmall
                    font.family: theme.monoFamily
                    elide: Text.ElideMiddle
                }
                AButton {
                    text: qsTr("Open data folder")
                    onClicked: workbench.openFolder(
                        agents.configFilePath().replace(
                            /[\\\\\\/]agents\\.json$/, ""))
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                Layout.bottomMargin: theme.spacingL
                spacing: theme.spacingM

                Item { Layout.fillWidth: true }
                AButton {
                    text: qsTr("Restore default launchers")
                    onClicked: {
                        if (!agents.restoreDefaults())
                            errorPopup.open()
                    }
                }
            }
        }
    }
}
