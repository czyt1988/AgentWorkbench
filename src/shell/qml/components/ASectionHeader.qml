import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置分组标题：副标题级文字 + 底部分割线，尾部 extra 插槽收纳右对齐的
// 分节动作（与 PageHeader 的尾槽同构）。表单/设置页分节统一用它。
ColumnLayout {
    id: control

    // 分组标题文字。
    property string text: ""
    // 尾部动作插槽：按钮、选择框等注入后贴行右缘。
    property alias extra: extraSlot.data

    spacing: theme.spacingXs
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.spacingS

        Label {
            text: control.text
            color: theme.textSecondary
            font.pixelSize: theme.fontSizeSubtitle
            font.bold: true
        }

        // 弹簧项：把 extra 内容推到行右缘。
        Item {
            Layout.fillWidth: true
        }

        // 经 extra 别名注入的子项（按钮、组合框）。必须是 Layout，
        // 注入的子项才会被定位并垂直居中——裸 Item 会把它们堆在 (0,0)，
        // 溢出到零高行并盖住下方的分割线。
        RowLayout {
            id: extraSlot
            spacing: theme.spacingS
        }
    }

    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: theme.separator
    }
}
