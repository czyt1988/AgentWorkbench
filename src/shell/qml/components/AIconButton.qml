import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 纯图标按钮：常规 28px（large 44px），悬停填充色由图标色派生，tooltip
// 必填——图标本身不表意，文字说明不能省。导航场景用 active 表示当前
// 目的地（侧栏钉底的系统图标即此用法）。
Button {
    id: control

    // 图标 URL。
    property string iconSource: ""
    // 悬停提示文字（必填约定，空串则不显示 tooltip）。
    property string tooltip: ""
    // "large" 撑到 44px，用于悬浮动作按钮。
    property string size: "normal"
    // 导航态：为真时像侧栏当前行一样填充按钮背景，用于表示
    // 「当前目的地」（footer 的系统区图标）。
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
        // 键盘焦点环：只在键盘导航（Tab/快捷键）获得焦点时显示。鼠标点击
        // 同样会让按钮持有 activeFocus（Button 默认 focusPolicy 为
        // StrongFocus），点击后一圈蓝环会一直挂在那儿——判据用仅键盘为
        // 真的 visualFocus（Qt 5.15.16 起可用），而不是 activeFocus。
        border.color: control.visualFocus ? theme.focusRing : "transparent"
        border.width: control.visualFocus ? 2 : 0
    }

    // 图标必须包一层 Item 居中、按 sourceSize 的隐式尺寸渲染，不能直接拿
    // Image 当 contentItem：Control 会把 contentItem 强制成自己的可用尺寸
    // （本按钮 28×28），PreserveAspectFit 随之把 16px 的栅格放大到 28px
    // 绘制——图标按钮曾因此普遍发糊、图标偏大。1:1 无缩放后 SVG 依旧
    // 清晰（sourceSize 是逻辑像素，Qt 对 SVG 自动乘 DPR 栅格化）。
    contentItem: Item {
        Image {
            anchors.centerIn: parent
            source: control.iconSource
            sourceSize: Qt.size(control.size === "large" ? 22 : 16,
                                control.size === "large" ? 22 : 16)
            fillMode: Image.PreserveAspectFit
            opacity: control.enabled ? 1 : 0.5
        }
    }
}
