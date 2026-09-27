import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Form field label: text + red required marker + info icon; hovering the
// label or the info icon shows the tip (both are hover sources so a long
// tip is reachable from either end of the row).
RowLayout {
    id: labelRow

    Layout.fillWidth: true

    property string labelText: ""
    property bool isRequired: false
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
