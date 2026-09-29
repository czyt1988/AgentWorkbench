import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 单个 launcher 的新增/编辑全量表单弹窗：agentId 为空 = 新增模式。
// 字段校验在本地 readonly 属性上完成，保存经 agents.* 门面写回 agents.json。
// 注意：承载 agent 映射的属性不能叫 `data`——它会撞上 QQuickItem 内建的
// `data` 属性组并静默断掉字段绑定。
Dialog {
    id: page

    // 目标 agent id（空串 = 新增模式）。
    property string agentId: ""
    // 是否新增模式。
    readonly property bool isAdd: agentId.length === 0
    // 目标 agent 的当前数据（新增模式为空对象）。
    property var agentData: agentId.length > 0 ? agents.model.agent(agentId) : ({})

    // 挂到窗口 Overlay：对话框以「窗口」而不是「屏幕」为居中与限高的基准
    // ——popup 会被所在窗口裁剪，按 Screen.height 算出来的高度在窗口比屏幕
    // 矮时，超出的部分（含保存按钮）无论表单怎么滚都够不着。
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    width: 640
    // 以窗口高度为上界；表单内部滚动。
    height: Math.min(scrollView.implicitHeight, parent.height - 120)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: theme.overlayBg
        border.color: theme.borderSubtle
        border.width: 1
        radius: theme.radiusOverlay
    }

    // 按目标 agent 打开弹窗（空串 = 新增）。
    function openFor(id) {
        agentId = id
        open()
    }

    // --- 校验 ---------------------------------------------------------------
    // 各字段的合法性（name/command/webUrl 必填，颜色须为 #RRGGBB 或空，
    // 新增时 id 须为合法字符且不与现有 agent 重复），汇总进 formValid。
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

    // 收集全部字段写回：新增走 addAgent，编辑走 updateAgentFull；
    // 失败（agents.json 写不进去）时弹出错误提示而不是关窗。
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


    // 表单行用共享的 AFormLabel / ATextField（components/），
    // Layout.fillWidth 在各使用点设置。

    // 表单内的小节标题（accent 色加粗）。
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
        ScrollBar.vertical: AScrollBar {}

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

            // --- 基础信息 ------------------------------------------------------
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
                    font.pixelSize: theme.fontSizeCaption
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

            // --- 外观 ----------------------------------------------------------
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
                    // 实时预览（仅 qrc/http/file；裸本地路径由 C++ 侧在
                    // 保存时解析）。
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
                // 内置图标快捷选择。
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
                    tip: qsTr("Accent color of the card in #RRGGBB form, e.g. #89B4FA. Leave empty to auto-assign a color from the built-in palette. Click the swatch to pick a color.")
                }
                // 颜色行用 AColorField：文本框仍是真相源（校验、保存都
                // 读它的 text），色卡点开 AColorPicker 快速选色。
                AColorField {
                    Layout.fillWidth: true
                    id: colorField
                    text: page.agentData.color || ""
                    placeholderText: qsTr("auto-assigned")
                    invalid: !page.colorValid
                }

                AFormLabel {
                    labelText: qsTr("Card color")
                    tip: qsTr("Background color of the card in #RRGGBB form while the agent is not running. Leave empty for the default surface background. Click the swatch to pick a color.")
                }
                AColorField {
                    Layout.fillWidth: true
                    id: cardColorField
                    text: page.agentData.cardColor || ""
                    placeholderText: qsTr("default: surface background")
                    invalid: !page.cardColorValid
                }
            }

            // --- 安装与维护 ----------------------------------------------------
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

            // --- 高级 ----------------------------------------------------------
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
                        // 设了 agent 主色就用它，否则回退主题 accent。
                        accentColor: (page.agentData.color || "").length > 0
                                     ? page.agentData.color : theme.accent
                        enabled: page.formValid
                        text: qsTr("Save")
                        onClicked: page.save()
                    }
                }
            }
        }
    }

    // addAgent/updateAgentFull 写不进 agents.json 时展示。
    AAlertDialog {
        id: saveErrorPopup
        titleText: qsTr("Save failed")
        message: qsTr("Could not write the configuration file:")
        detail: agents.configFilePath()
        dismissText: qsTr("OK")
    }
}
