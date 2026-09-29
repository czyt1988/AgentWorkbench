import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页外壳：左侧固定的分区导航列 + 右侧单个分页的 StackLayout——左
// 边选分区，右边显示该分区的页面。每个分区是独立的 Settings<Section>
// Page.qml（共用 ScrollView + ColumnLayout + PageHeader 骨架）并自带
// 弹窗；切换时分区页保持实例化，输入到一半的内容（如新填的 skill 根
// 目录）往返一趟不丢失。导航列最底部钉一条应用版本号（原状态栏右侧
// 信息，随环境徽标重排移进设置）。
Page {
    id: page

    background: Rectangle { color: theme.workspaceBg }

    // 分区表：导航与 StackLayout 的顺序由它单点决定。
    readonly property var sections: [
        { id: "appearance", title: qsTr("Appearance") },
        { id: "launchers", title: qsTr("Launchers") },
        { id: "environment", title: qsTr("Environment") },
        { id: "skills", title: qsTr("Skills") },
        { id: "web", title: qsTr("Web") },
        { id: "plugins", title: qsTr("Plugins") },
        { id: "advanced", title: qsTr("Advanced") }
    ]
    // 当前选中的分区 id。
    property string currentSection: "appearance"

    // 分区 id → StackLayout 下标（未命中回退到 0）。
    function sectionIndex(id) {
        for (let i = 0; i < page.sections.length; ++i)
            if (page.sections[i].id === id)
                return i
        return 0
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- 左：分区导航 -------------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: theme.sidebarBg

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item { Layout.preferredHeight: theme.spacingM }

                Repeater {
                    model: page.sections

                    delegate: Item {
                        id: navRow

                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        Layout.leftMargin: theme.spacingS
                        Layout.rightMargin: theme.spacingS

                        // 行销毁时共享 tooltip 可能冻结在屏上（侧栏同款问题）。
                        Component.onDestruction: ToolTip.hide()

                        Rectangle {
                            anchors.fill: parent
                            radius: theme.radiusControl
                            color: page.currentSection === navRow.modelData.id
                                   ? theme.surfaceBg
                                   : (navMouse.containsMouse
                                      ? theme.surfaceHoverBg
                                      : "transparent")
                            Behavior on color {
                                ColorAnimation {
                                    duration: theme.durationFast
                                }
                            }
                        }
                        Rectangle {
                            visible: page.currentSection === navRow.modelData.id
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3
                            height: 24
                            radius: 1
                            color: theme.accent
                        }
                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: theme.spacingM
                            verticalAlignment: Text.AlignVCenter
                            text: navRow.modelData.title
                            color: page.currentSection === navRow.modelData.id
                                   ? theme.textPrimary
                                   : theme.textSecondary
                            font.pixelSize: theme.fontSizeBody
                        }
                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked:
                                page.currentSection = navRow.modelData.id
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // --- 钉底：应用版本号（与主侧栏 footer 同款「分割线 + 钉底」）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: theme.spacingS
                    Layout.rightMargin: theme.spacingS
                    height: 1
                    color: theme.separator
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: theme.spacingM
                    Layout.topMargin: theme.spacingS
                    Layout.bottomMargin: theme.spacingM
                    text: "v" + Qt.application.version
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                }
            }
        }

        // --- 右：同一时刻显示一个分区页 -----------------------------------------
        // 分区页保持实例化（见顶部说明），切换只是换 currentIndex。
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.sectionIndex(page.currentSection)

            SettingsAppearancePage {}
            SettingsLaunchersPage {}
            SettingsEnvironmentPage {}
            SettingsSkillsPage {}
            SettingsWebPage {}
            SettingsPluginsPage {}
            SettingsAdvancedPage {}
        }
    }
}
