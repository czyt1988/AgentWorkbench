import QtQuick
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页/列表的单行：surface 背景、控件圆角、细边框 + 带标准行边距的
// 内容 RowLayout；图标/文本列/尾部控件作为子项注入，行高默认取
// rowHeight（48），也可按实例覆盖。
// implicitHeight 镜像显式 height：布局父（如 Flickable content 里的
// ColumnLayout，Web 页的运行中列表）按 implicit* 给子项定尺寸，缺了它
// 行会塌缩到 ~0 并被裁掉。
Rectangle {
    id: control

    // 行内容插槽：注入的图标/文本/尾部控件落在内部 RowLayout 里。
    default property alias contentData: rowLayout.data

    // 行高；隐式高跟随它，保证在布局里不塌缩。
    property real rowHeight: 48

    radius: theme.radiusControl
    color: theme.surfaceBg
    border.color: theme.borderSubtle
    border.width: 1
    height: rowHeight
    implicitHeight: rowHeight

    RowLayout {
        id: rowLayout
        anchors.fill: parent
        anchors.leftMargin: theme.spacingM
        anchors.rightMargin: theme.spacingS
        spacing: theme.spacingM
    }
}
