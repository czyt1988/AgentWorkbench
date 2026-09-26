import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App

// Settings entry: launcher management. Structured as sections inside a
// scrollable column so more settings can be added below "Launchers" later.
Page {
    id: page
    background: Rectangle { color: theme.workspaceBg }

    function openEditor(agentId) {
        page.StackView.view.push(agentEditComp, { "agentId": agentId })
    }

    Component {
        id: agentEditComp
        AgentEditPage {}
    }

    // --- Delete confirmation -----------------------------------------------
    Popup {
        id: deleteConfirmPopup
        property string pendingId: ""
        property string pendingName: ""
        property bool pendingRunning: false
        property bool pendingBuiltin: false
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 420
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.danger
            border.width: 1
            radius: theme.radiusOverlay
        }

        ColumnLayout {
            width: deleteConfirmPopup.availableWidth
            spacing: theme.spacingM

            Label {
                text: qsTr("Delete Launcher")
                color: theme.danger
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Remove \"%1\" from the launcher list?")
                      .arg(deleteConfirmPopup.pendingName)
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: deleteConfirmPopup.pendingRunning
                text: qsTr("The agent is currently running. Deleting it does not stop the process; stop it via its own command if needed.")
                color: theme.warning
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: deleteConfirmPopup.pendingBuiltin
                text: qsTr("This is a built-in launcher. You can bring it back later with \"Restore default launchers\".")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Delete")
                    background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.pressed(theme.danger) : (parent.hovered ? theme.hover(theme.danger) : theme.danger) }
                    contentItem: Label { text: parent.text; color: theme.windowBg; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        if (!agents.removeAgent(deleteConfirmPopup.pendingId))
                            errorPopup.open()
                        deleteConfirmPopup.close()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.surfaceAltBg : (parent.hovered ? theme.surfaceHoverBg : theme.surfaceBg); border.color: theme.borderSubtle }
                    contentItem: Label { text: parent.text; color: theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: deleteConfirmPopup.close()
                }
            }
        }
    }

    // Shown when removeAgent/restoreDefaults could not write agents.json.
    Popup {
        id: errorPopup
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 420
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.danger
            border.width: 1
            radius: theme.radiusOverlay
        }

        ColumnLayout {
            width: errorPopup.availableWidth
            spacing: theme.spacingM

            Label {
                text: qsTr("Save failed")
                color: theme.danger
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Could not write the configuration file:")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: agents.configFilePath()
                color: theme.accent
                font.pixelSize: theme.fontSizeBody
                font.family: theme.monoFamily
                wrapMode: Text.WrapAnywhere
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.surfaceAltBg : theme.surfaceBg; border.color: theme.borderSubtle }
                contentItem: Label { text: parent.text; color: theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: errorPopup.close()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.spacingXl
        spacing: theme.spacingM

        RowLayout {
            spacing: theme.spacingM

            Button {
                text: qsTr("\u2190 Back")
                background: Rectangle { color: "transparent" }
                contentItem: Label { text: parent.text; color: theme.accent; font.pixelSize: theme.fontSizeSubtitle }
                onClicked: page.StackView.view.pop()
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            text: qsTr("Settings")
            color: theme.textPrimary
            font.pixelSize: theme.fontSizePageTitle
            font.bold: true
        }

        // --- Appearance section ------------------------------------------
        // Theme switch entry (specs/03 S3-T4): applies at runtime and is
        // persisted to settings.json; theme files hot-reload through
        // ThemeRegistry.
        RowLayout {
            Layout.fillWidth: true
            spacing: theme.spacingM

            Label {
                text: qsTr("Appearance")
                color: theme.textSecondary
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: themeCombo
                textRole: "name"
                valueRole: "id"
                model: theme.availableThemes
                // Re-evaluates when the active theme changes (themeId
                // notifies); the user's pick re-activates it immediately.
                currentIndex: themeCombo.themeIdIndexOf(theme.themeId)
                onActivated: theme.applyTheme(currentValue)

                function themeIdIndexOf(id) {
                    const themes = theme.availableThemes
                    for (let i = 0; i < themes.length; ++i) {
                        if (themes[i].id === id)
                            return i
                    }
                    return -1
                }
            }
        }

        // --- Launchers section -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: theme.spacingM

            Label {
                text: qsTr("Launchers")
                color: theme.textSecondary
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Add Launcher")
                background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.pressed(theme.accent) : (parent.hovered ? theme.hover(theme.accent) : theme.accent) }
                contentItem: Label { text: parent.text; color: theme.windowBg; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: page.openEditor("")
            }
        }

        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: scrollView.availableWidth
                spacing: theme.spacingS

                Repeater {
                    model: agents.model

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 60
                        radius: theme.radiusControl
                        color: theme.surfaceBg

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: theme.spacingM
                            anchors.rightMargin: theme.spacingS
                            spacing: theme.spacingM

                            Image {
                                source: model.icon
                                sourceSize: Qt.size(28, 28)
                                fillMode: Image.PreserveAspectFit
                            }

                            ColumnLayout {
                                spacing: theme.spacingXs
                                Layout.fillWidth: true

                                Label {
                                    text: model.name
                                    color: theme.textPrimary
                                    font.pixelSize: theme.fontSizeBody
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: model.command
                                    color: theme.textMuted
                                    font.pixelSize: theme.fontSizeSmall
                                    elide: Text.ElideMiddle
                                }
                            }

                            // Running-state dot with tooltip.
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: model.running ? theme.success : theme.neutralOff
                                ToolTip.visible: dotArea.containsMouse
                                ToolTip.delay: 300
                                ToolTip.text: model.running ? qsTr("Running") : qsTr("Stopped")

                                MouseArea {
                                    id: dotArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }

                            Button {
                                text: qsTr("Edit")
                                background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.surfaceAltBg : (parent.hovered ? theme.surfaceHoverBg : theme.surfaceBg); border.color: theme.borderSubtle }
                                contentItem: Label { text: parent.text; color: theme.accent; font.pixelSize: theme.fontSizeBody; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                onClicked: page.openEditor(model.agentId)
                            }
                            Button {
                                text: qsTr("Delete")
                                background: Rectangle { radius: theme.radiusControl; color: parent.down ? theme.surfaceAltBg : (parent.hovered ? theme.surfaceHoverBg : theme.surfaceBg); border.color: theme.borderSubtle }
                                contentItem: Label { text: parent.text; color: theme.danger; font.pixelSize: theme.fontSizeBody; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                onClicked: {
                                    deleteConfirmPopup.pendingId = model.agentId
                                    deleteConfirmPopup.pendingName = model.name
                                    deleteConfirmPopup.pendingRunning = model.running
                                    deleteConfirmPopup.pendingBuiltin =
                                        agents.isDefaultAgent(model.agentId)
                                    deleteConfirmPopup.open()
                                }
                            }
                        }
                    }
                }
            }
        }

        Button {
            text: qsTr("Restore default launchers")
            background: Rectangle { color: "transparent" }
            contentItem: Label { text: parent.text; color: theme.textMuted; font.pixelSize: theme.fontSizeBody }
            onClicked: {
                if (!agents.restoreDefaults())
                    errorPopup.open()
            }
        }
    }
}
