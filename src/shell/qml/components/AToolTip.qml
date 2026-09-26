import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// Unified tooltip styling (specs/02 §10.2): themed background, 300 ms
// delay, max width 360.
ToolTip {
    id: control

    delay: 300
    text: ""
    contentItem: Text {
        text: control.text
        color: theme.tooltipText
        font.pixelSize: theme.fontSizeSmall
        wrapMode: Text.WordWrap
        maximumLineCount: 6
        elide: Text.ElideRight
        width: Math.min(implicitWidth, 360)
    }
    background: Rectangle {
        color: theme.tooltipBg
        radius: theme.radiusControl
        border.color: theme.borderSubtle
        border.width: 1
    }
}
