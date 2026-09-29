import QtQuick
import QtQuick.Controls
import AgentWorkbench.App
import AgentWorkbench

// 主题化多行文本编辑器：surface 背景、焦点环、可选 invalid 红边。
// 背景与 ATextField（surfaceAltBg）有意区分：长文编辑区用 surface——
// 亮色下 surfaceAlt 偏灰显脏，深色下 surface 比 surfaceAlt 更沉，像一块
// 「输入槽」。单行输入仍用 ATextField；带图标与清除键的搜索框仍用
// ASearchField。
TextArea {
    id: control

    property bool invalid: false

    color: theme.textPrimary
    placeholderTextColor: theme.textMuted
    selectionColor: theme.selectionBg
    font.pixelSize: theme.fontSizeBody
    wrapMode: TextArea.Wrap
    // TextEdit 的 selectByMouse 默认 false（Controls2 各风格也不覆盖）：
    // 不显式打开，用户能输入却无法用鼠标选中文字复制。
    selectByMouse: true

    background: Rectangle {
        radius: theme.radiusControl
        color: theme.surfaceBg
        border.color: control.invalid ? theme.danger
                                      : (control.activeFocus ? theme.focusRing
                                                             : theme.borderSubtle)
        border.width: control.invalid || control.activeFocus ? 2 : 1
    }
}
