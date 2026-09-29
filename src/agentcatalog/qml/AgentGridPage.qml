import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The launcher page : header with page actions, search +
// display filter, and the card grid. Card interactions are unchanged from
// 0.3.0 (AgentCard.qml).
Item {
    id: page

    property string filterText: ""
    // 0 = all, 1 = running, 2 = not installed
    property int displayFilter: 0
    property int shownCount: 0

    // shownCount is derived from the hidden counter delegates' `matches`
    // property, never from card visibility: Item.visible reads back the
    // *effective* visibility, so a card created inside the ScrollView
    // (hidden while shownCount === 0) can never read visible=true —
    // counting cards deadlocked shownCount at 0 and pinned the
    // "No matching launchers" empty state over a fully configured model.
    function recountShown() {
        let n = 0
        for (let i = 0; i < counterBox.children.length; ++i) {
            if (counterBox.children[i].matches === true)
                ++n
        }
        page.shownCount = n
    }

    function matchesFilter(m) {
        if (displayFilter === 1 && !m.running)
            return false
        if (displayFilter === 2 && m.installed)
            return false
        if (filterText.length === 0)
            return true
        const needle = filterText.toLowerCase()
        return m.name.toLowerCase().includes(needle)
               || m.command.toLowerCase().includes(needle)
               || m.webUrl.toLowerCase().includes(needle)
    }

    // 页面底衬光斑：workspaceBg 是纯色平底，卡片改半透明玻璃后若无底衬，
    // 「透」在视觉上无从体现。两团极低透明度的大径向光斑（accent 与 agent
    // 调色板暖色）静态铺在页面最底层，只在主题变化或尺寸变化时重绘；
    // 透明度压到不影响任何文字对比度的程度。
    Canvas {
        id: bgGlow
        anchors.fill: parent

        // 把主题令牌读进本地属性作为重绘触发器：Canvas 不会自动跟踪
        // theme，直接依赖才能在切换主题时重画
        property color tintA: theme.accent
        property color tintB: theme.agentPalette.length > 1 ? theme.agentPalette[1]
                                                            : theme.accent
        onTintAChanged: requestPaint()
        onTintBChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            var reach = Math.max(width, height) * 0.75;

            var g1 = ctx.createRadialGradient(width * 0.88, height * 0.06, 0,
                                              width * 0.88, height * 0.06, reach);
            g1.addColorStop(0, theme.alpha(tintA, 0.07));
            g1.addColorStop(1, theme.alpha(tintA, 0));
            ctx.fillStyle = g1;
            ctx.fillRect(0, 0, width, height);

            var g2 = ctx.createRadialGradient(width * 0.06, height * 0.95, 0,
                                              width * 0.06, height * 0.95, reach * 0.8);
            g2.addColorStop(0, theme.alpha(tintB, 0.05));
            g2.addColorStop(1, theme.alpha(tintB, 0));
            ctx.fillStyle = g2;
            ctx.fillRect(0, 0, width, height);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            title: qsTr("Agent Launcher")
            subtitle: qsTr("Launch AI coding agents and open their web UI")

            AButton {
                variant: "primary"
                text: qsTr("Add Launcher")
                onClicked: editDialog.openFor("")
            }
            AButton {
                text: qsTr("Restore Defaults")
                onClicked: {
                    if (!agents.restoreDefaults())
                        errorPopup.open()
                }
            }
        }

        // --- Search + display filter -------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingM
            spacing: theme.spacingM

            ASearchField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search launchers...")
                onTextChanged: page.filterText = text
            }

            Row {
                spacing: theme.spacingXs

                AButton {
                    text: qsTr("All")
                    variant: page.displayFilter === 0 ? "primary" : "ghost"
                    onClicked: page.displayFilter = 0
                }
                AButton {
                    text: qsTr("Running")
                    variant: page.displayFilter === 1 ? "primary" : "ghost"
                    onClicked: page.displayFilter = 1
                }
                AButton {
                    text: qsTr("Not installed")
                    variant: page.displayFilter === 2 ? "primary" : "ghost"
                    onClicked: page.displayFilter = 2
                }
            }
        }

        // --- Empty states -------------------------------------------------
        // Hidden counters: total configured launchers, and per-row match
        // state. `matches` mirrors exactly what the card's visible binding
        // computes, but lives on an always-hidden delegate so recountShown()
        // can bootstrap without depending on the ScrollView's visibility.
        Item {
            id: counterBox
            visible: false
            Repeater {
                id: totalRepeater
                model: agents.model
                delegate: Item {
                    width: 0
                    height: 0
                    readonly property bool matches: page.matchesFilter(model)
                    onMatchesChanged: Qt.callLater(page.recountShown)
                }
            }
        }

        AEmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: totalRepeater.count === 0
            iconSource: "qrc:/icons/terminal.svg"
            title: qsTr("No launchers configured yet")
            description: qsTr("Add your first AI coding agent, or restore the built-in defaults.")
            actionText: qsTr("Add Launcher")
            onActionClicked: editDialog.openFor("")
        }

        AEmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: totalRepeater.count > 0 && page.shownCount === 0
            iconSource: "qrc:/icons/search.svg"
            title: qsTr("No matching launchers")
            description: qsTr("No launcher matches the current search or filter.")
            actionText: qsTr("Clear filters")
            onActionClicked: {
                searchField.text = ""
                page.displayFilter = 0
            }
        }

        // --- Card grid ----------------------------------------------------
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Match the search row's side margins so the first card never
            // sits flush against the sidebar.
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            // Only when there is something to show: with zero cards both
            // this (fillHeight) and the empty state (fillHeight) competed
            // for the same column height.
            visible: page.shownCount > 0
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: AScrollBar {}

            Flow {
                id: flow
                width: scrollView.availableWidth
                spacing: theme.spacingL
                onChildrenChanged: Qt.callLater(page.recountShown)

                Repeater {
                    model: agents.model
                    delegate: AgentCard {
                        width: theme.cardMinWidth
                        visible: page.matchesFilter(model)
                        onConfigureRequested: function(id) {
                            editDialog.openFor(id)
                        }
                    }
                }
            }
        }
    }

    AgentEditDialog {
        id: editDialog
    }

    // Central error display for launch/stop failures. The matching card
    // also flashes red for at-place feedback.
    AAlertDialog {
        id: errorPopup
        width: 500
        titleText: qsTr("Launch failed")
        property string message: ""
        detail: errorPopup.message
        dismissText: qsTr("OK")
    }

    Connections {
        target: agents
        function onLaunchFailed(id, message) {
            errorPopup.message = message
            errorPopup.open()
        }
        function onInstallFinished(id, success, message) {
            if (!success) {
                errorPopup.message = message
                errorPopup.open()
            }
        }
    }

    Component.onCompleted: page.recountShown()
}
