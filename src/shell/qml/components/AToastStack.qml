import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 右下角通知栈：最多同屏 3 条，其余在后面排队；每条按级别时长自动消失，
// 指针悬停其上时计时暂停。数据来自 toasts 单例，dismiss 经 toastId 回调。
ColumnLayout {
    id: stack

    spacing: theme.spacingS

    Repeater {
        model: toasts
        delegate: Rectangle {
            id: toast

            // 该级别的自动消失时长（来自 toasts 门面）。
            readonly property int duration: toasts.durationFor(model.level)
            // 排队：超出 maxVisible 的条目隐藏在后面等前面的让位。
            visible: index < toasts.maxVisible()

            Layout.preferredWidth: theme.toastWidth
            implicitHeight: Math.max(body.implicitHeight + 2 * theme.spacingM,
                                     theme.spacingXl)
            radius: theme.radiusOverlay
            color: theme.overlayBg
            border.width: 1
            border.color: theme.borderSubtle
            clip: true

            // 左侧按级别着色的色条。
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
                        font.pixelSize: theme.fontSizeCaption
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

            // 自动消失计时：悬停暂停，到点经 toastId 通知门面移除本条。
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
