import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 底部状态栏：只占工作区列（侧栏整条通到窗口底部，状态栏从侧栏右缘
// 开始）。中部活动计数（运行中的 agent 数），最右是运行环境徽标
// （Python/Node.js）。应用版本号不在状态栏展示——移到了设置页侧栏的
// 钉底（见 SettingsPage）。高度取 theme.statusBarHeight。
// Web 标签数只在侧栏徽标里显示（BuiltinPages 写 nav 徽标），状态栏
// 不再重复一份。
Rectangle {
    id: statusBar

    color: theme.chromeBg
    // 布局子项（ColumnLayout 里）经 implicitHeight 提供尺寸：直接绑
    // height 会与布局的重分配互相触发，Qt 5 的 Layouts 引擎因此报
    // "recursive rearrange"（Qt 6 容忍了这种写法）。
    implicitHeight: theme.statusBarHeight

    // 与工作区的分界线：侧栏玻璃化后外壳各面的色差整体变弱，顶边补
    // 一条细线让「工作区 → 状态栏」的换行干净。
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: theme.separator
    }

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

        Item { Layout.fillWidth: true }

        // --- 运行环境徽标（Python / Node.js）：钉在状态栏最右 --------------
        Row {
            spacing: theme.spacingS

            Rectangle {
                id: pythonBadge
                readonly property bool installed: environment.pythonInstalled
                readonly property string version: environment.pythonVersion
                // 没拿到结论（首次探测在途 / 最近一轮失败）不是「没装」：
                // 红叉只留给权威判定，否则一次慢启动就假报故障。
                readonly property bool unknown: environment.pythonStatus === "unknown"
                radius: theme.radiusPill
                implicitWidth: pyBadgeLayout.implicitWidth + theme.spacingL
                implicitHeight: 18
                color: theme.badgeBg
                border.color: installed ? theme.borderSubtle
                            : unknown ? theme.borderSubtle
                            : theme.danger
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
                            : pythonBadge.unknown ? "…"
                            : "×"
                        color: pythonBadge.installed ? theme.success
                             : pythonBadge.unknown ? theme.textMuted
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
                        : pythonBadge.unknown
                          ? environment.detecting
                            ? qsTr("Python: checking...")
                            : qsTr("Python: detection failed")
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
                // 没拿到结论不是「没装」，同 Python 徽标。
                readonly property bool unknown: environment.nodeStatus === "unknown"
                radius: theme.radiusPill
                implicitWidth: nodeBadgeLayout.implicitWidth + theme.spacingL
                implicitHeight: 18
                color: theme.badgeBg
                border.color: installed ? theme.borderSubtle
                            : unknown ? theme.borderSubtle
                            : theme.danger
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
                        text: nodeBadge.installed ? nodeBadge.version
                            : nodeBadge.unknown ? "…"
                            : "×"
                        color: nodeBadge.installed ? theme.success
                             : nodeBadge.unknown ? theme.textMuted
                             : theme.danger
                        font.pixelSize: theme.fontSizeCaption
                        font.bold: true
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    ToolTip.text: nodeBadge.installed
                        ? qsTr("Node.js %1").arg(nodeBadge.version)
                        : nodeBadge.unknown
                          ? environment.detecting
                            ? qsTr("Node.js: checking...")
                            : qsTr("Node.js: detection failed")
                          : qsTr("Node.js is not installed or not in PATH. Agents requiring Node.js may not work.")
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                }
            }
        }
    }
}
