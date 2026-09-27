import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// The sidebar: "where to go", never a business action.
// Renders the navigation model grouped by section, with badges, keyboard
// hints and a collapse handle at the bottom.
Rectangle {
    id: sidebar

    // Kept in sync with ShellController by MainWindow.
    property bool collapsed: false

    color: theme.sidebarBg
    // Expanded width: window.sidebarWidth from settings (ShellController
    // owns it), falling back to the theme token when the
    // key is explicitly cleared (0). Both default to 240.
    width: collapsed ? theme.sidebarCollapsedWidth
                     : (shell.sidebarWidth > 0 ? shell.sidebarWidth
                                               : theme.sidebarWidth)

    Behavior on width {
        NumberAnimation { duration: theme.durationNormal }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Header: app icon + name ------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 48

            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: theme.spacingS

                Image {
                    source: "qrc:/icons/app-icon.png"
                    sourceSize: Qt.size(20, 20)
                    fillMode: Image.PreserveAspectFit
                }
                Label {
                    visible: !sidebar.collapsed
                    text: "AgentWorkbench"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                    font.bold: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingS
            Layout.rightMargin: theme.spacingS
            height: 1
            color: theme.separator
        }

        // --- Scrollable page list (one repeater; the model is already
        // sorted by section + order, and each row draws the divider for a
        // new non-main section above itself) -----------------------------
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: rowsColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: rowsColumn
                width: flick.width
                spacing: 0

                Repeater {
                    model: nav
                    delegate: navRow
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingS
            Layout.rightMargin: theme.spacingS
            height: 1
            color: theme.separator
        }

        // --- Collapse handle ---------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 40

            AIconButton {
                anchors.right: parent.right
                anchors.rightMargin: theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                iconSource: sidebar.collapsed ? "qrc:/icons/chevron-right.svg"
                                              : "qrc:/icons/chevron-left.svg"
                tooltip: sidebar.collapsed ? qsTr("Expand sidebar")
                                           : qsTr("Collapse sidebar")
                onClicked: shell.sidebarCollapsed = !shell.sidebarCollapsed
            }
        }
    }

    Component {
        id: navRow

        Item {
            id: row

            // Divider + gap above the first row of every non-main section
            readonly property bool firstInSection:
                index === nav.rowOfFirstInSection(model.section)
            readonly property bool needsDivider:
                firstInSection && model.section !== "main"

            Layout.fillWidth: true
            Layout.preferredHeight: visible ? (36 + (needsDivider ? 9 : 0)) : 0
            Layout.topMargin: firstInSection && model.section === "system"
                              ? theme.spacingL : 0
            visible: model.enabled

            Rectangle {
                visible: row.needsDivider
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: theme.spacingS
                anchors.rightMargin: theme.spacingS
                height: 1
                color: theme.separator
            }

            // Active background + 3px accent bar.
            Rectangle {
                anchors.top: row.needsDivider ? dividerSpace.bottom : parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: theme.spacingS
                anchors.rightMargin: theme.spacingS
                height: 36
                radius: theme.radiusControl
                color: nav.currentPageId === model.pageId ? theme.surfaceBg
                                                          : (rowMouse.containsMouse
                                                             ? theme.surfaceHoverBg
                                                             : "transparent")
                Behavior on color { ColorAnimation { duration: theme.durationFast } }
            }
            Item {
                id: dividerSpace
                visible: false
                anchors.top: parent.top
                height: row.needsDivider ? 9 : 0
            }
            Rectangle {
                anchors.top: row.needsDivider ? dividerSpace.bottom : parent.top
                anchors.left: parent.left
                width: 3
                height: 36
                radius: 1
                visible: nav.currentPageId === model.pageId
                color: theme.accent
            }

            RowLayout {
                anchors.top: row.needsDivider ? dividerSpace.bottom : parent.top
                anchors.left: parent.left
                anchors.leftMargin: sidebar.collapsed ? theme.spacingS
                                                      : theme.spacingM
                anchors.right: parent.right
                anchors.rightMargin: theme.spacingS
                height: 36
                spacing: theme.spacingS

                Item {
                    Layout.alignment: Qt.AlignVCenter
                    width: 24
                    height: 24
                    Image {
                        anchors.centerIn: parent
                        source: model.iconSource
                        sourceSize: Qt.size(18, 18)
                        fillMode: Image.PreserveAspectFit
                    }
                }
                Label {
                    visible: !sidebar.collapsed
                    Layout.fillWidth: true
                    text: model.title
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                    elide: Text.ElideRight
                }
                APill {
                    visible: !sidebar.collapsed && model.badgeText.length > 0
                    text: model.badgeText
                }
            }

            MouseArea {
                id: rowMouse
                anchors.top: row.needsDivider ? dividerSpace.bottom : parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 36
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: nav.setCurrentPageId(model.pageId)
            }

            // Collapsed state: tooltip with the page title.
            ToolTip.visible: sidebar.collapsed && rowMouse.containsMouse
            ToolTip.delay: 300
            ToolTip.text: model.title
        }
    }
}
