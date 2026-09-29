import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 主题化菜单分组线：上下各半档呼吸，线体 1px separator 色、左右收窄。
// AMenu 的配套件（见 AMenu 头注释）。
MenuSeparator {
    contentItem: Item {
        implicitHeight: 9

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            implicitHeight: 1
            color: theme.separator
        }
    }
}
