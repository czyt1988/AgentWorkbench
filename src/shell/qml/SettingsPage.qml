import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The settings page (specs/02 §5, specs/03 S4-T7): grouped sections in a
// scrollable column. It hosts the application-level settings; the launcher
// list stays here for continuity with 0.3.0.
Page {
    id: page

    background: Rectangle { color: theme.workspaceBg }

    function openEditor(agentId) {
        editDialog.openFor(agentId)
    }

    AgentEditDialog {
        id: editDialog
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
        padding: theme.spacingL
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

                AButton {
                    Layout.fillWidth: true
                    variant: "danger"
                    text: qsTr("Delete")
                    onClicked: {
                        if (!agents.removeAgent(deleteConfirmPopup.pendingId))
                            errorPopup.open()
                        deleteConfirmPopup.close()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
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
        padding: theme.spacingL
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
            AButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                onClicked: errorPopup.close()
            }
        }
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: theme.spacingM

            PageHeader {
                title: qsTr("Settings")
                subtitle: qsTr("Appearance, launchers and application options")
            }

            // --- Appearance ------------------------------------------------
            ASectionHeader {
                text: qsTr("Appearance")
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Theme")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                ComboBox {
                    id: themeCombo
                    textRole: "name"
                    valueRole: "id"
                    model: theme.availableThemes
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

            // --- Launchers -------------------------------------------------
            ASectionHeader {
                text: qsTr("Launchers")
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                extra: [
                    AButton {
                        text: qsTr("Add Launcher")
                        onClicked: page.openEditor("")
                    }
                ]
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
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

                            // Running-state dot with tooltip (never
                            // color-only, specs/02 §14).
                            Rectangle {
                                width: 10
                                height: 10
                                radius: theme.radiusPill
                                color: model.running ? theme.success : theme.neutralOff
                                ToolTip.visible: dotArea.containsMouse
                                ToolTip.delay: 300
                                ToolTip.text: model.running ? qsTr("Running")
                                                            : qsTr("Stopped")

                                MouseArea {
                                    id: dotArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }

                            AButton {
                                text: qsTr("Edit")
                                onClicked: page.openEditor(model.agentId)
                            }
                            AButton {
                                variant: "danger"
                                text: qsTr("Delete")
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

            // --- Environment ------------------------------------------------
            ASectionHeader {
                text: qsTr("Environment")
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: environment.pythonInstalled
                          ? qsTr("Python %1").arg(environment.pythonVersion)
                          : qsTr("Python not found")
                    color: environment.pythonInstalled ? theme.textPrimary
                                                       : theme.danger
                    font.pixelSize: theme.fontSizeBody
                }
                Label {
                    Layout.fillWidth: true
                    text: environment.nodeInstalled
                          ? qsTr("Node.js %1").arg(environment.nodeVersion)
                          : qsTr("Node.js not found")
                    color: environment.nodeInstalled ? theme.textPrimary
                                                     : theme.danger
                    font.pixelSize: theme.fontSizeBody
                }
                AButton {
                    text: qsTr("Re-detect")
                    onClicked: environment.refresh()
                }
            }

            // --- Skills (placeholder until S6) -------------------------------
            ASectionHeader {
                text: qsTr("Skills")
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                text: qsTr("Skill root directories and scanning options arrive with the Skills page.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            // --- Web ---------------------------------------------------------
            ASectionHeader {
                text: qsTr("Web")
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Surface")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                ComboBox {
                    id: surfaceCombo
                    textRole: "text"
                    valueRole: "value"
                    // Without WebEngine only the external surface exists
                    // (02 §6.7).
                    model: web.engineAvailable
                           ? [{ text: qsTr("Embedded (in-app)"), value: "embedded" },
                              { text: qsTr("External (system browser)"), value: "external" }]
                           : [{ text: qsTr("External (system browser)"), value: "external" }]
                    Component.onCompleted: currentIndex =
                        indexOfValue(shell.webSurface)
                    onActivated: shell.setWebSurface(currentValue)

                    function indexOfValue(value) {
                        for (let i = 0; i < count; ++i) {
                            if (get(i).value === value)
                                return i
                        }
                        return 0
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                TextField {
                    id: flagsField
                    Layout.fillWidth: true
                    text: shell.webChromiumFlags
                    placeholderText: qsTr("Chromium flags, e.g. --disable-gpu (applies after restart)")
                    color: theme.textPrimary
                    placeholderTextColor: theme.textMuted
                    font.family: theme.monoFamily
                    font.pixelSize: theme.fontSizeSmall
                    background: Rectangle {
                        radius: theme.radiusControl
                        color: theme.surfaceAltBg
                        border.color: flagsField.activeFocus ? theme.focusRing
                                                             : theme.borderSubtle
                        border.width: flagsField.activeFocus ? 2 : 1
                    }
                    onEditingFinished: shell.setWebChromiumFlags(text.trim())
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                text: qsTr("If embedded views fail to start (GPU driver issues), add --disable-gpu here. The in-app 'Open in browser' action always works as a fallback.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            // --- Advanced ----------------------------------------------------
            ASectionHeader {
                text: qsTr("Advanced")
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: agents.configFilePath()
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeSmall
                    font.family: theme.monoFamily
                    elide: Text.ElideMiddle
                }
                AButton {
                    text: qsTr("Open data folder")
                    onClicked: workbench.openFolder(
                        agents.configFilePath().replace(
                            /[\\\\\\/]agents\\.json$/, ""))
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                Layout.bottomMargin: theme.spacingL
                spacing: theme.spacingM

                Item { Layout.fillWidth: true }
                AButton {
                    text: qsTr("Restore default launchers")
                    onClicked: {
                        if (!agents.restoreDefaults())
                            errorPopup.open()
                    }
                }
            }
        }
    }
}
