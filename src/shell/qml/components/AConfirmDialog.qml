import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 基于 ADialog 的确认弹窗：标题 + 消息 + 可选的中间详情插槽（detailData）
// + 右对齐的确认/取消按钮行，发出 confirmed/cancelled 信号后自动关闭。
// danger 置真时确认按钮转危险变体，用于破坏性操作的二次确认。
ADialog {
    id: control

    // 正文消息（空串则不渲染）。
    property string message: ""
    // 确认按钮文字（空串则不渲染该按钮）。
    property string confirmText: ""
    // 取消按钮文字（空串则不渲染该按钮）。
    property string cancelText: ""
    // 详情插槽：取消/确认之外还需要上下文（警告、列表等）时由调用方插入，
    // 内容落在消息与按钮行之间。
    default property alias detailData: detailColumn.data

    // 用户点击确认后发出，随按钮点击关闭弹窗。
    signal confirmed()
    // 用户点击取消后发出，随按钮点击关闭弹窗。
    signal cancelled()

    // 消息 + 调用方插入的详情 + 按钮行，整体作为 dialogContent 装进骨架。
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
