import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·环境分区：Python / Node.js 检测结果与手动重测。
ScrollView {
    id: page

    contentWidth: availableWidth
    ScrollBar.vertical: AScrollBar {}

    ColumnLayout {
        width: page.availableWidth
        spacing: theme.spacingM

        PageHeader {
            title: qsTr("Environment")
            subtitle: qsTr("Runtimes used by the agents' setup commands")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: environment.pythonInstalled
                      ? qsTr("Python %1").arg(environment.pythonVersion)
                      : qsTr("Python not found")
                color: environment.pythonInstalled ? theme.textPrimary
                                                   : theme.danger
                font.pixelSize: theme.fontSizeBody
            }
            Label {
                Layout.fillWidth: true
                text: environment.nodeInstalled
                      ? qsTr("Node.js %1").arg(environment.nodeVersion)
                      : qsTr("Node.js not found")
                color: environment.nodeInstalled ? theme.textPrimary
                                                 : theme.danger
                font.pixelSize: theme.fontSizeBody
            }
            AButton {
                text: qsTr("Re-detect")
                onClicked: environment.refresh()
            }
        }
    }
}
