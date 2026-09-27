import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgentWorkbench
import AgentWorkbench.App

// One skill card : 340x160, click copies the directory path,
// right-click offers the copy/open actions, hover opens the detail flyout.
Item {
    id: card

    property string skillFilePath: ""
    property string skillName: ""
    property string description: ""
    property string dirPath: ""
    property string kind: ""
    property string rootLabel: ""
    property string pluginVersion: ""

    readonly property string pathText: dirPath

    width: theme.cardMinWidth + 80 // spec width 340
    height: 160

    // Cards are model delegates: search/filter/rescan destroys them while
    // hovered, and the ToolTip attached property shares ONE visual tooltip
    // per window. When the hovered owner dies, its `ToolTip.visible`
    // binding dies with it and nothing ever hides the shared tooltip again
    // — it froze on screen. Hide it on destruction; every tooltip also
    // sets a timeout so even a missed case self-heals.
    Component.onDestruction: ToolTip.hide()

    // --- Hover flyout (400 ms) ---------------------------------
    Timer {
        id: hoverTimer
        interval: 400
        onTriggered: card.openFlyout()
    }

    // Open with edge-aware placement: sit right of the card, flip left/up
    // when the window edge would clip it.
    function openFlyout() {
        const pos = card.mapToItem(null, 0, 0)
        const win = card.Window.window
        if (!win)
            return
        const gap = theme.spacingM
        // height is content-driven now, but keep the 320 fallback in case
        // the popup has not been laid out yet (flip test must not use 0).
        const fh = flyout.height > 1 ? flyout.height : 320
        flyout.x = (pos.x + card.width + gap + flyout.width > win.width)
                   ? -(flyout.width + gap) : card.width + gap
        flyout.y = (pos.y + fh > win.height)
                   ? -(fh - card.height) : 0
        flyout.open()
    }

    Rectangle {
        id: background
        anchors.fill: parent
        radius: theme.radiusCard
        color: hoverHandler.hovered ? theme.surfaceHoverBg : theme.surfaceBg
        border.color: hoverHandler.hovered ? theme.accent : theme.borderSubtle
        border.width: 1
        Behavior on color { ColorAnimation { duration: theme.durationFast } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: theme.spacingM
            spacing: theme.spacingXs

            // Title row + source badge.
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Label {
                    Layout.fillWidth: true
                    text: card.skillName
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeSubtitle
                    font.bold: true
                    elide: Text.ElideRight
                }
                APill {
                    text: card.kind
                    tooltip: card.kind === "plugin"
                             ? qsTr("Plugin: %1").arg(card.pluginVersion)
                             : ""
                }
            }

            // Description, up to three lines. maximumLineCount
            // already elides after the third line, so implicitHeight is the
            // right height — no extra clamp (lineHeight is a multiplier, not
            // pixels; using it as a pixel cap collapsed this to ~4 px).
            Label {
                Layout.fillWidth: true
                text: card.description.length > 0 ? card.description
                                                  : qsTr("No description.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                clip: true
            }

            Item { Layout.fillHeight: true }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.separator
            }

            // Path + copy button.
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Label {
                    Layout.fillWidth: true
                    text: card.pathText
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                    font.family: theme.monoFamily
                    elide: Text.ElideMiddle
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 300
                        ToolTip.timeout: 10000
                        ToolTip.text: card.pathText
                    }
                }
                AIconButton {
                    iconSource: "qrc:/icons/copy.svg"
                    tooltip: qsTr("Copy path")
                    onClicked: card.copyPath()
                }
            }
        }
    }

    HoverHandler {
        id: hoverHandler
        onHoveredChanged: {
            if (hovered)
                hoverTimer.start()
            else {
                hoverTimer.stop()
                flyout.tryCloseLater()
            }
        }
    }

    // Scrolling the grid closes the flyout immediately — the
    // wheel gesture over THIS card; scrolling elsewhere is covered by the
    // hover-out path above.
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        // blocking:false — with the default (true) the handler swallowed the
        // wheel over the card and the ScrollView behind never scrolled; the
        // flyout still closes via onWheel while the event keeps propagating.
        blocking: false
        onWheel: function(event) {
            if (flyout.opened)
                flyout.close()
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: card.forceActiveFocus()
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                contextMenu.popup()
            else
                card.copyPath()
        }
    }

    // Keyboard: Tab reaches the card (— focus also shows the
    // flyout), Enter copies the path, Ctrl+Enter opens the folder.
    // activeFocusOnTab, NOT focus: true — every delegate setting focus
    // would make the last-created card steal the page's initial focus.
    activeFocusOnTab: true
    onActiveFocusChanged: {
        if (activeFocus)
            card.openFlyout()
        else
            flyout.tryCloseLater()
    }
    Keys.onReturnPressed: copyPath()
    Keys.onEnterPressed: copyPath()
    Keys.onPressed: function(event) {
        // Any key closes the flyout first ; the NEXT press acts.
        if (flyout.opened) {
            flyout.close()
            event.accepted = true
            return
        }
        if ((event.modifiers & Qt.ControlModifier)
            && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            skills.openFolder(card.skillFilePath)
            event.accepted = true
        }
    }

    function copyPath() {
        const result = skills.copyPath(card.skillFilePath)
        if (result.ok)
            workbench.notify("success", qsTr("Path copied"),
                             card.pathText)
        else
            workbench.notify("error", qsTr("Copy failed"), result.error)
    }

    Menu {
        id: contextMenu
        MenuItem {
            text: qsTr("Copy path")
            onTriggered: card.copyPath()
        }
        MenuItem {
            text: qsTr("Copy SKILL.md path")
            onTriggered: {
                const result = skills.copySkillFile(card.skillFilePath)
                if (result.ok)
                    workbench.notify("success", qsTr("Path copied"),
                                     card.skillFilePath)
                else
                    workbench.notify("error", qsTr("Copy failed"),
                                     result.error)
            }
        }
        MenuItem {
            text: qsTr("Copy name")
            onTriggered: {
                const result = skills.copyName(card.skillFilePath)
                if (result.ok)
                    workbench.notify("success", qsTr("Copied"),
                                     card.skillName)
                else
                    workbench.notify("error", qsTr("Copy failed"),
                                     result.error)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Open containing folder")
            onTriggered: {
                const result = skills.openFolder(card.skillFilePath)
                if (!result.ok)
                    workbench.notify("error", qsTr("Cannot open folder"),
                                     result.error)
            }
        }
        MenuItem {
            text: qsTr("Reveal SKILL.md")
            onTriggered: {
                const result = skills.revealSkillFile(card.skillFilePath)
                if (!result.ok)
                    workbench.notify("error", qsTr("Cannot open folder"),
                                     result.error)
            }
        }
    }

    SkillDetailFlyout {
        id: flyout
        skillFilePath: card.skillFilePath
        // Sit below-right of the card, flipping when off-screen.
        x: card.width + theme.spacingM
        y: 0
    }
}
