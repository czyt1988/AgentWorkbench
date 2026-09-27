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
        // 尺寸必须经 implicitWidth/implicitHeight 汇报给控件：Button 的隐式
        // 宽度取自 contentItem 的隐式尺寸，Item 不像 Text 自带隐式宽度，
        // 必须显式绑定（否则所有按钮塌缩到只剩内边距的固定宽度）。
        // 下拉按钮在文字两侧各预留一份（箭头区 + 间距）：文字按整按钮
        // 视觉居中时，右侧也不会与贴右缘的箭头区重叠。
        implicitWidth: label.implicitWidth
                       + (control.dropdown
                          ? 2 * (chevronZone.width + theme.spacingXs)
                          : 0)
        implicitHeight: label.implicitHeight

        Text {
            id: label
            // 锚定在 contentItem 整体居中：文字相对按钮视觉居中。下拉
            // 箭头不与文字一起参与居中，否则文字会偏左。
            anchors.centerIn: parent
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
            // 超长文字在此宽度内省略，避免与右缘箭头区重叠。
            width: Math.min(
                implicitWidth,
                control.availableWidth
                - (control.dropdown
                   ? 2 * (chevronZone.width + theme.spacingXs)
                   : 0))
        }

        // 箭头区贴按钮右缘（工具栏按钮惯例：下拉箭头靠最右，而非跟在
        // 文字后居中）。MouseArea 是 control 的后代，先于控件收到按下
        // 事件，点击不会传给按钮本体，两个动作因此干净地分开。
        Item {
            id: chevronZone
            visible: control.dropdown
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
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
