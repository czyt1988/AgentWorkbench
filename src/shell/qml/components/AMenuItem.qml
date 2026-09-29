import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 主题化菜单条目：图标槽（16px，恒占位让文字列对齐）+ 文字，悬停
// 高亮是内缩的圆角 accent 半透明块。AMenu 的配套件，不要在页面里
// 混用裸 MenuItem（Default 样式的悬停色取 palette.light——硬编码
// 近白，深色主题下配浅色文字不可读，0.4.0 的实际症状）。
//
// 图标经内建 icon 组声明（icon.source: "qrc:/icons/..."），尺寸组件
// 内定死 16px；无图标时槽位保留，文字与有图标条目对齐（Windows 原生
// 菜单的惯例）。
MenuItem {
    id: control

    icon.width: 16
    icon.height: 16

    implicitHeight: 30
    leftPadding: 10
    rightPadding: 12

    // 自绘内容行：颜色与间距全部自管，不依赖模板 palette。
    contentItem: Row {
        spacing: theme.spacingS

        // 图标槽：恒占 16px，文字列因此对齐。
        Item {
            width: 16
            height: 16
            anchors.verticalCenter: parent.verticalCenter

            Image {
                anchors.fill: parent
                visible: control.icon.source.toString().length > 0
                source: control.icon.source
                sourceSize: Qt.size(16, 16)
                fillMode: Image.PreserveAspectFit
                opacity: control.enabled ? 1 : 0.5
            }
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.enabled ? theme.textPrimary : theme.textDisabled
            font.pixelSize: theme.fontSizeBody
        }
    }

    // 悬停/键盘高亮：accent 半透明圆角块，内缩 2px 与菜单玻璃圆角嵌套。
    background: Rectangle {
        x: 2
        y: 2
        width: control.width - 4
        height: control.height - 4
        radius: theme.radiusControl
        color: control.down ? theme.alpha(theme.accent, 0.30)
                            : (control.highlighted
                               ? theme.alpha(theme.accent, 0.22)
                               : "transparent")
        visible: control.highlighted || control.down
    }
}
