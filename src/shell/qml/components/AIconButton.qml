import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// Icon-only button: 28px (large 44), hover fill derived
// from the icon color, tooltip required.
Button {
    id: control

    property string iconSource: ""
    property string tooltip: ""
    // "large" grows to 44 for floating action buttons.
    property string size: "normal"
    // Navigation state: fills the button like an active sidebar row when
    // it represents the current destination (footer system icons).
    property bool active: false

    implicitWidth: size === "large" ? 44 : 28
    implicitHeight: size === "large" ? 44 : 28
    padding: 0

    ToolTip.visible: tooltip.length > 0 && hovered
    ToolTip.delay: 300
    ToolTip.timeout: 10000
    ToolTip.text: tooltip

    background: Rectangle {
        radius: theme.radiusControl
        color: control.down ? theme.alpha(theme.textMuted, 0.28)
                            : (control.active ? theme.surfaceBg
                             : (control.hovered ? theme.alpha(theme.textMuted, 0.18)
                                                : "transparent"))
        // Keyboard focus ring.
        border.color: control.activeFocus ? theme.focusRing : "transparent"
        border.width: control.activeFocus ? 2 : 0
    }

    contentItem: Image {
        source: control.iconSource
        sourceSize: Qt.size(size === "large" ? 22 : 16,
                            size === "large" ? 22 : 16)
        fillMode: Image.PreserveAspectFit
        opacity: control.enabled ? 1 : 0.5
    }
}
