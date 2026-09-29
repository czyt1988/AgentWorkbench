import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 模态弹窗骨架：居中、遮罩背景、Esc 关闭、焦点落在第一个可交互子项，
// 是一切模态弹窗的统一底座——确认/提示类弹窗基于它扩展，不要旁路它手写
// Popup。正文与动作按钮由调用方经 dialogContent 插入。
Dialog {
    id: control

    // 弹窗标题（空串则不渲染标题行）。
    property alias titleText: titleLabel.text
    // 正文插槽：调用方把内容（含按钮行）作为子项追加进来。
    default property alias dialogContent: contentColumn.data
    // danger 变体：红标题 + 红边框，用于破坏性确认与错误提示。
    property bool danger: false

    anchors.centerIn: parent
    modal: true
    focus: true
    padding: theme.spacingL
    width: 420
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: theme.overlayBg
        border.color: control.danger ? theme.danger : theme.borderSubtle
        border.width: 1
        radius: theme.radiusOverlay
    }

    // 内容列：标题在顶，之后是调用方经 dialogContent 追加的正文与
    // 右对齐动作按钮行（沿用 0.3.0 弹窗的组装方式）。
    contentItem: ColumnLayout {
        id: contentColumn
        spacing: theme.spacingM

        Label {
            id: titleLabel
            visible: text.length > 0
            color: control.danger ? theme.danger : theme.textPrimary
            font.pixelSize: theme.fontSizeSubtitle
            font.bold: true
        }
    }
}
