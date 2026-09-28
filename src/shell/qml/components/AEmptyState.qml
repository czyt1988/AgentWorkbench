import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Empty state: icon + title + description + optional extra content +
// action button. The root is a plain Item — layout-managed, so it must not
// carry anchors (that would be undefined behavior); the column centers
// itself inside.
//
// 内层容器用普通 Column（positioner）而不是 ColumnLayout：Layout 子项的
// 高度走「隐式高 → 高度自动跟随 → geometryChanged 同步重排」，叠加描述
// Label 的 WordWrap（height-for-width）后，宿主首次拿到最终尺寸时同步
// 重入会叠到第三层，Qt 5 报 "Detected recursive rearrange"（Tools 页文件
// 树空态、Web 页空态启动即触发）。Column 没有 rearrange 机制，从根上消
// 除重入；需要 Layout.* 的注入内容放进 extraSlot（它仍是 ColumnLayout）。
Item {
    id: control

    property string iconSource: ""
    property string title: ""
    property string description: ""
    property string actionText: ""
    // Extra content between description and action button (lists, hints).
    property alias extra: extraSlot.data
    signal actionClicked()

    Column {
        id: content

        anchors.centerIn: parent
        spacing: theme.spacingM
        width: Math.min(parent.width - theme.spacingXl, 520)

        Image {
            // Column 只管垂直位置，水平锚允许（positioner 约定）。
            anchors.horizontalCenter: parent.horizontalCenter
            source: control.iconSource
            sourceSize: Qt.size(48, 48)
            fillMode: Image.PreserveAspectFit
            opacity: 0.7
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: control.title.length > 0
            text: control.title
            color: theme.textPrimary
            font.pixelSize: theme.fontSizeSubtitle
            font.bold: true
        }

        Label {
            // 宽绑所在 Column 的宽度：WordWrap 的 height-for-width 只随列宽
            // 变化，不与任何重排互相反馈。
            width: content.width
            visible: control.description.length > 0
            text: control.description
            color: theme.textMuted
            font.pixelSize: theme.fontSizeBody
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        ColumnLayout {
            id: extraSlot

            width: content.width
            spacing: theme.spacingM
            visible: children.length > 0
        }

        AButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: control.actionText.length > 0
            variant: "primary"
            text: control.actionText
            onClicked: control.actionClicked()
        }
    }
}
