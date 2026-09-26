import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// One skill card (specs/02 §7.3): 340x160, click copies the directory path,
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

    // --- Hover flyout (400 ms, 02 §7.4) ---------------------------------
    Timer {
        id: hoverTimer
        interval: 400
        onTriggered: flyout.open()
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

            // Description, up to three lines (02 §7.3).
            Label {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(implicitHeight,
                                                  3 * lineHeight * 1.3)
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

            // Path + copy button (02 §7.3).
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

    // Keyboard: Enter copies the path, Ctrl+Enter opens the folder (02 §7.3).
    focus: true
    Keys.onReturnPressed: copyPath()
    Keys.onEnterPressed: copyPath()
    Keys.onPressed: function(event) {
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
        // Sit below-right of the card, flipping when off-screen (02 §7.4).
        x: card.width + theme.spacingM
        y: 0
    }
}
