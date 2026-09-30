import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·环境分区：Python / Node.js 各占一行（版本 + 安装路径），手动
// 重测在页头。
//
// 行的三态来自 environment.*Status：found（可用，显示版本与路径）、
// missing（权威判定没有，红字）、unknown（没拿到结论——首次探测在途或
// 最近一轮失败）。unknown 不能画成 missing：那正是「偶尔报红叉」的来源。
ScrollView {
    id: page

    contentWidth: availableWidth
    ScrollBar.vertical: AScrollBar {}

    ColumnLayout {
        width: page.availableWidth
        spacing: theme.spacingM

        PageHeader {
            title: qsTr("Environment")
            subtitle: qsTr("Runtimes used by the agents' setup commands")

            AButton {
                text: qsTr("Re-detect")
                busy: environment.detecting
                onClicked: environment.refresh()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingL
            spacing: theme.spacingS

            AListRow {
                id: pythonRow

                Layout.fillWidth: true
                rowHeight: 44

                readonly property string status: environment.pythonStatus
                // 最近一轮探测的排查记录；只有「检测失败」时会展示。
                readonly property string detail: environment.pythonProbeDetail
                // 行标题：版本 / 未找到 / 正在检测 / 检测失败。
                readonly property string title: {
                    if (status === "found")
                        return qsTr("Python %1").arg(environment.pythonVersion)
                    if (status === "missing")
                        return qsTr("Python not found")
                    return environment.detecting ? qsTr("Python: checking...")
                                                 : qsTr("Python: detection failed")
                }
                // 没结论时不给红字：红叉只留给权威判定。
                readonly property color titleColor: {
                    if (status === "found")
                        return theme.textPrimary
                    if (status === "missing")
                        return theme.danger
                    return theme.textMuted
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        text: pythonRow.title
                        color: pythonRow.titleColor
                        font.pixelSize: theme.fontSizeBody

                        // 只有「检测失败」有额外的话要说：把探测试过什么、
                        // 卡在哪摆出来（成功与未找到看文字就够了）。
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            ToolTip.text: pythonRow.detail
                            ToolTip.visible: containsMouse
                                           && pythonRow.status === "unknown"
                                           && pythonRow.detail.length > 0
                            ToolTip.delay: 300
                            ToolTip.timeout: 10000
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: environment.pythonPath
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                        font.family: theme.monoFamily
                        elide: Text.ElideMiddle
                    }
                }
            }

            AListRow {
                id: nodeRow

                Layout.fillWidth: true
                rowHeight: 44

                readonly property string status: environment.nodeStatus
                // 最近一轮探测的排查记录；只有「检测失败」时会展示。
                readonly property string detail: environment.nodeProbeDetail
                readonly property string title: {
                    if (status === "found")
                        return qsTr("Node.js %1").arg(environment.nodeVersion)
                    if (status === "missing")
                        return qsTr("Node.js not found")
                    return environment.detecting ? qsTr("Node.js: checking...")
                                                 : qsTr("Node.js: detection failed")
                }
                readonly property color titleColor: {
                    if (status === "found")
                        return theme.textPrimary
                    if (status === "missing")
                        return theme.danger
                    return theme.textMuted
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        text: nodeRow.title
                        color: nodeRow.titleColor
                        font.pixelSize: theme.fontSizeBody

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            ToolTip.text: nodeRow.detail
                            ToolTip.visible: containsMouse
                                           && nodeRow.status === "unknown"
                                           && nodeRow.detail.length > 0
                            ToolTip.delay: 300
                            ToolTip.timeout: 10000
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: environment.nodePath
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                        font.family: theme.monoFamily
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
}
