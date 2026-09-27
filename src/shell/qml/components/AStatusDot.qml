import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// Status dot: small on/off indicator. Never color-only — the tooltip
// carries the text state (accessibility + light/dark themes).
Rectangle {
    id: control

    property bool on: false
    property color onColor: theme.success
    property color offColor: theme.neutralOff
    property real diameter: 8
    // Ring for dots sitting on an icon (AgentAvatar); transparent by default.
    property color ringColor: "transparent"
    property real ringWidth: 0
    property string tooltip: ""

    width: diameter
    height: diameter
    radius: width / 2
    color: on ? onColor : offColor
    border.color: ringColor
    border.width: ringWidth
    Behavior on color { ColorAnimation { duration: theme.durationNormal } }

    HoverHandler { id: hover; enabled: control.tooltip.length > 0 }
    ToolTip.visible: hover.enabled && hover.hovered
    ToolTip.delay: 300
    ToolTip.timeout: 10000
    ToolTip.text: control.tooltip
}
