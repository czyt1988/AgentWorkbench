import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The example plugin's page: proof that a plugin can contribute UI through
// the same path the built-in pages use (specs/01 §9.4).
Rectangle {
    id: page

    color: theme.workspaceBg

    ColumnLayout {
        anchors.centerIn: parent
        spacing: theme.spacingM

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Hello from a plugin!")
            color: theme.textPrimary
            font.pixelSize: theme.fontSizePageTitle
            font.bold: true
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 420
            text: qsTr("This page was registered by the example plugin through the plugin Services interface. It runs in the host process — only enable plugins you trust.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeBody
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        APill {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("extensions section")
        }
    }
}
