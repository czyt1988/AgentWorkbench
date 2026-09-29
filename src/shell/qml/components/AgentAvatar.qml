import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// agent 的可视化入口：图标 + 右下角状态角标，启动卡片与 Web 标签共用。
// 状态永远不只靠颜色——statusText 经 tooltip 给出文字状态。
Item {
    id: control

    // agent 图标 URL。
    property url iconSource: ""
    // agent 主色：点亮状态角标与描边用。
    property color agentColor: theme.accent
    // 运行状态：点亮右下角角标。
    property bool running: false
    // 状态文字（tooltip 用；默认按 running 取「运行中/已停止」）。
    property string statusText: running ? qsTr("Running") : qsTr("Stopped")

    implicitWidth: 32
    implicitHeight: 32

    Image {
        anchors.fill: parent
        source: control.iconSource
        sourceSize: Qt.size(control.width, control.height)
        fillMode: Image.PreserveAspectFit
    }

    // 状态角标：贴右下角，描边环与底面隔开。
    AStatusDot {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        diameter: 8
        on: control.running
        onColor: control.agentColor
        ringColor: theme.surfaceBg
        ringWidth: 1
    }

    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 300
    ToolTip.timeout: 10000
    ToolTip.text: control.statusText
}
