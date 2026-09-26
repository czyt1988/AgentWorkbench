import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The launcher page (specs/02 §8): header with page actions, search +
// display filter, and the card grid. Card interactions are unchanged from
// 0.3.0 (AgentCard.qml).
Item {
    id: page

    property string filterText: ""
    // 0 = all, 1 = running, 2 = not installed
    property int displayFilter: 0
    property int shownCount: 0

    function recountShown() {
        let n = 0
        for (const child of flow.children) {
            if (child.visible)
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
        // Total number of configured launchers (hidden counter).
        Item {
            visible: false
            Repeater {
                id: totalRepeater
                model: agents.model
                delegate: Item { width: 0; height: 0 }
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
            visible: page.shownCount > 0 || totalRepeater.count === 0
            clip: true
            contentWidth: availableWidth

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
                        onVisibleChanged: Qt.callLater(page.recountShown)
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
    // also flashes red for at-place feedback (specs/02 §8.2).
    Popup {
        id: errorPopup
        property string message: ""
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 500
        height: Math.min(errorColumn.implicitHeight + 2 * errorPopup.padding, 400)
        padding: theme.spacingL
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.danger
            border.width: 1
            radius: theme.radiusOverlay
        }

        ColumnLayout {
            id: errorColumn
            width: errorPopup.availableWidth
            spacing: theme.spacingM

            Label {
                text: qsTr("Launch failed")
                color: theme.danger
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Label {
                    Layout.fillWidth: true
                    text: errorPopup.message
                    color: theme.textPrimary
                    font.family: theme.monoFamily
                    font.pixelSize: theme.fontSizeSmall
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
            }
            AButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                onClicked: errorPopup.close()
            }
        }
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
