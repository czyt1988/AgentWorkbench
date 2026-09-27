import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// The app-wide button: themed variants, fixed height,
// tokenized radius — no bare Rectangle buttons anywhere else.
Button {
    id: control

    // variant: primary | secondary | ghost | danger
    property string variant: "secondary"

    implicitHeight: 32
    padding: theme.spacingM

    background: Rectangle {
        radius: theme.radiusControl
        color: {
            if (!control.enabled)
                return theme.alpha(theme.surfaceAltBg, 0.5)
            switch (control.variant) {
            case "primary":
                return control.down ? theme.pressed(theme.accent)
                                    : (control.hovered ? theme.hover(theme.accent)
                                                       : theme.accent)
            case "danger":
                return control.down ? theme.pressed(theme.danger)
                                    : (control.hovered ? theme.hover(theme.danger)
                                                       : theme.danger)
            case "ghost":
                return control.hovered ? theme.surfaceHoverBg : "transparent"
            default:
                return control.down ? theme.surfaceAltBg
                                    : (control.hovered ? theme.surfaceHoverBg
                                                       : theme.surfaceAltBg)
            }
        }
        // Keyboard focus ring: 2px focusRing, drawn whenever
        // the button has active focus — not only during keyboard navigation.
        border.color: control.activeFocus ? theme.focusRing
                      : (control.variant === "secondary" ? theme.borderSubtle
                                                         : "transparent")
        border.width: control.activeFocus ? 2
                      : (control.variant === "secondary" ? 1 : 0)
        opacity: control.enabled ? 1 : 0.5
    }

    contentItem: Text {
        text: control.text
        color: {
            switch (control.variant) {
            case "primary":
                return theme.textOnAccent
            case "danger":
                return theme.windowBg
            default:
                return theme.textPrimary
            }
        }
        font.pixelSize: theme.fontSizeBody
        font.bold: control.variant === "primary" || control.variant === "danger"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
