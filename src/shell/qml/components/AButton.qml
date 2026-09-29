import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 全应用通用的文字按钮：变体（primary/secondary/ghost/danger）+ 固定高度
// + 令牌化圆角一体封装，页面里不再手写裸 Button 配 Rectangle 背景；
// 需要运行期着色就设 accentColor，不许旁路本组件。
Button {
    id: control

    // 变体：primary（主操作）| secondary（常规）| ghost（弱化）| danger（危险操作）。
    property string variant: "secondary"
    // primary 变体的填充色——默认取主题 accent；卡片场景注入 agent 色，
    // 而不是绕开本组件自己画背景。
    property color accentColor: theme.accent
    // QToolButton 式下拉箭头：为真时在文字后画一个小下箭头。点击箭头区
    // 发出 dropdownActivated()（由使用方弹菜单）；点击按钮其余部分仍是
    // 普通的 clicked。
    property bool dropdown: false
    // 菜单打开期间由使用方置真：箭头区保持高亮，让菜单看起来锚在按钮上。
    property bool menuOpen: false
    // 忙碌指示：为真时文字左侧显示一个旋转的弧形转圈（颜色随 variant 取
    // 主题令牌），表示按钮触发的操作正在后台进行。busy 只负责视觉；点击
    // 是否可用由使用方经 enabled 控制。
    property bool busy: false

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
                       + (control.busy ? spinner.width + theme.spacingXs : 0)
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
                - (control.busy ? spinner.width + theme.spacingXs : 0)
                - (control.dropdown
                   ? 2 * (chevronZone.width + theme.spacingXs)
                   : 0))
        }

        // 忙碌转圈：弧形描边随角度属性旋转（RotationAnimation 动画属性
        // 而不是直接动 rotation，旋转原点才是弧的圆心）。
        Canvas {
            id: spinner
            visible: control.busy
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            // 弧的起始角度：动画 0..360 循环。
            property real sweep: 0

            // Canvas 的 paint 不跟随 label.color 的绑定自动触发；主题
            // 切换时主动请求重绘，转圈颜色才能跟上新主题。
            Connections {
                target: label
                function onColorChanged() { spinner.requestPaint() }
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.lineWidth = 2
                ctx.strokeStyle = label.color
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, width / 2 - 1,
                        spinner.sweep * Math.PI / 180,
                        (spinner.sweep + 270) * Math.PI / 180)
                ctx.stroke()
            }

            RotationAnimation on sweep {
                running: control.busy
                loops: Animation.Infinite
                from: 0
                to: 360
                duration: 1000
            }
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
