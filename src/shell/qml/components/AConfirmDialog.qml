import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Confirmation dialog on ADialog: title + message + extra detail rows
// (any inline children placed between message and buttons) + a
// right-aligned action row. `danger` marks destructive confirmations.
ADialog {
    id: control

    property string message: ""
    property string confirmText: ""
    property string cancelText: ""
    // Set by the caller when more than Cancel/Confirm is needed.
    default property alias detailData: detailColumn.data

    signal confirmed()
    signal cancelled()

    // Message + caller-provided detail rows + buttons.
    contentData: [
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

            ColumnLayout {
                id: detailColumn
                Layout.fillWidth: true
                spacing: theme.spacingM
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Item { Layout.fillWidth: true }
                AButton {
                    visible: control.cancelText.length > 0
                    text: control.cancelText
                    onClicked: {
                        control.cancelled()
                        control.close()
                    }
                }
                AButton {
                    visible: control.confirmText.length > 0
                    variant: control.danger ? "danger" : "primary"
                    text: control.confirmText
                    onClicked: {
                        control.confirmed()
                        control.close()
                    }
                }
            }
        }
    ]
}
