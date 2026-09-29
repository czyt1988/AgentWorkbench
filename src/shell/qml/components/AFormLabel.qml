import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 表单字段标签行：文字 + 红色必填星号 + 信息图标。悬停标签或信息图标都
// 会显示 tip——两者都是 hover 源，长提示从行的任一端都能触发。
RowLayout {
    id: labelRow

    Layout.fillWidth: true

    // 标签文字。
    property string labelText: ""
    // 为真时显示红色必填星号。
    property bool isRequired: false
    // 提示文本（非空时才渲染信息图标并启用 tooltip）。
    property string tip: ""

    spacing: theme.spacingXs

    Label {
        text: labelRow.labelText
        color: theme.textSecondary
        font.pixelSize: theme.fontSizeBody

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            ToolTip.text: labelRow.tip
            ToolTip.visible: containsMouse && labelRow.tip.length > 0
            ToolTip.delay: 300
            ToolTip.timeout: 10000
        }
    }
    Label {
        text: "*"
        color: theme.danger
        font.pixelSize: theme.fontSizeBody
        visible: labelRow.isRequired
    }
    Label {
        text: "\u2139"
        color: theme.textDisabled
        font.pixelSize: theme.fontSizeBody
        visible: labelRow.tip.length > 0

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            ToolTip.text: labelRow.tip
            ToolTip.visible: containsMouse
            ToolTip.delay: 300
            ToolTip.timeout: 10000
        }
    }
    Item { Layout.fillWidth: true }
}
