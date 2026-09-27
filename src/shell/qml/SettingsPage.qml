import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The settings page: grouped sections in a
// scrollable column. It hosts the application-level settings; the launcher
// list stays here for continuity with 0.3.0.
Page {
    id: page

    background: Rectangle { color: theme.workspaceBg }

    function openEditor(agentId) {
        editDialog.openFor(agentId)
    }

    // Root-kind labels go through literal qsTr() calls (qsTr of a dynamic
    // string is invisible to lupdate).
    function kindLabel(kind) {
        switch (kind) {
        case "agents": return qsTr("Agents")
        case "claude": return qsTr("Claude")
        case "codex": return qsTr("Codex")
        case "plugin": return qsTr("Plugin")
        case "project": return qsTr("Project")
        case "custom": return qsTr("Custom")
        default: return kind
        }
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
                    // ComboBox.indexOfValue understands valueRole (Qt 6).
                    currentIndex: indexOfValue(theme.themeId)
                    onActivated: theme.applyTheme(currentValue)
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
                        // Rows are delegates; deleting an agent while its
                        // status-dot tooltip is showing must not freeze the
                        // shared tooltip on screen.
                        Component.onDestruction: ToolTip.hide()

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

                            // Running-state dot (tooltip built in —
                            // never color-only).
                            AStatusDot {
                                diameter: 10
                                on: model.running
                                tooltip: model.running ? qsTr("Running")
                                                       : qsTr("Stopped")
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

            // --- Skills ------------------------------------
            ASectionHeader {
                text: qsTr("Skills")
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                extra: [
                    AButton {
                        text: qsTr("Rescan")
                        onClicked: skills.refresh()
                    }
                ]
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingS

                Repeater {
                    // Property (not roots()): re-evaluates when a root is
                    // added/removed/toggled via the NOTIFY signal.
                    model: skills.roots
                    delegate: Rectangle {
                        required property var modelData

                        Layout.fillWidth: true
                        height: 44
                        radius: theme.radiusControl
                        color: theme.surfaceBg
                        border.color: theme.borderSubtle
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: theme.spacingM
                            anchors.rightMargin: theme.spacingS
                            spacing: theme.spacingM

                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Label {
                                    text: modelData.label
                                    color: theme.textPrimary
                                    font.pixelSize: theme.fontSizeBody
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.path
                                    color: theme.textMuted
                                    font.pixelSize: theme.fontSizeCaption
                                    font.family: theme.monoFamily
                                    elide: Text.ElideMiddle
                                }
                            }
                            Label {
                                text: page.kindLabel(modelData.kind)
                                color: theme.textSecondary
                                font.pixelSize: theme.fontSizeCaption
                            }
                            Switch {
                                checked: modelData.enabled
                                onToggled: skills.setRootEnabled(
                                    modelData.id, checked)
                            }
                            AIconButton {
                                iconSource: "qrc:/icons/close.svg"
                                tooltip: qsTr("Remove this root")
                                onClicked: skills.removeRoot(modelData.id)
                            }
                        }
                    }
                }

                // Add a custom root.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingS

                    TextField {
                        id: newRootField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Add a skill root directory...")
                        color: theme.textPrimary
                        placeholderTextColor: theme.textMuted
                        font.family: theme.monoFamily
                        font.pixelSize: theme.fontSizeSmall
                        background: Rectangle {
                            radius: theme.radiusControl
                            color: theme.surfaceAltBg
                            border.color: newRootField.activeFocus
                                          ? theme.focusRing : theme.borderSubtle
                            border.width: newRootField.activeFocus ? 2 : 1
                        }
                        onAccepted: {
                            if (skills.addRoot(text.trim()))
                                text = ""
                        }
                    }
                    AButton {
                        text: qsTr("Add")
                        enabled: newRootField.text.trim().length > 0
                        onClicked: {
                            if (skills.addRoot(newRootField.text.trim()))
                                newRootField.text = ""
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: skills.statsText
                    color: skills.partialFailure ? theme.warning
                                                 : theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Plugin caches keep several versions of the same plugin; only the highest is listed.")
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                    wrapMode: Text.WordWrap
                }
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
                    model: web.engineAvailable
                           ? [{ text: qsTr("Embedded (in-app)"), value: "embedded" },
                              { text: qsTr("External (system browser)"), value: "external" }]
                           : [{ text: qsTr("External (system browser)"), value: "external" }]
                    Component.onCompleted: currentIndex =
                        indexOfValue(shell.webSurface)
                    onActivated: shell.setWebSurface(currentValue)
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

            // --- Plugins (experimental) --------------------------
            ASectionHeader {
                text: qsTr("Plugins")
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
            }

            // Trust notice — required by
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                text: workbench.pluginTrustNotice()
                color: theme.warning
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Enable plugins (experimental)")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                Switch {
                    checked: workbench.pluginsEnabled()
                    onToggled: workbench.setPluginsEnabled(checked)
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                visible: workbench.pluginList().length === 0
                text: qsTr("No plugins found. Drop one into the plugins folder (Settings -> data directory).")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingS

                Repeater {
                    model: workbench.pluginList()
                    delegate: Rectangle {
                        required property var modelData

                        Layout.fillWidth: true
                        height: 56
                        radius: theme.radiusControl
                        color: theme.surfaceBg
                        border.color: theme.borderSubtle
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: theme.spacingM
                            anchors.rightMargin: theme.spacingS
                            spacing: theme.spacingM

                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Label {
                                    text: (modelData.name || modelData.id)
                                          + "  v" + (modelData.version || "?")
                                    color: theme.textPrimary
                                    font.pixelSize: theme.fontSizeBody
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.description || ""
                                    color: theme.textMuted
                                    font.pixelSize: theme.fontSizeCaption
                                    elide: Text.ElideRight
                                }
                            }
                            // Effective on the next start.
                            Switch {
                                checked: modelData.enabled
                                onToggled: workbench.setPluginEnabled(
                                    modelData.id, checked)
                            }
                        }
                    }
                }
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
