import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 底部状态栏：中部活动计数（运行中的 agent、Web 标签数），最右是运行
// 环境徽标（Python/Node.js）。应用版本号不在状态栏展示——移到了设置页
// 侧栏的钉底（见 SettingsPage）。高度取 theme.statusBarHeight。
Rectangle {
    id: statusBar

    color: theme.chromeBg
    // 布局子项（ColumnLayout 里）经 implicitHeight 提供尺寸：直接绑
    // height 会与布局的重分配互相触发，Qt 5 的 Layouts 引擎因此报
    // "recursive rearrange"（Qt 6 容忍了这种写法）。
    implicitHeight: theme.statusBarHeight

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.spacingM
        anchors.rightMargin: theme.spacingM
        spacing: theme.spacingM

        Item { Layout.fillWidth: true }

        // --- 活动计数 -------------------------------------------------------
        Label {
            // agents 徽标已是「运行中的 agent 数」（BuiltinPages 维护）。
            // nav.badges 是带 NOTIFY 的映射——page("agents") 是方法调用，
            // 徽标变化时那种绑定永远不会重算。
            readonly property string running: nav.badges["agents"] || ""
            text: running.length > 0
                  ? qsTr("Running: %1").arg(running)
                  : qsTr("Running: 0")
            color: theme.textSecondary
            font.pixelSize: theme.fontSizeCaption
        }
        Label {
            readonly property string tabs: nav.badges["web"] || ""
            visible: nav.countInSection("web") > 0 && tabs.length > 0
            text: qsTr("Tabs: %1").arg(tabs)
            color: theme.textSecondary
            font.pixelSize: theme.fontSizeCaption
        }

        Item { Layout.fillWidth: true }

        // --- 运行环境徽标（Python / Node.js）：钉在状态栏最右 --------------
        Row {
            spacing: theme.spacingS

            Rectangle {
                id: pythonBadge
                readonly property bool installed: environment.pythonInstalled
                readonly property string version: environment.pythonVersion
                radius: theme.radiusPill
                implicitWidth: pyBadgeLayout.implicitWidth + theme.spacingL
                implicitHeight: 18
                color: theme.badgeBg
                border.color: installed ? theme.borderSubtle : theme.danger
                border.width: 1

                RowLayout {
                    id: pyBadgeLayout
                    anchors.centerIn: parent
                    spacing: theme.spacingXs

                    Text {
                        text: "Python"
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                        font.bold: true
                    }
                    Text {
                        text: pythonBadge.installed ? pythonBadge.version
                                                    : "×"
                        color: pythonBadge.installed ? theme.success
                                                     : theme.danger
                        font.pixelSize: theme.fontSizeCaption
                        font.bold: true
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    ToolTip.text: pythonBadge.installed
                        ? qsTr("Python %1").arg(pythonBadge.version)
                        : qsTr("Python is not installed or not in PATH. Agents requiring Python may not work.")
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                }
            }

            Rectangle {
                id: nodeBadge
                readonly property bool installed: environment.nodeInstalled
                readonly property string version: environment.nodeVersion
                radius: theme.radiusPill
                implicitWidth: nodeBadgeLayout.implicitWidth + theme.spacingL
                implicitHeight: 18
                color: theme.badgeBg
                border.color: installed ? theme.borderSubtle : theme.danger
                border.width: 1

                RowLayout {
                    id: nodeBadgeLayout
                    anchors.centerIn: parent
                    spacing: theme.spacingXs

                    Text {
                        text: "Node"
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                        font.bold: true
                    }
                    Text {
                        text: nodeBadge.installed ? nodeBadge.version : "×"
                        color: nodeBadge.installed ? theme.success : theme.danger
                        font.pixelSize: theme.fontSizeCaption
                        font.bold: true
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    ToolTip.text: nodeBadge.installed
                        ? qsTr("Node.js %1").arg(nodeBadge.version)
                        : qsTr("Node.js is not installed or not in PATH. Agents requiring Node.js may not work.")
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                }
            }
        }
    }
}
