import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// Pill badge: rounded capsule on the badge background.
Rectangle {
    id: control

    property string text: ""
    property color fillColor: theme.badgeBg
    property color textColor: theme.textSecondary

    implicitWidth: label.implicitWidth + theme.spacingS + theme.spacingXs
    implicitHeight: label.implicitHeight + theme.spacingXs
    radius: theme.radiusPill
    color: fillColor

    // Tooltip for long labels (e.g. plugin id + version). The timeout keeps
    // the shared tooltip self-dismissing (see SkillCard for the stuck-case
    // background).
    property string tooltip: ""
    HoverHandler { id: hover }
    ToolTip.visible: tooltip.length > 0 && hover.hovered
    ToolTip.delay: 300
    ToolTip.timeout: 10000
    ToolTip.text: tooltip

    Label {
        id: label
        anchors.centerIn: parent
        text: control.text
        color: control.textColor
        font.pixelSize: theme.fontSizeCaption
        font.bold: true
    }
}
