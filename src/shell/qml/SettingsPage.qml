import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The settings shell: a fixed navigation column on the left and one paged
// StackLayout on the right — pick a section on the left, that section's page
// shows on the right. Each section is its own Settings<Section>Page.qml
// (same ScrollView + ColumnLayout + PageHeader skeleton) and owns its
// dialogs; the pages stay instantiated while switching, so half-typed
// input (e.g. a new skill root) survives a round trip.
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
    property string currentSection: "appearance"

    function sectionIndex(id) {
        for (let i = 0; i < page.sections.length; ++i)
            if (page.sections[i].id === id)
                return i
        return 0
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Left: section navigation --------------------------------------
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
            }
        }

        // --- Right: one section page at a time ----------------------------
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
