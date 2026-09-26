import QtQuick
import AgentWorkbench.App
import AgentWorkbench

// Card container (specs/02 §10.2): surface background, card radius, subtle
// border with an optional hover state.
Rectangle {
    id: control

    property bool hoverEnabled: false
    property bool selected: false
    readonly property bool hovered: hoverEnabled && hoverHandler.hovered

    radius: theme.radiusCard
    color: hovered ? theme.surfaceHoverBg : theme.surfaceBg
    border.color: selected ? theme.accent : theme.borderSubtle
    border.width: 1

    HoverHandler {
        id: hoverHandler
        enabled: control.hoverEnabled
    }
}
