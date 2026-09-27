import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Full add/edit form for one launcher, as a dialog.
// Empty agentId = add mode.
// NOTE: do not name the agent map property `data` — it collides with
// QQuickItem's built-in `data` group and silently breaks field bindings.
Dialog {
    id: page

    property string agentId: ""
    readonly property bool isAdd: agentId.length === 0
    property var agentData: agentId.length > 0 ? agents.model.agent(agentId) : ({})

    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    width: 640
    // Bounded by the screen; the form scrolls internally.
    height: Math.min(scrollView.implicitHeight, Screen.height - 120)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: theme.overlayBg
        border.color: theme.borderSubtle
        border.width: 1
        radius: theme.radiusOverlay
    }

    function openFor(id) {
        agentId = id
        open()
    }

    // --- Validation --------------------------------------------------------
    readonly property bool nameValid: nameField.text.trim().length > 0
    readonly property bool commandValid: commandField.text.trim().length > 0
    readonly property bool webUrlValid: /^https?:\/\/\S+$/.test(webUrlField.text.trim())
    readonly property bool colorValid: colorField.text.trim().length === 0
                                      || /^#[0-9a-fA-F]{6}$/.test(colorField.text.trim())
    readonly property bool cardColorValid: cardColorField.text.trim().length === 0
                                           || /^#[0-9a-fA-F]{6}$/.test(cardColorField.text.trim())
    readonly property bool idValid: {
        if (!isAdd)
            return true
        const t = idField.text.trim()
        if (t.length === 0)
            return true
        return /^[A-Za-z0-9_-]+$/.test(t) && agents.model.indexOf(t) < 0
    }
    readonly property bool formValid: nameValid && commandValid && webUrlValid
                                      && colorValid && cardColorValid && idValid

    function save() {
        const fields = {
            "name": nameField.text.trim(),
            "command": commandField.text.trim(),
            "webUrl": webUrlField.text.trim(),
            "configDir": configDirField.text.trim(),
            "icon": iconField.text.trim(),
            "color": colorField.text.trim(),
            "cardColor": cardColorField.text.trim(),
            "installCommand": installField.text.trim(),
            "updateCommand": updateField.text.trim(),
            "versionCommand": versionField.text.trim(),
            "setupCommand": setupField.text.trim(),
            "tokenFile": tokenFileField.text.trim()
        }
        let ok = false
        if (isAdd) {
            fields["id"] = idField.text.trim()
            ok = agents.addAgent(fields)
        } else {
            ok = agents.updateAgentFull(page.agentId, fields)
        }
        if (ok)
            page.close()
        else
            saveErrorPopup.open()
    }


    // Form rows use the shared AFormLabel / ATextField components
    // (components/), with Layout.fillWidth set at each use site.

    component SectionLabel: Label {
        color: theme.accent
        font.pixelSize: theme.fontSizeSubtitle
        font.bold: true
        Layout.topMargin: theme.spacingM
        Layout.fillWidth: true
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: theme.spacingM

            RowLayout {
                Layout.leftMargin: theme.spacingXl
                Layout.rightMargin: theme.spacingXl
                Layout.topMargin: theme.spacingXl
                spacing: theme.spacingM

                AButton {
                    variant: "ghost"
                    text: qsTr("\u2190 Back")
                    onClicked: page.close()
                }
                Item { Layout.fillWidth: true }
            }

            ColumnLayout {
                Layout.leftMargin: theme.spacingXl
                Layout.rightMargin: theme.spacingXl
                spacing: theme.spacingXs

                Label {
                    text: page.isAdd ? qsTr("Add Launcher") : qsTr("Edit Launcher")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizePageTitle
                    font.bold: true
                }
                Label {
                    visible: !page.isAdd
                    text: page.agentData.running
                          ? qsTr("This agent is running. Changes take effect on the next launch.")
                          : qsTr("Changes are saved to the configuration file.")
                    color: theme.textDisabled
                    font.pixelSize: theme.fontSizeBody
                }
            }

            // --- Basics ---------------------------------------------------
            ColumnLayout {
                Layout.leftMargin: theme.spacingXl
                Layout.rightMargin: theme.spacingXl
                spacing: theme.spacingS
                Layout.fillWidth: true

                SectionLabel { text: qsTr("Basics") }

                AFormLabel {
                    labelText: qsTr("Name")
                    isRequired: true
                    tip: qsTr("Display name shown on the launcher card, e.g. \"Kimi Code\".")
                }
                ATextField {
        Layout.fillWidth: true
                    id: nameField
                    text: page.agentData.name || ""
                    placeholderText: qsTr("e.g. Kimi Code")
                    invalid: !page.nameValid
                }

                AFormLabel {
                    labelText: qsTr("Command")
                    isRequired: true
                    tip: qsTr("Command line that starts the agent, e.g. \"kimi web --port 58628\". It runs in the background without a visible window.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: commandField
                    text: page.agentData.command || ""
                    placeholderText: qsTr("e.g. opencode web --port 4096")
                    invalid: !page.commandValid
                }

                AFormLabel {
                    labelText: qsTr("Web URL")
                    isRequired: true
                    tip: qsTr("The agent's web UI address. Used as a health check to detect whether the agent is running, and opened in the browser. Keep the port in sync with the command.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: webUrlField
                    text: page.agentData.webUrl || ""
                    placeholderText: qsTr("e.g. http://127.0.0.1:4096")
                    invalid: !page.webUrlValid
                }
                Label {
                    visible: !page.webUrlValid && webUrlField.text.trim().length > 0
                    text: qsTr("Must be a valid http:// or https:// URL.")
                    color: theme.danger
                    font.pixelSize: theme.fontSizeSmall
                }

                AFormLabel {
                    labelText: qsTr("ID")
                    tip: qsTr("Unique identifier stored in the configuration file. Leave empty to generate it from the name. It cannot be changed after creation.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: idField
                    text: page.isAdd ? "" : page.agentId
                    placeholderText: qsTr("auto-generated from name")
                    readOnly: !page.isAdd
                    color: page.isAdd ? theme.textPrimary : theme.textMuted
                    invalid: !page.idValid
                }

                AFormLabel {
                    labelText: qsTr("Config directory")
                    tip: qsTr("The agent's own configuration folder, e.g. \"%USERPROFILE%/.kimi-code\". Opened from the card's context menu. %VAR% and ~ are expanded.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: configDirField
                    text: page.agentData.configDir || ""
                    placeholderText: qsTr("e.g. %USERPROFILE%/.config/opencode")
                }
            }

            // --- Appearance -----------------------------------------------
            ColumnLayout {
                Layout.leftMargin: theme.spacingXl
                Layout.rightMargin: theme.spacingXl
                spacing: theme.spacingS
                Layout.fillWidth: true

                SectionLabel { text: qsTr("Appearance") }

                AFormLabel {
                    labelText: qsTr("Icon")
                    tip: qsTr("Built-in icon (qrc:/icons/<name>.svg), a local file path (%VAR% and ~ expanded), or an http(s):// URL. Leave empty for the default icon. Click a built-in icon below to fill the field.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingS

                    ATextField {
        Layout.fillWidth: true
                        id: iconField
                        text: page.agentData.icon || ""
                        placeholderText: qsTr("qrc:/icons/<name>.svg, file path or URL")
                    }
                    // Live preview (qrc/http/file only; raw local paths are
                    // resolved by the C++ side on save).
                    Image {
                        source: {
                            const t = iconField.text.trim()
                            if (t.startsWith("qrc:/") || t.startsWith("http://")
                                    || t.startsWith("https://") || t.startsWith("file://"))
                                return t
                            return "qrc:/icons/default.svg"
                        }
                        sourceSize: Qt.size(30, 30)
                        fillMode: Image.PreserveAspectFit
                    }
                }
                // Built-in icon quick picks.
                Row {
                    spacing: theme.spacingS

                    Repeater {
                        model: ["default", "terminal", "cube", "bot"]

                        delegate: Item {
                            width: 30
                            height: 30

                            Rectangle {
                                anchors.fill: parent
                                radius: theme.radiusControl
                                color: pickArea.containsMouse ? theme.surfaceAltBg : theme.surfaceBg
                                border.color: theme.borderSubtle
                            }
                            Image {
                                anchors.centerIn: parent
                                source: "qrc:/icons/" + modelData + ".svg"
                                sourceSize: Qt.size(20, 20)
                                fillMode: Image.PreserveAspectFit
                            }
                            MouseArea {
                                id: pickArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: iconField.text = "qrc:/icons/" + modelData + ".svg"
                            }
                        }
                    }
                }

                AFormLabel {
                    labelText: qsTr("Color")
                    tip: qsTr("Accent color of the card in #RRGGBB form, e.g. #89B4FA. Leave empty to auto-assign a color from the built-in palette.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingS
                    ATextField {
        Layout.fillWidth: true
                        id: colorField
                        text: page.agentData.color || ""
                        placeholderText: qsTr("auto-assigned")
                        invalid: !page.colorValid
                    }
                    Rectangle {
                        width: 30
                        height: 30
                        radius: theme.radiusControl
                        color: page.colorValid && colorField.text.trim().length > 0
                               ? colorField.text.trim() : "transparent"
                        border.color: theme.borderSubtle
                    }
                }

                AFormLabel {
                    labelText: qsTr("Card color")
                    tip: qsTr("Background color of the card in #RRGGBB form while the agent is not running. Leave empty for the default surface background.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingS
                    ATextField {
        Layout.fillWidth: true
                        id: cardColorField
                        text: page.agentData.cardColor || ""
                        placeholderText: qsTr("default: surface background")
                        invalid: !page.cardColorValid
                    }
                    Rectangle {
                        width: 30
                        height: 30
                        radius: theme.radiusControl
                        color: page.cardColorValid && cardColorField.text.trim().length > 0
                               ? cardColorField.text.trim() : "transparent"
                        border.color: theme.borderSubtle
                    }
                }
            }

            // --- Install & maintenance ------------------------------------
            ColumnLayout {
                Layout.leftMargin: theme.spacingXl
                Layout.rightMargin: theme.spacingXl
                spacing: theme.spacingS
                Layout.fillWidth: true

                SectionLabel { text: qsTr("Install & Maintenance") }

                AFormLabel {
                    labelText: qsTr("Install command")
                    tip: qsTr("Command that installs the agent, e.g. \"npm install -g @kimi-code/cli\". Offered on the card when the agent is not installed.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: installField
                    text: page.agentData.installCommand || ""
                    placeholderText: qsTr("e.g. npm install -g opencode-ai")
                }

                AFormLabel {
                    labelText: qsTr("Update command")
                    tip: qsTr("Command that updates the agent to the latest version, e.g. \"npm update -g @kimi-code/cli\". Run from the card's context menu.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: updateField
                    text: page.agentData.updateCommand || ""
                    placeholderText: qsTr("e.g. npm update -g opencode-ai")
                }

                AFormLabel {
                    labelText: qsTr("Version command")
                    tip: qsTr("Command that prints the agent's version, e.g. \"kimi --version\". Run silently at startup to detect whether the agent is installed.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: versionField
                    text: page.agentData.versionCommand || ""
                    placeholderText: qsTr("e.g. opencode --version")
                }
            }

            // --- Advanced -------------------------------------------------
            ColumnLayout {
                Layout.leftMargin: theme.spacingXl
                Layout.rightMargin: theme.spacingXl
                spacing: theme.spacingS
                Layout.bottomMargin: theme.spacingXl
                Layout.fillWidth: true

                SectionLabel { text: qsTr("Advanced") }

                AFormLabel {
                    labelText: qsTr("First-run setup command")
                    tip: qsTr("One-time command run before the agent's first launch (e.g. generating a token). Runs only once; a successful run is remembered. Leave empty for no setup.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: setupField
                    text: page.agentData.setupCommand || ""
                    placeholderText: qsTr("optional")
                }

                AFormLabel {
                    labelText: qsTr("Token file")
                    tip: qsTr("Path to a bearer-token file (%VAR% and ~ expanded). Its content is passed to the agent on launch and appended to the Web URL as #token=... when opening the browser.")
                }
                ATextField {
        Layout.fillWidth: true
                    id: tokenFileField
                    text: page.agentData.tokenFile || ""
                    placeholderText: qsTr("optional")
                }

                Row {
                    spacing: theme.spacingM
                    Layout.topMargin: theme.spacingL
                    Layout.alignment: Qt.AlignRight

                    AButton {
                        text: qsTr("Cancel")
                        onClicked: page.close()
                    }
                    AButton {
                        id: saveButton
                        variant: "primary"
                        // The agent's own color when set; theme accent otherwise.
                        accentColor: page.agentData.color.length > 0
                                     ? page.agentData.color : theme.accent
                        enabled: page.formValid
                        text: qsTr("Save")
                        onClicked: page.save()
                    }
                }
            }
        }
    }

    // Shown when addAgent/updateAgentFull could not write agents.json.
    Popup {
        id: saveErrorPopup
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
            width: saveErrorPopup.availableWidth
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
                onClicked: saveErrorPopup.close()
            }
        }
    }
}
