import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 徽标胶囊：badge 背景上的圆角胶囊，用于计数与来源标签；文字、底色、
// 前景色可按实例注入。
Rectangle {
    id: control

    // 胶囊文字。
    property string text: ""
    // 胶囊底色（默认 badge 令牌）。
    property color fillColor: theme.badgeBg
    // 胶囊文字色。
    property color textColor: theme.textSecondary

    implicitWidth: label.implicitWidth + theme.spacingS + theme.spacingXs
    implicitHeight: label.implicitHeight + theme.spacingXs
    radius: theme.radiusPill
    color: fillColor

    // 长标签的悬停提示（如插件 id + 版本）。timeout 保证共享 tooltip
    // 会自动消失（卡死场景的背景见 SkillCard）。
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
