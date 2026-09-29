import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 颜色输入行：主题化文本框（手输入 #RRGGBB）+ 迷你色卡。点色卡弹出
// AColorPicker 快速选色，选中写回文本框；allowNone 时「No color」清空文
// 本（表单里的「跟随默认」语义）。文本框是唯一真相源，手输入与选色互
// 相不打架；校验（#RRGGBB 或空）沿用使用方自己的规则，经 invalid 注入。
// 排版上按「一行控件」使用：Layout.fillWidth 设在使用点，内部文本框吃
// 剩余宽度。
Item {
    id: control

    // 颜色文本（#rrggbb 串；空串 = 跟随默认）。
    property alias text: field.text
    // 文本框占位提示。
    property alias placeholderText: field.placeholderText
    // 无效态红边（触发条件由使用方决定）。
    property alias invalid: field.invalid
    // 允许「No color」清空文本；关闭后选色只能落成具体颜色
    property bool allowNone: true

    implicitHeight: field.implicitHeight

    RowLayout {
        anchors.fill: parent
        spacing: theme.spacingS

        ATextField {
            id: field
            Layout.fillWidth: true
        }

        AColorSwatch {
            id: swatch
            side: 30
            colorValue: field.text.trim()
            tooltip: qsTr("Pick a color")
            onClicked: picker.openBelow(swatch)
        }
    }

    AColorPicker {
        id: picker
        allowNone: control.allowNone
        // 跟随文本框（而非在点击时赋值）：绑定不会被命令式赋值打断，
        // 对话框表单初始回填时选中标记也是对的。
        currentHex: field.text.trim()
        onPicked: function(hex) {
            field.text = hex
        }
        onCleared: field.text = ""
    }
}
