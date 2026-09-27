import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// 主题化多行文本编辑器：surface-alt 背景、焦点环、可选 invalid 红边，
// 外观契约与 ATextField 一致（同为 surfaceAltBg/focusRing/borderSubtle）。
// 单行输入仍用 ATextField；带图标与清除键的搜索框仍用 ASearchField。
TextArea {
    id: control

    property bool invalid: false

    color: theme.textPrimary
    placeholderTextColor: theme.textMuted
    selectionColor: theme.selectionBg
    font.pixelSize: theme.fontSizeBody
    wrapMode: TextArea.Wrap

    background: Rectangle {
        radius: theme.radiusControl
        color: theme.surfaceAltBg
        border.color: control.invalid ? theme.danger
                                      : (control.activeFocus ? theme.focusRing
                                                             : theme.borderSubtle)
        border.width: control.invalid || control.activeFocus ? 2 : 1
    }
}
