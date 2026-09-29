import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 页面标题栏：左侧标题 + 副标题，右侧页面级动作插槽（actions 注入后贴
// 右缘）。除 Web 页（自带标签栏）外每个页面都自己渲染一份。
RowLayout {
    id: control

    // 页面标题。
    property string title: ""
    // 页面副标题（标题下方的一行说明）。
    property string subtitle: ""
    // 页面级动作插槽：按钮等注入后贴行右缘。
    default property alias actions: actionsSlot.data

    Layout.fillWidth: true
    Layout.margins: theme.spacingL
    spacing: theme.spacingM

    // 标题列：标题 + 副标题靠左堆叠。
    ColumnLayout {
        spacing: theme.spacingXs

        Label {
            visible: control.title.length > 0
            text: control.title
            color: theme.textPrimary
            font.pixelSize: theme.fontSizePageTitle
            font.bold: true
        }
        Label {
            visible: control.subtitle.length > 0
            text: control.subtitle
            color: theme.textMuted
            font.pixelSize: theme.fontSizeBody
        }
    }

    // 弹簧项：把动作推到行右缘。
    Item {
        Layout.fillWidth: true
    }

    // 经 actions 别名注入的页面级动作行。
    RowLayout {
        id: actionsSlot
        spacing: theme.spacingS
    }
}
