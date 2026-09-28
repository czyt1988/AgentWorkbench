import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Alert dialog on ADialog (danger-styled by default): title + message +
// an optional scrollable detail block (mono font, for command output /
// file paths) + a dismiss button.
ADialog {
    id: control

    danger: true

    property string message: ""
    property string detail: ""
    property string dismissText: ""

    signal dismissed()

    dialogContent: [
        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                visible: control.message.length > 0
                text: control.message
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }

            // Scrollable mono detail, height-capped so long command output
            // cannot push the buttons off screen.
            ScrollView {
                Layout.fillWidth: true
                visible: control.detail.length > 0
                implicitHeight: Math.min(detailLabel.implicitHeight, 160)
                clip: true
                ScrollBar.vertical: AScrollBar {}

                Label {
                    id: detailLabel
                    width: control.availableWidth
                    text: control.detail
                    color: theme.textSecondary
                    font.family: theme.monoFamily
                    font.pixelSize: theme.fontSizeSmall
                    wrapMode: Text.WrapAnywhere
                    textFormat: Text.PlainText
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AButton {
                    text: control.dismissText
                    onClicked: {
                        control.dismissed()
                        control.close()
                    }
                }
            }
        }
    ]
}
