import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Agent Tools 页：提示词编写台。解决两个痛点——引用文件要手拼相对路径、
// 在 agent CLI 里误触回车把半成品发送出去。本页没有发送动作：回车只换行，
// 写好后点 Copy 粘贴进目标 agent；文件树（后续提交）提供拖拽引用。
Item {
    id: page

    function currentWorkspaceIndex() {
        for (let i = 0; i < tools.workspaces.length; ++i) {
            if (tools.workspaces[i] === tools.currentWorkspace)
                return i
        }
        return -1
    }

    // file:///C:/x%20y -> C:/x y（FolderDialog 给的是 URL，含百分号转义）。
    function toLocalPath(folderUrl) {
        let text = folderUrl.toString()
        if (text.startsWith("file:///"))
            text = text.substring(8)
        else if (text.startsWith("file://"))
            text = text.substring(7)
        return decodeURIComponent(text)
    }

    function copyDraft() {
        if (tools.draft.length === 0) {
            workbench.notify("info", qsTr("Nothing to copy"),
                             qsTr("The editor is empty."))
            return
        }
        workbench.copyText(tools.draft)
        workbench.notify("success", qsTr("Copied"),
                         qsTr("The prompt is on the clipboard."))
    }

    function addWorkspaceFromDialog() {
        const path = toLocalPath(folderDialog.selectedFolder)
        if (path.length === 0)
            return
        const result = tools.addWorkspace(path)
        if (!result.ok)
            workbench.notify("error", qsTr("Could not add the workspace"),
                             result.error)
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose a workspace folder")
        onAccepted: page.addWorkspaceFromDialog()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            title: qsTr("Agent Tools")
            subtitle: qsTr("Compose prompts without accidentally sending them")

            AButton {
                text: qsTr("Copy")
                onClicked: page.copyDraft()
            }
        }

        // --- Toolbar: workspace switcher + add + refresh -----------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingM
            spacing: theme.spacingM

            Label {
                text: qsTr("Workspace")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeSmall
            }

            ComboBox {
                id: workspaceCombo

                Layout.preferredWidth: 340
                model: tools.workspaces
                currentIndex: page.currentWorkspaceIndex()
                enabled: tools.workspaces.length > 0

                // 路径可能很长，中部省略让盘符与末段同时可见。
                contentItem: Text {
                    text: workspaceCombo.displayText
                    font.pixelSize: theme.fontSizeSmall
                    color: workspaceCombo.enabled ? theme.textPrimary
                                                  : theme.textDisabled
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                    leftPadding: 10
                    rightPadding: 34
                }

                onActivated: function(index) {
                    tools.currentWorkspace = tools.workspaces[index]
                }

                delegate: ItemDelegate {
                    id: workspaceChoice

                    required property var modelData
                    required property int index

                    width: workspaceCombo.width
                    highlighted: workspaceCombo.highlightedIndex === index

                    contentItem: RowLayout {
                        spacing: theme.spacingS

                        Label {
                            Layout.fillWidth: true
                            text: workspaceChoice.modelData
                            color: theme.textPrimary
                            font.pixelSize: theme.fontSizeSmall
                            elide: Text.ElideMiddle
                        }

                        AIconButton {
                            iconSource: "qrc:/icons/close.svg"
                            tooltip: qsTr("Remove this workspace")
                            onClicked: {
                                const result =
                                        tools.removeWorkspace(workspaceChoice.modelData)
                                if (!result.ok)
                                    workbench.notify("error",
                                                     qsTr("Could not remove the workspace"),
                                                     result.error)
                            }
                        }
                    }
                }
            }

            AButton {
                text: qsTr("Add Folder...")
                onClicked: folderDialog.open()
            }

            AIconButton {
                iconSource: "qrc:/icons/refresh.svg"
                tooltip: qsTr("Refresh the file tree")
                enabled: tools.currentWorkspace.length > 0
                onClicked: tools.refresh()
            }
        }

        // --- Body: prompt editor (file tree joins on the right later) ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingL
            spacing: theme.spacingL

            ATextArea {
                id: promptEditor

                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: qsTr("Write your prompt here. Enter only inserts a new line; nothing is sent from this page.")
                // 只在初始化时从门面取草稿：常驻双向绑定会与用户输入互相打架。
                Component.onCompleted: text = tools.draft
                onTextChanged: tools.draft = text
            }
        }
    }
}
