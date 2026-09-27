import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// Agent icon + status dot, shared by launcher cards and
// (from S5) web tabs. Status is never color-only: a tooltip carries the
// text state as well.
Item {
    id: control

    property url iconSource: ""
    property color agentColor: theme.accent
    property bool running: false
    property string statusText: running ? qsTr("Running") : qsTr("Stopped")

    implicitWidth: 32
    implicitHeight: 32

    Image {
        anchors.fill: parent
        source: control.iconSource
        sourceSize: Qt.size(control.width, control.height)
        fillMode: Image.PreserveAspectFit
    }

    // Status dot, bottom-right.
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
