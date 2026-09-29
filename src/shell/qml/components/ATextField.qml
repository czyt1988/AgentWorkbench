import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 主题化单行输入框：surface-alt 背景、焦点环、可选 invalid（红）状态，
// 高度 32，与 AButton/ASearchField 对齐。带前置图标与清除键的搜索框
// 是独立组件 ASearchField。
TextField {
    id: control

    // 无效态：为真时边框转 danger 红。
    property bool invalid: false

    color: theme.textPrimary
    placeholderTextColor: theme.textMuted
    selectionColor: theme.selectionBg
    selectedTextColor: theme.selectionText
    font.pixelSize: theme.fontSizeBody
    implicitHeight: 32

    background: Rectangle {
        radius: theme.radiusControl
        color: theme.surfaceAltBg
        border.color: control.invalid ? theme.danger
                                      : (control.activeFocus ? theme.focusRing
                                                             : theme.borderSubtle)
        border.width: control.invalid || control.activeFocus ? 2 : 1
    }
}
