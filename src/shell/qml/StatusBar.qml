import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Status bar (specs/02 §2.1): runtime badges on the left, activity counts
// in the middle, app version on the right. Height = theme.statusBarHeight.
Rectangle {
    id: statusBar

    color: theme.chromeBg
    height: theme.statusBarHeight

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.spacingM
        anchors.rightMargin: theme.spacingM
        spacing: theme.spacingM

        // --- Runtime badges (Python / Node.js) --------------------------
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
                }
            }
        }

        Item { Layout.fillWidth: true }

        // --- Activity counts ---------------------------------------------
        Label {
            // The agents badge already counts running agents (BuiltinPages).
            readonly property string running: nav.page("agents").badgeText
            text: running.length > 0
                  ? qsTr("Running: %1").arg(running)
                  : qsTr("Running: 0")
            color: theme.textSecondary
            font.pixelSize: theme.fontSizeCaption
        }
        Label {
            visible: nav.countInSection("web") > 0
                     && nav.page("web").badgeText.length > 0
            text: qsTr("Tabs: %1").arg(nav.page("web").badgeText)
            color: theme.textSecondary
            font.pixelSize: theme.fontSizeCaption
        }

        Item { Layout.fillWidth: true }

        // --- Version -----------------------------------------------------
        Label {
            text: "v" + Qt.application.version
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
        }
    }
}
