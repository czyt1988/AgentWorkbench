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
    // Fill for the primary variant — defaults to the theme accent; cards
    // tint theirs with the agent color instead of bypassing the component.
    property color accentColor: theme.accent
    // QToolButton 式下拉箭头：为真时在文字后画一个小下箭头。点击箭头区
    // 发出 dropdownActivated()（由使用方弹菜单）；点击按钮其余部分仍是
    // 普通的 clicked。
    property bool dropdown: false
    // 菜单打开期间由使用方置真：箭头区保持高亮，让菜单看起来锚在按钮上。
    property bool menuOpen: false

    signal dropdownActivated()

    implicitHeight: 32
    padding: theme.spacingM

    background: Rectangle {
        radius: theme.radiusControl
        color: {
            if (!control.enabled)
                return theme.alpha(theme.surfaceAltBg, 0.5)
            switch (control.variant) {
            case "primary":
                return control.down ? theme.pressed(control.accentColor)
                                    : (control.hovered ? theme.hover(control.accentColor)
                                                       : control.accentColor)
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

    contentItem: Item {
        Row {
            anchors.centerIn: parent
            spacing: theme.spacingXs

            Text {
                id: label
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
                elide: Text.ElideRight
                // 超长文字在此宽度内省略，而不是把箭头区挤出去。
                width: Math.min(implicitWidth,
                                control.availableWidth
                                - (control.dropdown
                                   ? chevronZone.width + theme.spacingXs
                                   : 0))
            }

            // 箭头点击区。MouseArea 是 control 的后代，先于控件收到按下
            // 事件，点击不会传给按钮本体，两个动作因此干净地分开。
            Item {
                id: chevronZone
                visible: control.dropdown
                width: 18
                height: label.implicitHeight

                Rectangle {
                    anchors.fill: parent
                    radius: theme.radiusPill
                    color: chevronArea.containsMouse || control.menuOpen
                           ? theme.alpha(theme.windowBg, 0.3)
                           : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "\u25BC"
                    color: label.color
                    font.pixelSize: theme.fontSizeCaption
                }
                MouseArea {
                    id: chevronArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: control.dropdownActivated()
                }
            }
        }
    }
}
