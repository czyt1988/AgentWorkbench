import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·启动器分区：launcher 列表的增删改。编辑、删除确认与保存失败
// 提示三个对话框归本页私有（AgentGridPage 另有自己的编辑对话框实例）。
Item {
    id: page

    function openEditor(agentId) {
        editDialog.openFor(agentId)
    }

    AgentEditDialog {
        id: editDialog
    }

    // --- Delete confirmation -----------------------------------------------
    AConfirmDialog {
        id: deleteConfirmPopup
        danger: true
        titleText: qsTr("Delete Launcher")
        message: qsTr("Remove \"%1\" from the launcher list?")
                  .arg(deleteConfirmPopup.pendingName)
        confirmText: qsTr("Delete")
        cancelText: qsTr("Cancel")
        property string pendingId: ""
        property string pendingName: ""
        property bool pendingRunning: false
        property bool pendingBuiltin: false

        // Contextual warnings injected between message and buttons.
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

        onConfirmed: {
            if (!agents.removeAgent(deleteConfirmPopup.pendingId))
                errorPopup.open()
        }
    }

    // Shown when removeAgent could not write agents.json.
    AAlertDialog {
        id: errorPopup
        titleText: qsTr("Save failed")
        message: qsTr("Could not write the configuration file:")
        detail: agents.configFilePath()
        dismissText: qsTr("OK")
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

            PageHeader {
                title: qsTr("Launchers")
                subtitle: qsTr("The agents available on the launcher page")
                AButton {
                    text: qsTr("Add Launcher")
                    onClicked: page.openEditor("")
                }
            }

            // 启动时版本检查：关掉后卡片不显示版本号与安装状态（省一
            // 轮子进程探测）。开关只影响下一次启动。
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Check launcher versions at startup")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                Switch {
                    checked: agents.startupVersionCheck
                    onToggled: agents.setStartupVersionCheck(checked)
                }
            }

            // 托盘与单实例（launcher.* 一节）。
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Show tray icon")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                Switch {
                    checked: shell.trayEnabled
                    onToggled: shell.trayEnabled = checked
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("When remembered, close to tray instead of exiting")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                Switch {
                    checked: shell.closeToTray
                    enabled: shell.trayEnabled
                    onToggled: shell.closeToTray = checked
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Remember my close choice")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                Switch {
                    checked: shell.rememberCloseAction
                    enabled: shell.trayEnabled
                    onToggled: shell.rememberCloseAction = checked
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Single instance")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                Switch {
                    checked: shell.singleInstance
                    onToggled: shell.singleInstance = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingM

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Open the Markdown workbench on startup")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                }
                Switch {
                    checked: markdown.autoOpenWorkbench
                    onToggled: markdown.autoOpenWorkbench = checked
                }
            }

            // 文件关联（E2）：只出现在有预览能力的构建里——没有它，双击
            // .md 只会收获一个报因的 toast，注册入口不该出现。
            ColumnLayout {
                id: associationSection
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingXs
                visible: markdown.available

                // 注册态在开页时读一次、点击后手动刷新——它是注册表
                // 事实，没有变更信号可绑。
                property bool registered:
                    workbench.markdownFileAssociationRegistered()

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingM

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Associate .md files")
                        color: theme.textPrimary
                        font.pixelSize: theme.fontSizeBody
                    }
                    AButton {
                        text: associationSection.registered
                              ? qsTr("Registered")
                              : qsTr("Associate")
                        enabled: !associationSection.registered
                        onClicked: {
                            const result =
                                    workbench.registerMarkdownFileAssociation()
                            // 成功与失败都给一句话：失败的原因（策略/权限）
                            // 由注册动作带回，成功提示「去系统设置选默认」。
                            workbench.notify(
                                result.ok ? "success" : "error",
                                result.ok
                                    ? qsTr("Added to the Open with list")
                                    : qsTr("Could not register"),
                                result.ok
                                    ? qsTr("Pick the default app for .md files in Windows Settings.")
                                    : result.error)
                            associationSection.registered =
                                    workbench.markdownFileAssociationRegistered()
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                    wrapMode: Text.Wrap
                    text: qsTr("Puts this app in the \"Open with\" list for .md and .markdown files. The default app is chosen in Windows Settings, not here.")
                }
            }

            // singleInstance 只在启动时被读一次（实例守卫在界面装配之前
            // 就已经判定），改完要到下次启动才生效。
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.Wrap
                text: qsTr("Single instance takes effect at the next start")
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.spacingL
                Layout.rightMargin: theme.spacingL
                spacing: theme.spacingS

                Repeater {
                    model: agents.model

                    delegate: AListRow {
                        rowHeight: 60
                        Layout.fillWidth: true
                        // Rows are delegates; deleting an agent while its
                        // status-dot tooltip is showing must not freeze the
                        // shared tooltip on screen.
                        Component.onDestruction: ToolTip.hide()

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
                                font.pixelSize: theme.fontSizeCaption
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
    }
}
