import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 状态点：on/off 的小圆点指示。状态永远不只靠颜色——文字状态由 tooltip
// 承载（色弱用户与深浅两套主题都需要）。颜色、尺寸、描边可按实例注入。
Rectangle {
    id: control

    // 指示状态：为真点亮 onColor，否则熄灭为 offColor。
    property bool on: false
    // 点亮色（默认 success 令牌）。
    property color onColor: theme.success
    // 熄灭色（默认 neutralOff 令牌）。
    property color offColor: theme.neutralOff
    // 圆点直径。
    property real diameter: 8
    // 描边环色：用于叠在图标上的状态点（AgentAvatar），默认透明。
    property color ringColor: "transparent"
    // 描边环宽（0 = 不描边）。
    property real ringWidth: 0
    // 状态文字提示（空串则不启用 tooltip）。
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
