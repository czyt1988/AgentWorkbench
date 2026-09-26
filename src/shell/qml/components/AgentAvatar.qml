import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// Agent icon + status dot (specs/02 §10.2), shared by launcher cards and
// (from S5) web tabs. Status is never color-only: a tooltip carries the
// text state as well (specs/02 §14).
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
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 8
        height: 8
        radius: theme.radiusPill
        color: control.running ? control.agentColor : theme.neutralOff
        border.color: theme.surfaceBg
        border.width: 1
    }

    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 300
    ToolTip.text: control.statusText
}
