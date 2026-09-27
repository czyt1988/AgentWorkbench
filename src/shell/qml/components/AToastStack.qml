import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Toast stack: at most three visible, queued behind them;
// each toast auto-dismisses after its level's duration, and the timer
// pauses while the pointer is over it.
ColumnLayout {
    id: stack

    spacing: theme.spacingS

    Repeater {
        model: toasts
        delegate: Rectangle {
            id: toast

            readonly property int duration: toasts.durationFor(model.level)
            visible: index < toasts.maxVisible()

            Layout.preferredWidth: theme.toastWidth
            implicitHeight: Math.max(body.implicitHeight + 2 * theme.spacingM,
                                     theme.spacingXl)
            radius: theme.radiusOverlay
            color: theme.overlayBg
            border.width: 1
            border.color: theme.borderSubtle
            clip: true

            // Left color bar by level.
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 3
                color: {
                    switch (model.level) {
                    case "success": return theme.success
                    case "warning": return theme.warning
                    case "error": return theme.danger
                    default: return theme.info
                    }
                }
            }

            RowLayout {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: theme.spacingM
                anchors.rightMargin: theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                spacing: theme.spacingS

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingXs

                    Label {
                        visible: model.title.length > 0
                        text: model.title
                        color: theme.textPrimary
                        font.pixelSize: theme.fontSizeBody
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: model.text.length > 0
                        text: model.text
                        color: theme.textSecondary
                        font.pixelSize: theme.fontSizeSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                AIconButton {
                    iconSource: "qrc:/icons/close.svg"
                    tooltip: qsTr("Dismiss")
                    onClicked: toasts.dismiss(model.toastId)
                }
            }

            // Auto-dismiss with hover pause.
            Timer {
                id: dismissTimer
                interval: toast.duration
                running: toast.visible && !hoverHandler.hovered
                onTriggered: toasts.dismiss(model.toastId)
            }
            HoverHandler { id: hoverHandler }
        }
    }
}
