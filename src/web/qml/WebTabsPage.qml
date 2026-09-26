import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The Web page (specs/02 §6): its own tab bar serves as the page header,
// the body hosts one surface per tab, and the empty state lists running
// agents with one-click open.
Item {
    id: page

    // Fullscreen hides the tab bar (02 §6.4); Esc leaves it first.
    property bool chromeHidden: false
    // Loaded surface items by tab id — the toolbar reaches the active one
    // (devtools lives on the surface).
    property var surfaceItems: ({})
    property int runningCount: 0

    function recountRunning() {
        let n = 0
        for (const child of runningList.children) {
            if (child.visible && child.height > 0)
                ++n
        }
        page.runningCount = n
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Tab bar (the page header, 02 §6.1) --------------------------
        Rectangle {
            Layout.fillWidth: true
            visible: !page.chromeHidden
            height: visible ? theme.tabBarHeight : 0
            color: theme.chromeBg

            // Bottom separator (02 §6.1: 下边框 theme.separator).
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: theme.separator
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: tabsRow.implicitWidth
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: tabsRow
                        height: parent.height

                        Repeater {
                            model: web.model
                            delegate: Rectangle {
                                id: tabButton

                                required property string tabId
                                required property string title
                                required property string url
                                required property string state
                                required property string color
                                required property string iconSource
                                required property int loadProgress
                                required property string surfaceKind
                                property bool active: web.activeTabId === tabId

                                width: Math.min(Math.max(
                                        tabLabel.implicitWidth
                                        + theme.spacingL * 2
                                        + (closeArea.visible ? 20 : 0),
                                        96), 200)
                                height: theme.tabBarHeight
                                color: active ? theme.tabActiveBg
                                              : (tabMouse.containsMouse
                                                 ? theme.surfaceHoverBg
                                                 : theme.tabInactiveBg)

                                // Active tab: 2px accent bar on top (02 §6.2).
                                Rectangle {
                                    visible: tabButton.active
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 2
                                    color: theme.accent
                                }

                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: theme.spacingS
                                    anchors.rightMargin: theme.spacingS
                                    spacing: theme.spacingXs

                                    // Status dot (never color-only: tooltip
                                    // below carries the text state).
                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: theme.radiusPill
                                        color: {
                                            if (state === "offline")
                                                return theme.neutralOff
                                            if (state === "crashed"
                                                || state === "error")
                                                return theme.danger
                                            return tabButton.color.length > 0
                                                   ? tabButton.color
                                                   : theme.accent
                                        }
                                    }

                                    // Tab icon, 16px (02 §6.2): the agent
                                    // icon, falling back to web.svg.
                                    Image {
                                        width: 16
                                        height: 16
                                        source: tabButton.iconSource.length > 0
                                                ? tabButton.iconSource
                                                : "qrc:/icons/web.svg"
                                        sourceSize: Qt.size(16, 16)
                                        fillMode: Image.PreserveAspectFit
                                    }

                                    Label {
                                        id: tabLabel
                                        Layout.fillWidth: true
                                        text: title.length > 0 ? title
                                             : String(url).replace(/^https?:\/\//, "").replace(/[?#].*$/, "")
                                        color: active ? theme.textPrimary
                                                      : theme.textMuted
                                        font.pixelSize: theme.fontSizeSmall
                                        elide: Text.ElideMiddle
                                    }

                                    // Busy indicator while loading (02 §6.2).
                                    BusyIndicator {
                                        visible: state === "loading"
                                        running: visible
                                        implicitWidth: 12
                                        implicitHeight: 12
                                    }

                                    Item {
                                        id: closeArea
                                        width: visible ? 16 : 0
                                        height: 16
                                        visible: tabButton.active
                                                 || tabMouse.containsMouse

                                        Image {
                                            anchors.centerIn: parent
                                            source: "qrc:/icons/close.svg"
                                            sourceSize: Qt.size(10, 10)
                                            fillMode: Image.PreserveAspectFit
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: web.closeTab(tabButton.tabId)
                                        }
                                    }
                                }

                                // Load progress line at the bar bottom.
                                Rectangle {
                                    visible: state === "loading"
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    height: 2
                                    width: parent.width * loadProgress / 100
                                    color: theme.accent
                                }

                                MouseArea {
                                    id: tabMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // Middle click must be ACCEPTED, or the
                                    // button never reaches the handlers (02 §6.2).
                                    acceptedButtons: Qt.LeftButton
                                                     | Qt.MiddleButton
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.MiddleButton)
                                            web.closeTab(tabButton.tabId)
                                        else
                                            web.activateTab(tabButton.tabId)
                                    }
                                    onDoubleClicked: function(mouse) {
                                        if (mouse.button === Qt.LeftButton)
                                            web.reloadTab(tabButton.tabId)
                                    }
                                }

                                ToolTip.visible: tabMouse.containsMouse
                                ToolTip.delay: 300
                                ToolTip.text: state === "offline"
                                              ? qsTr("This agent is not running")
                                              : state === "crashed"
                                                ? qsTr("The page crashed")
                                                : state === "error"
                                                  ? qsTr("Failed to load the page")
                                                  : title
                            }
                        }
                    }
                }

                // --- Toolbar (acts on the active tab, 02 §6.1) -----------
                Row {
                    spacing: theme.spacingXs
                    rightPadding: theme.spacingS

                    // ⟳ reload / ✕ stop — the button follows the active
                    // tab's state (02 §6.1).
                    AIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: web.activeState === "loading"
                                    ? "qrc:/icons/close.svg"
                                    : "qrc:/icons/refresh.svg"
                        tooltip: web.activeState === "loading"
                                 ? qsTr("Stop loading") : qsTr("Reload")
                        enabled: web.activeTabId.length > 0
                        onClicked: {
                            if (web.activeState === "loading") {
                                const item = page.surfaceItems[web.activeTabId]
                                if (item)
                                    item.stopLoading()
                            } else {
                                web.reloadTab(web.activeTabId)
                            }
                        }
                    }
                    // "Open in browser" stays visible at all times — the
                    // escape hatch must never be hidden (02 §6.7).
                    AIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: "qrc:/icons/external-link.svg"
                        tooltip: qsTr("Open in browser")
                        enabled: web.activeTabId.length > 0
                        onClicked: web.openExternal(web.activeTabId)
                    }
                    // Qt Quick Controls 2 has no MenuButton type (it was a
                    // Qt 5 Controls 1 thing) — a Button popping the menu
                    // is the supported shape.
                    Button {
                        id: tabMenuButton
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: web.activeTabId.length > 0
                        implicitWidth: 28
                        implicitHeight: 28
                        padding: 0
                        background: Rectangle {
                            radius: theme.radiusControl
                            color: parent.hovered || tabMenu.opened
                                   ? theme.surfaceHoverBg : "transparent"
                        }
                        contentItem: Image {
                            source: "qrc:/icons/menu.svg"
                            sourceSize: Qt.size(14, 14)
                            fillMode: Image.PreserveAspectFit
                        }
                        onClicked: tabMenu.popup()
                    }

                    Menu {
                        id: tabMenu
                            MenuItem {
                                text: qsTr("Copy URL")
                                onTriggered: {
                                    const item = page.surfaceItems[web.activeTabId]
                                    if (item)
                                        workbench.copyText(String(item.tab.url))
                                }
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: qsTr("Zoom in")
                                onTriggered: stepZoom(0.1)
                            }
                            MenuItem {
                                text: qsTr("Zoom out")
                                onTriggered: stepZoom(-0.1)
                            }
                            MenuItem {
                                text: qsTr("Reset zoom")
                                onTriggered: web.setTabZoom(web.activeTabId, 1.0)
                            }
                            MenuSeparator {}
                            MenuItem {
                                visible: web.devToolsEnabled
                                text: qsTr("Developer tools")
                                onTriggered: {
                                    const item = page.surfaceItems[web.activeTabId]
                                    if (item)
                                        item.openDevTools()
                                }
                            }
                            MenuItem {
                                text: qsTr("Close tab")
                                onTriggered: web.closeTab(web.activeTabId)
                            }
                        }
                }
            }
        }

        // --- Body: one surface per tab ------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Running agents with one-click open (02 §6.1 empty state).
            Flickable {
                id: emptyState
                anchors.fill: parent
                // tabCount is NOTifiable; rowCount() has no notify signal,
                // so a binding to it never re-evaluated after the first tab.
                visible: web.tabCount === 0
                contentWidth: width
                contentHeight: runningColumn.implicitHeight + 2 * theme.spacingXl

                ColumnLayout {
                    id: runningColumn
                    width: emptyState.width - 2 * theme.spacingXl
                    x: theme.spacingXl
                    y: theme.spacingXl
                    spacing: theme.spacingM

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("No Web views open")
                        color: theme.textPrimary
                        font.pixelSize: theme.fontSizeSubtitle
                        font.bold: true
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        text: web.engineAvailable
                              ? qsTr("Open a view from a running agent's card, or from the list below.")
                              : qsTr("This build opens agent WebUIs in the system browser. Start an agent below to open it.")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeBody
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Repeater {
                        id: runningList
                        model: agents.model
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            visible: model.running
                            height: visible ? 48 : 0
                            onVisibleChanged: Qt.callLater(page.recountRunning)
                            radius: theme.radiusControl
                            color: theme.surfaceBg
                            border.color: theme.borderSubtle
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: theme.spacingM
                                anchors.rightMargin: theme.spacingS
                                spacing: theme.spacingM

                                AgentAvatar {
                                    iconSource: model.icon
                                    agentColor: model.color
                                    running: model.running
                                }
                                ColumnLayout {
                                    spacing: 0
                                    Layout.fillWidth: true
                                    Label {
                                        text: model.name
                                        color: theme.textPrimary
                                        font.pixelSize: theme.fontSizeBody
                                        font.bold: true
                                    }
                                    Label {
                                        text: model.webUrl
                                        color: theme.textMuted
                                        font.pixelSize: theme.fontSizeSmall
                                        font.family: theme.monoFamily
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                }
                                AButton {
                                    variant: "primary"
                                    text: qsTr("Open")
                                    onClicked: workbench.openWeb(model.agentId)
                                }
                            }
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        visible: page.runningCount === 0
                        text: qsTr("No agent is running - start one from the launcher page.")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeSmall
                    }
                    AButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Go to launcher")
                        onClicked: workbench.showPage("agents")
                    }
                }
            }

            // Surfaces: every tab keeps its place; released views are
            // destroyed (Loader inactive), frozen views stay in memory.
            Repeater {
                model: web.model
                delegate: Item {
                    id: tabHost

                    required property string tabId
                    required property string state
                    required property string surfaceKind
                    required property var tabObject

                    anchors.fill: parent
                    visible: web.activeTabId === tabId
                             && emptyState.visible === false

                    // Released: grey placeholder until restored (02 §6.3).
                    Rectangle {
                        anchors.fill: parent
                        visible: tabHost.state === "released"
                        color: theme.windowBg

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: theme.spacingM
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("View released to free memory")
                                color: theme.textMuted
                                font.pixelSize: theme.fontSizeBody
                            }
                            AButton {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("Restore view")
                                onClicked: web.reopen(tabHost.tabId)
                            }
                        }
                    }

                    Loader {
                        id: surfaceLoader
                        anchors.fill: parent
                        active: tabHost.state !== "released"
                        visible: tabHost.state !== "released"
                        source: active ? web.surfaceUrl(tabHost.surfaceKind)
                                       : ""

                        onLoaded: {
                            item.tab = tabHost.tabObject
                            item.fullScreenToggled.connect(
                                function(active) {
                                    page.chromeHidden = active
                                })
                            // Entries are overwritten on reload; stale ones
                            // for closed tabs are never consulted (the menu
                            // only reads the active tab's fresh item).
                            page.surfaceItems[tabHost.tabId] = item
                        }
                    }
                }
            }
        }
    }

    // --- Shortcuts (02 §6.6, ApplicationShortcut so Chromium never eats
    // them) ---------------------------------------------------------------
    function stepZoom(delta) {
        const id = web.activeTabId
        if (id.length === 0)
            return
        const tab = web.tabObject(id)
        if (!tab)
            return
        web.setTabZoom(id, Math.min(2.0, Math.max(0.5, tab.zoom + delta)))
    }

    Shortcut {
        sequences: [StandardKey.Close]
        context: Qt.ApplicationShortcut
        enabled: web.activeTabId.length > 0
        onActivated: web.closeTab(web.activeTabId)
    }
    Shortcut {
        sequence: StandardKey.Refresh
        context: Qt.ApplicationShortcut
        enabled: web.activeTabId.length > 0
        onActivated: web.reloadTab(web.activeTabId)
    }
    Shortcut {
        sequences: [StandardKey.NextChild]
        context: Qt.ApplicationShortcut
        onActivated: web.stepActiveTab(1)
    }
    Shortcut {
        sequences: [StandardKey.PreviousChild]
        context: Qt.ApplicationShortcut
        onActivated: web.stepActiveTab(-1)
    }
    Shortcut {
        sequences: ["Ctrl+=", "Ctrl++"]
        context: Qt.ApplicationShortcut
        enabled: web.activeTabId.length > 0
        onActivated: stepZoom(0.1)
    }
    Shortcut {
        sequence: "Ctrl+-"
        context: Qt.ApplicationShortcut
        enabled: web.activeTabId.length > 0
        onActivated: stepZoom(-0.1)
    }
    Shortcut {
        sequence: "Ctrl+0"
        context: Qt.ApplicationShortcut
        enabled: web.activeTabId.length > 0
        onActivated: web.setTabZoom(web.activeTabId, 1.0)
    }
    // F12 opens devtools — Debug builds only (02 §6.6).
    Shortcut {
        sequence: "F12"
        context: Qt.ApplicationShortcut
        enabled: web.devToolsEnabled && web.activeTabId.length > 0
        onActivated: {
            const item = page.surfaceItems[web.activeTabId]
            if (item)
                item.openDevTools()
        }
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: page.chromeHidden
        onActivated: page.chromeHidden = false
    }
}
