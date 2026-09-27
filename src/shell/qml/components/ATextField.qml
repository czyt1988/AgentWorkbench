import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// Themed single-line text field: surface-alt background, focus ring,
// optional invalid (red) state. Height matches AButton/ASearchField (32).
// ASearchField stays separate — it adds the leading icon and clear button.
TextField {
    id: control

    property bool invalid: false

    color: theme.textPrimary
    placeholderTextColor: theme.textMuted
    selectionColor: theme.selectionBg
    font.pixelSize: theme.fontSizeBody
    implicitHeight: 32

    background: Rectangle {
        radius: theme.radiusControl
        color: theme.surfaceAltBg
        border.color: control.invalid ? theme.danger
                                      : (control.activeFocus ? theme.focusRing
                                                             : theme.borderSubtle)
        border.width: control.invalid || control.activeFocus ? 2 : 1
    }
}
