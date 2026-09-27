import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Search input with leading icon and a clear button.
TextField {
    id: control

    placeholderText: qsTr("Search...")
    color: theme.textPrimary
    placeholderTextColor: theme.textMuted
    selectionColor: theme.selectionBg
    font.pixelSize: theme.fontSizeBody
    leftPadding: 30
    rightPadding: clearArea.visible ? 28 : theme.spacingM
    implicitHeight: 32

    background: Rectangle {
        radius: theme.radiusControl
        color: theme.surfaceAltBg
        border.color: control.activeFocus ? theme.focusRing : theme.borderSubtle
        border.width: control.activeFocus ? 2 : 1
    }

    Image {
        anchors.left: parent.left
        anchors.leftMargin: theme.spacingS
        anchors.verticalCenter: parent.verticalCenter
        source: "qrc:/icons/search.svg"
        sourceSize: Qt.size(14, 14)
        fillMode: Image.PreserveAspectFit
    }

    // Clear (×) when there is text.
    MouseArea {
        id: clearArea
        anchors.right: parent.right
        anchors.rightMargin: theme.spacingXs
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        visible: control.text.length > 0
        onClicked: control.text = ""

        Image {
            anchors.centerIn: parent
            source: "qrc:/icons/close.svg"
            sourceSize: Qt.size(10, 10)
            fillMode: Image.PreserveAspectFit
        }
    }
}
