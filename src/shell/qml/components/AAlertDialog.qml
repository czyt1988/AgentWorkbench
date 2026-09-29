import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 基于 ADialog 的提示弹窗（默认 danger 样式）：标题 + 消息 + 可选的等宽
// 字体详情块（命令输出/文件路径之类）+ 关闭按钮，用于错误与通知的呈现。
// 用户点关闭或按 Esc 即发出 dismissed 并关闭。
ADialog {
    id: control

    danger: true

    // 正文消息（空串则不渲染）。
    property string message: ""
    // 详情文本（空串则不渲染详情块）。
    property string detail: ""
    // 关闭按钮文字。
    property string dismissText: ""

    // 用户关闭弹窗时发出。
    signal dismissed()

    // 消息 + 可滚动详情 + 关闭按钮，整体作为 dialogContent 装进骨架。
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

            // 可滚动的等宽详情，高度封顶，超长的命令输出不会把按钮行
            // 顶出屏幕。
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
