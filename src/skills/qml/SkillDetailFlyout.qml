import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Hover detail flyout (specs/02 §7.4): full description + metadata table.
// Opened by the card after 400 ms; closes 300 ms after the pointer leaves
// both the card and this popup, on any key press, or on page scroll.
Popup {
    id: flyout

    property string skillFilePath: ""
    property bool closePending: false

    // The definition behind the path (facade lookup, 01 §4.4).
    readonly property var info: skills.skill(skillFilePath)

    modal: false
    focus: false
    padding: theme.spacingM
    width: 420
    height: Math.min(body.implicitHeight + 2 * padding, 320)
    closePolicy: Popup.NoAutoClose

    background: Rectangle {
        color: theme.overlayBg
        border.color: theme.borderSubtle
        border.width: 1
        radius: theme.radiusOverlay
    }

    // The pointer entered the flyout: cancel the pending close.
    HoverHandler {
        onHoveredChanged: {
            if (hovered)
                flyout.closePending = false
        }
    }

    function tryCloseLater() {
        closePending = true
        closeTimer.restart()
    }

    Timer {
        id: closeTimer
        interval: 300
        onTriggered: {
            if (flyout.closePending && !flyoutHostHover.hovered)
                flyout.close()
        }
    }

    HoverHandler {
        id: flyoutHostHover
    }

    // Any key press closes it (02 §7.4).
    Keys.onPressed: function(event) {
        flyout.close()
        event.accepted = true
    }

    contentItem: Flickable {
        id: body
        contentWidth: width
        contentHeight: details.implicitHeight
        clip: true

        ColumnLayout {
            id: details
            width: body.width
            spacing: theme.spacingS

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS
                Label {
                    Layout.fillWidth: true
                    text: flyout.info.name || ""
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeSubtitle
                    font.bold: true
                    elide: Text.ElideRight
                }
                APill {
                    text: flyout.info.kind || ""
                }
            }

            Label {
                Layout.fillWidth: true
                visible: (flyout.info.rootLabel || "").length > 0
                text: flyout.info.rootLabel || ""
                color: theme.textSecondary
                font.pixelSize: theme.fontSizeCaption
            }

            Label {
                Layout.fillWidth: true
                visible: (flyout.info.description || "").length > 0
                text: flyout.info.description || ""
                color: theme.textSecondary
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.separator
            }

            // Metadata table (02 §7.4).
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: theme.spacingM
                rowSpacing: theme.spacingXs

                Label {
                    text: qsTr("SKILL.md")
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                }
                Label {
                    Layout.fillWidth: true
                    text: flyout.info.skillFilePath || ""
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeCaption
                    font.family: theme.monoFamily
                    elide: Text.ElideMiddle
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 300
                        ToolTip.text: flyout.info.skillFilePath || ""
                    }
                }

                Label {
                    text: qsTr("Modified")
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                }
                Label {
                    text: flyout.info.lastModified !== undefined
                          ? Qt.formatDateTime(flyout.info.lastModified,
                                              "yyyy-MM-dd hh:mm")
                          : ""
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeCaption
                }

                Label {
                    text: qsTr("Size")
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                }
                Label {
                    text: {
                        const bytes = flyout.info.sizeBytes || 0
                        return (bytes / 1024).toFixed(1) + " KB"
                    }
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeCaption
                }
            }

            // Extra frontmatter scalars (02 §7.4).
            Repeater {
                model: {
                    const extras = flyout.info.extras || {}
                    const rows = []
                    for (const key in extras)
                        rows.push({ key: key, value: String(extras[key]) })
                    return rows
                }
                delegate: GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: theme.spacingM

                    Label {
                        text: modelData.key
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.value
                        color: theme.textSecondary
                        font.pixelSize: theme.fontSizeCaption
                        elide: Text.ElideRight
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 300
                            ToolTip.text: modelData.value
                        }
                    }
                }
            }

            Label {
                text: qsTr("Click the card to copy the path")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                font.italic: true
            }
        }
    }
}
