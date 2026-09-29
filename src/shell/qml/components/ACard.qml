import QtQuick
import AgentWorkbench
import AgentWorkbench.App

// 卡片容器：surface 背景、卡片圆角、细边框一体封装，可选 hover 态与
// 选中描边。页面里的卡片外框用它，不要各自手写 Rectangle。
Rectangle {
    id: control

    // 为真时启用 HoverHandler，hover 反馈给 hovered 与背景色。
    property bool hoverEnabled: false
    // 为真时边框转 accent，表示当前选中项。
    property bool selected: false
    // 是否处于悬停态（由内部 HoverHandler 驱动，只读）。
    readonly property bool hovered: hoverEnabled && hoverHandler.hovered

    radius: theme.radiusCard
    color: hovered ? theme.surfaceHoverBg : theme.surfaceBg
    border.color: selected ? theme.accent : theme.borderSubtle
    border.width: 1

    // 悬停检测：enabled 跟随 hoverEnabled，不干扰不可悬停的卡片。
    HoverHandler {
        id: hoverHandler
        enabled: control.hoverEnabled
    }
}
