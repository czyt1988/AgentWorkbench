import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// The sidebar: "where to go", never a business action.
// Renders the navigation model grouped by section, with badges, keyboard
// hints and a footer of system-page icon buttons plus the collapse handle.
//
// Layout (fixed): workflow pages (sections main/extensions) scroll in the
// middle; system pages (section system, e.g. Settings) are PINNED to the
// bottom as icon-only buttons sharing the footer row with the collapse
// handle — they stay at the bottom no matter how long the list grows
// (see designs.md).
Rectangle {
    id: sidebar

    // Kept in sync with ShellController by MainWindow.
    property bool collapsed: false

    color: theme.sidebarBg
    // Expanded width: window.sidebarWidth from settings (ShellController
    // owns it), falling back to the theme token when the
    // key is explicitly cleared (0). Both default to 240.
    // implicitWidth, NOT width: this item is managed by the RowLayout in
    // MainWindow, and layouts re-distribute from implicit-size changes —
    // a child's width changing behind the layout's back left the workspace
    // frozen at its old size with a gap next to the collapsed sidebar.
    implicitWidth: collapsed ? theme.sidebarCollapsedWidth
                             : (shell.sidebarWidth > 0 ? shell.sidebarWidth
                                                       : theme.sidebarWidth)

    Behavior on implicitWidth {
        NumberAnimation { duration: theme.durationNormal }
    }

    // One navigation row in the scrollable workflow list (sections
    // main/extensions). Each row draws the divider for a new non-main
    // section above itself; system pages render in the footer below
    // instead of here.
    component NavRow: Item {
        id: row

        readonly property bool firstInSection:
            index === nav.rowOfFirstInSection(model.section)
        readonly property bool needsDivider:
            firstInSection && model.section !== "main"

        Layout.fillWidth: true
        Layout.preferredHeight: visible ? (36 + (needsDivider ? 9 : 0)) : 0
        visible: model.enabled && model.section !== "system"
        // Rows are delegates (page plugins come and go) — a row dying
        // while hovered must not freeze the shared tooltip on screen.
        Component.onDestruction: ToolTip.hide()

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
            // Collapsed: tighter margins keep the highlight a squarish
            // pill around the centered icon in the narrow sidebar.
            anchors.leftMargin: sidebar.collapsed ? theme.spacingXs
                                                  : theme.spacingS
            anchors.rightMargin: sidebar.collapsed ? theme.spacingXs
                                                   : theme.spacingS
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
            // Collapsed: center the 24px icon slot (its width is set on
            // the slot below) in the icon-width sidebar.
            anchors.leftMargin: sidebar.collapsed
                ? (theme.sidebarCollapsedWidth - 24) / 2
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
        ToolTip.timeout: 10000
        ToolTip.text: model.title
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

        // --- Scrollable workflow list (sections main + extensions; the
        // model is already sorted by section + order, and each row draws
        // the divider for a new non-main section above itself) ------------
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: rowsColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AScrollBar {}

            ColumnLayout {
                id: rowsColumn
                width: flick.width
                spacing: 0

                Repeater {
                    model: nav
                    NavRow {}
                }
            }
        }

        // --- Footer: system-page icons + collapse handle ------------------
        // System pages (Settings & friends) leave the workflow list and
        // render as icon-only buttons (the tooltip carries the title)
        // sharing the footer row with the collapse handle. Expanded:
        // icons on the left, handle on the right. Collapsed: icons
        // stacked above the handle, everything centered in the
        // icon-width sidebar.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingS
            Layout.rightMargin: theme.spacingS
            height: 1
            color: theme.separator
        }

        Item {
            id: footer

            // 28 = AIconButton's normal implicit size; keep in sync with
            // it. buttonGap paces the vertical stack and the footer edge
            // padding when collapsed.
            readonly property int buttonSize: 28
            readonly property int buttonGap: 6

            readonly property int systemCount: nav.countInSection("system")

            Layout.fillWidth: true
            Layout.preferredHeight: sidebar.collapsed
                ? 2 * footer.buttonGap
                  + footer.systemCount * (footer.buttonSize + footer.buttonGap)
                  + footer.buttonSize
                : 40

            Repeater {
                model: nav
                AIconButton {
                    // Stacking slot among the system pages (the model is
                    // sorted by section, system last).
                    readonly property int systemIndex:
                        index - nav.rowOfFirstInSection("system")

                    visible: model.section === "system"
                    iconSource: model.iconSource
                    tooltip: model.title
                    active: nav.currentPageId === model.pageId
                    onClicked: nav.setCurrentPageId(model.pageId)
                    // Delegates die with the model (pages come and go) —
                    // a button dying while hovered must not freeze the
                    // shared tooltip on screen.
                    Component.onDestruction: ToolTip.hide()

                    x: sidebar.collapsed
                       ? (footer.width - width) / 2
                       : theme.spacingS
                         + systemIndex * (footer.buttonSize + footer.buttonGap)
                    y: sidebar.collapsed
                       ? footer.buttonGap
                         + systemIndex * (footer.buttonSize + footer.buttonGap)
                       : (footer.height - height) / 2
                }
            }

            AIconButton {
                iconSource: sidebar.collapsed ? "qrc:/icons/chevron-right.svg"
                                              : "qrc:/icons/chevron-left.svg"
                tooltip: sidebar.collapsed ? qsTr("Expand sidebar")
                                           : qsTr("Collapse sidebar")
                onClicked: shell.sidebarCollapsed = !shell.sidebarCollapsed

                x: sidebar.collapsed
                   ? (footer.width - width) / 2
                   : footer.width - width - theme.spacingS
                y: sidebar.collapsed
                   ? footer.buttonGap
                     + footer.systemCount * (footer.buttonSize + footer.buttonGap)
                   : (footer.height - height) / 2
            }
        }
    }
}
