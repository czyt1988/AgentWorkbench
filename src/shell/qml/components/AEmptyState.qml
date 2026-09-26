import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Empty state (specs/02 §10.2): icon + title + description + action button.
// The root is a plain Item — layout-managed, so it must not carry anchors
// (that would be undefined behavior); the column centers itself inside.
Item {
    id: control

    property string iconSource: ""
    property string title: ""
    property string description: ""
    property string actionText: ""
    signal actionClicked()

    ColumnLayout {
        anchors.centerIn: parent
        spacing: theme.spacingM
        width: Math.min(parent.width - theme.spacingXl, 520)

        Image {
            Layout.alignment: Qt.AlignHCenter
            source: control.iconSource
            sourceSize: Qt.size(48, 48)
            fillMode: Image.PreserveAspectFit
            opacity: 0.7
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            visible: control.title.length > 0
            text: control.title
            color: theme.textPrimary
            font.pixelSize: theme.fontSizeSubtitle
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            visible: control.description.length > 0
            text: control.description
            color: theme.textMuted
            font.pixelSize: theme.fontSizeBody
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        AButton {
            Layout.alignment: Qt.AlignHCenter
            visible: control.actionText.length > 0
            variant: "primary"
            text: control.actionText
            onClicked: control.actionClicked()
        }
    }
}
