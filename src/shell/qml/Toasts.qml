import QtQuick
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 通知宿主：把 toast 栈锚到窗口右下角（内容与排队逻辑在 AToastStack）。
AToastStack {
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.margins: theme.spacingL
}
