import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Full add/edit form for one launcher. Empty agentId = add mode.
// NOTE: do not name the agent map property `data` — it collides with
// QQuickItem's built-in `data` group and silently breaks field bindings.
Page {
    id: page

    property string agentId: ""
    readonly property bool isAdd: agentId.length === 0
    property var agentData: agentId.length > 0 ? agents.model.agent(agentId) : ({})

    background: Rectangle { color: "#1e1e2e" }

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
            page.StackView.view.pop()
        else
            saveErrorPopup.open()
    }

    // Label row: text + red required marker + info icon, each Label is a
    // tooltip hover source (MouseArea child, like the home-page badges).
    component FormLabel: RowLayout {
        id: labelRow
        Layout.fillWidth: true
        property string labelText: ""
        property bool isRequired: false
        property string tip: ""
        spacing: 2

        Label {
            text: labelRow.labelText
            color: "#a6adc8"
            font.pixelSize: 13

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                ToolTip.text: labelRow.tip
                ToolTip.visible: containsMouse && labelRow.tip.length > 0
                ToolTip.delay: 300
            }
        }
        Label {
            text: "*"
            color: "#f38ba8"
            font.pixelSize: 13
            visible: labelRow.isRequired
        }
        Label {
            text: "\u2139"
            color: "#6c7086"
            font.pixelSize: 12
            visible: labelRow.tip.length > 0

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                ToolTip.text: labelRow.tip
                ToolTip.visible: containsMouse
                ToolTip.delay: 300
            }
        }
        Item { Layout.fillWidth: true }
    }

    // Dark-themed TextField with an invalid (red border) state.
    component FormTextField: TextField {
        id: input
        property bool invalid: false
        Layout.fillWidth: true
        color: "#cdd6f4"
        background: Rectangle {
            radius: 8
            color: "#313244"
            border.color: input.invalid ? "#f38ba8" : "#45475a"
        }
    }

    component SectionLabel: Label {
        color: "#89b4fa"
        font.pixelSize: 15
        font.bold: true
        Layout.topMargin: 14
        Layout.fillWidth: true
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: 12

            RowLayout {
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                Layout.topMargin: 24
                spacing: 12

                Button {
                    text: qsTr("\u2190 Back")
                    background: Rectangle { color: "transparent" }
                    contentItem: Label { text: parent.text; color: "#89b4fa"; font.pixelSize: 16 }
                    onClicked: page.StackView.view.pop()
                }
                Item { Layout.fillWidth: true }
            }

            ColumnLayout {
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 4

                Label {
                    text: page.isAdd ? qsTr("Add Launcher") : qsTr("Edit Launcher")
                    color: "#cdd6f4"
                    font.pixelSize: 24
                    font.bold: true
                }
                Label {
                    visible: !page.isAdd
                    text: page.agentData.running
                          ? qsTr("This agent is running. Changes take effect on the next launch.")
                          : qsTr("Changes are saved to the configuration file.")
                    color: "#6c7086"
                    font.pixelSize: 12
                }
            }

            // --- Basics ---------------------------------------------------
            ColumnLayout {
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 10
                Layout.fillWidth: true

                SectionLabel { text: qsTr("Basics") }

                FormLabel {
                    labelText: qsTr("Name")
                    isRequired: true
                    tip: qsTr("Display name shown on the launcher card, e.g. \"Kimi Code\".")
                }
                FormTextField {
                    id: nameField
                    text: page.agentData.name || ""
                    placeholderText: qsTr("e.g. Kimi Code")
                    invalid: !page.nameValid
                }

                FormLabel {
                    labelText: qsTr("Command")
                    isRequired: true
                    tip: qsTr("Command line that starts the agent, e.g. \"kimi web --port 58628\". It runs in the background without a visible window.")
                }
                FormTextField {
                    id: commandField
                    text: page.agentData.command || ""
                    placeholderText: qsTr("e.g. opencode web --port 4096")
                    invalid: !page.commandValid
                }

                FormLabel {
                    labelText: qsTr("Web URL")
                    isRequired: true
                    tip: qsTr("The agent's web UI address. Used as a health check to detect whether the agent is running, and opened in the browser. Keep the port in sync with the command.")
                }
                FormTextField {
                    id: webUrlField
                    text: page.agentData.webUrl || ""
                    placeholderText: qsTr("e.g. http://127.0.0.1:4096")
                    invalid: !page.webUrlValid
                }
                Label {
                    visible: !page.webUrlValid && webUrlField.text.trim().length > 0
                    text: qsTr("Must be a valid http:// or https:// URL.")
                    color: "#f38ba8"
                    font.pixelSize: 11
                }

                FormLabel {
                    labelText: qsTr("ID")
                    tip: qsTr("Unique identifier stored in the configuration file. Leave empty to generate it from the name. It cannot be changed after creation.")
                }
                FormTextField {
                    id: idField
                    text: page.isAdd ? "" : page.agentId
                    placeholderText: qsTr("auto-generated from name")
                    readOnly: !page.isAdd
                    color: page.isAdd ? "#cdd6f4" : "#7f849c"
                    invalid: !page.idValid
                }

                FormLabel {
                    labelText: qsTr("Config directory")
                    tip: qsTr("The agent's own configuration folder, e.g. \"%USERPROFILE%/.kimi-code\". Opened from the card's context menu. %VAR% and ~ are expanded.")
                }
                FormTextField {
                    id: configDirField
                    text: page.agentData.configDir || ""
                    placeholderText: qsTr("e.g. %USERPROFILE%/.config/opencode")
                }
            }

            // --- Appearance -----------------------------------------------
            ColumnLayout {
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 10
                Layout.fillWidth: true

                SectionLabel { text: qsTr("Appearance") }

                FormLabel {
                    labelText: qsTr("Icon")
                    tip: qsTr("Built-in icon (qrc:/icons/<name>.svg), a local file path (%VAR% and ~ expanded), or an http(s):// URL. Leave empty for the default icon. Click a built-in icon below to fill the field.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    FormTextField {
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
                    spacing: 6

                    Repeater {
                        model: ["default", "terminal", "cube", "bot"]

                        delegate: Item {
                            width: 30
                            height: 30

                            Rectangle {
                                anchors.fill: parent
                                radius: 8
                                color: pickArea.containsMouse ? "#45475a" : "#313244"
                                border.color: "#45475a"
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

                FormLabel {
                    labelText: qsTr("Color")
                    tip: qsTr("Accent color of the card in #RRGGBB form, e.g. \"#89b4fa\". Leave empty to auto-assign a color from the built-in palette.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FormTextField {
                        id: colorField
                        text: page.agentData.color || ""
                        placeholderText: qsTr("auto-assigned")
                        invalid: !page.colorValid
                    }
                    Rectangle {
                        width: 30
                        height: 30
                        radius: 8
                        color: page.colorValid && colorField.text.trim().length > 0
                               ? colorField.text.trim() : "transparent"
                        border.color: "#45475a"
                    }
                }

                FormLabel {
                    labelText: qsTr("Card color")
                    tip: qsTr("Background color of the card in #RRGGBB form while the agent is not running. Leave empty for the default (#313244).")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FormTextField {
                        id: cardColorField
                        text: page.agentData.cardColor || ""
                        placeholderText: qsTr("default (#313244)")
                        invalid: !page.cardColorValid
                    }
                    Rectangle {
                        width: 30
                        height: 30
                        radius: 8
                        color: page.cardColorValid && cardColorField.text.trim().length > 0
                               ? cardColorField.text.trim() : "transparent"
                        border.color: "#45475a"
                    }
                }
            }

            // --- Install & maintenance ------------------------------------
            ColumnLayout {
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 10
                Layout.fillWidth: true

                SectionLabel { text: qsTr("Install & Maintenance") }

                FormLabel {
                    labelText: qsTr("Install command")
                    tip: qsTr("Command that installs the agent, e.g. \"npm install -g @kimi-code/cli\". Offered on the card when the agent is not installed.")
                }
                FormTextField {
                    id: installField
                    text: page.agentData.installCommand || ""
                    placeholderText: qsTr("e.g. npm install -g opencode-ai")
                }

                FormLabel {
                    labelText: qsTr("Update command")
                    tip: qsTr("Command that updates the agent to the latest version, e.g. \"npm update -g @kimi-code/cli\". Run from the card's context menu.")
                }
                FormTextField {
                    id: updateField
                    text: page.agentData.updateCommand || ""
                    placeholderText: qsTr("e.g. npm update -g opencode-ai")
                }

                FormLabel {
                    labelText: qsTr("Version command")
                    tip: qsTr("Command that prints the agent's version, e.g. \"kimi --version\". Run silently at startup to detect whether the agent is installed.")
                }
                FormTextField {
                    id: versionField
                    text: page.agentData.versionCommand || ""
                    placeholderText: qsTr("e.g. opencode --version")
                }
            }

            // --- Advanced -------------------------------------------------
            ColumnLayout {
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 10
                Layout.bottomMargin: 24
                Layout.fillWidth: true

                SectionLabel { text: qsTr("Advanced") }

                FormLabel {
                    labelText: qsTr("First-run setup command")
                    tip: qsTr("One-time command run before the agent's first launch (e.g. generating a token). Runs only once; a successful run is remembered. Leave empty for no setup.")
                }
                FormTextField {
                    id: setupField
                    text: page.agentData.setupCommand || ""
                    placeholderText: qsTr("optional")
                }

                FormLabel {
                    labelText: qsTr("Token file")
                    tip: qsTr("Path to a bearer-token file (%VAR% and ~ expanded). Its content is passed to the agent on launch and appended to the Web URL as #token=... when opening the browser.")
                }
                FormTextField {
                    id: tokenFileField
                    text: page.agentData.tokenFile || ""
                    placeholderText: qsTr("optional")
                }

                Row {
                    spacing: 12
                    Layout.topMargin: 16
                    Layout.alignment: Qt.AlignRight

                    Button {
                        text: qsTr("Cancel")
                        background: Rectangle { radius: 8; color: parent.down ? "#45475a" : "#313244"; border.color: "#45475a" }
                        contentItem: Label { text: parent.text; color: "#cdd6f4"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        onClicked: page.StackView.view.pop()
                    }
                    Button {
                        id: saveButton
                        readonly property color accent: page.agentData.color || "#89b4fa"
                        enabled: page.formValid
                        text: qsTr("Save")
                        background: Rectangle {
                            radius: 8
                            color: saveButton.enabled
                                   ? (saveButton.down ? Qt.darker(saveButton.accent, 1.3) : saveButton.accent)
                                   : "#313244"
                            border.color: "#45475a"
                        }
                        contentItem: Label { text: parent.text; color: "#ffffff"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
            color: "#1e1e2e"
            border.color: "#f38ba8"
            border.width: 1
            radius: 12
        }

        ColumnLayout {
            width: saveErrorPopup.availableWidth
            spacing: 12

            Label {
                text: qsTr("Save failed")
                color: "#f38ba8"
                font.pixelSize: 16
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Could not write the configuration file:")
                color: "#cdd6f4"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: agents.configFilePath()
                color: "#89b4fa"
                font.pixelSize: 12
                font.family: "Consolas, Monaco, monospace"
                wrapMode: Text.WrapAnywhere
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                background: Rectangle { radius: 8; color: parent.down ? "#45475a" : "#313244"; border.color: "#45475a" }
                contentItem: Label { text: parent.text; color: "#cdd6f4"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: saveErrorPopup.close()
            }
        }
    }
}
