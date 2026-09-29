import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 通用色块：颜色选择器（AColorPicker）网格里的一格，也是表单里可点击
// 的迷你色卡。colorValue 为空或不是 #RRGGBB 时呈现「无颜色」语义（中
// 性底 + 对角斜线），非法串不会绑到 color 上产生求值告警。三态视觉——
// 常态 1px 令牌描边；悬停微抬 + 边框加强；选中（当前颜色与本格相同，
// 由使用方置 checked）画一圈 accent 环 + 勾。点击直接发 clicked()，
// 选择语义（写回哪里、要不要记忆）由使用方在 handler 里定义。
Rectangle {
    id: control

    // 色块颜色（#rrggbb 串；空串或非法串 = 无颜色）。
    property string colorValue: ""
    // 色块边长（正方形）。
    property real side: 22
    // 是否处于选中态（当前生效颜色 = 本格颜色时由使用方置真）。
    property bool checked: false
    // 状态文字提示（tooltip；空串不启用）。
    property string tooltip: ""

    signal clicked()

    // 只接受 #RRGGBB：QRect color 绑上非法串会每次重求值都告警。
    readonly property bool hasColor: /^#[0-9a-fA-F]{6}$/.test(colorValue.trim())

    width: side
    height: side
    radius: Math.max(2, side / 5)
    color: hasColor ? colorValue.trim() : theme.surfaceAltBg
    border.width: press.containsMouse ? 2 : 1
    border.color: press.containsMouse ? theme.borderStrong
                                      : theme.borderSubtle
    opacity: enabled ? 1 : 0.5

    // 悬停微抬：只作用于视觉层，不占布局。
    scale: press.containsMouse ? 1.08 : 1
    Behavior on scale {
        NumberAnimation { duration: theme.durationFast }
    }

    // 选中环：accent 描边外扩一圈，隔着网格间隙也能找到「当前色」。
    Rectangle {
        visible: control.checked
        anchors.centerIn: parent
        width: parent.width + 6
        height: parent.height + 6
        radius: parent.radius + 3
        color: "transparent"
        border.width: 2
        border.color: theme.accent
    }

    // 选中勾：勾色按底色亮度取反，深浅底色上都可读（选中态不只靠环）。
    Text {
        visible: control.checked && control.hasColor
        anchors.centerIn: parent
        text: "\u2713"
        color: {
            // Rectangle.color 已是 QColor：绑定直接取它的 r/g/b 分量。
            const c = control.color
            return (c.r + c.g + c.b) / 3 > 0.5 ? theme.windowBg
                                               : theme.textOnAccent
        }
        font.pixelSize: Math.max(9, control.side * 0.55)
        font.bold: true
    }

    // 「无颜色」对角斜线：旋转矩形而非 Canvas——Canvas 不随主题令牌变
    // 化自动重绘，绑定式画法免维护。
    Rectangle {
        visible: !control.hasColor
        anchors.centerIn: parent
        width: control.side * 1.2
        height: 2
        radius: 1
        rotation: -45
        color: theme.danger
    }

    MouseArea {
        id: press
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
    ToolTip.visible: press.containsMouse && control.tooltip.length > 0
    ToolTip.delay: 300
    ToolTip.timeout: 10000
    ToolTip.text: control.tooltip
}
