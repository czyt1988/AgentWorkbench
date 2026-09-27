import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Agent Tools 页：提示词编写台。解决两个痛点——引用文件要手拼相对路径、
// 在 agent CLI 里误触回车把半成品发送出去。本页没有发送动作：回车只换行，
// 写好后点 Copy 粘贴进目标 agent；右侧文件树浏览当前工作区，
// 双击文件行把 `./相对路径` 插到编辑区光标处。
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

    // 恢复刷新前的展开状态。模型在 refresh() 里已经把快照里的目录重新
    // fetch 好；这里只负责让视图重新展开它们（先父后子，子目录的视图行
    // 要等父目录展开后才存在）。
    function reexpandTree() {
        if (typeof fileTree.rowAtIndex !== "function")
            return
        fileTree.forceLayout()
        const paths = tools.model.restoredExpandedPaths.slice()
        paths.sort((a, b) => a.split("/").length - b.split("/").length)
        for (let i = 0; i < paths.length; ++i) {
            const index = tools.model.indexByPath(paths[i])
            if (!index.valid)
                continue
            fileTree.expandToIndex(index)
            const row = fileTree.rowAtIndex(index)
            if (row >= 0)
                fileTree.expand(row)
        }
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

        // --- Body: prompt editor (left) + workspace file tree (right) ----
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

            Rectangle {
                id: treePanel

                Layout.preferredWidth: 360
                Layout.fillHeight: true
                color: theme.surfaceBg
                radius: theme.radiusControl
                border.color: theme.borderSubtle
                border.width: 1
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: theme.spacingM
                        Layout.topMargin: theme.spacingS
                        Layout.bottomMargin: theme.spacingXs
                        text: qsTr("Files")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeSmall
                    }

                    TreeView {
                        id: fileTree

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: tools.currentWorkspace.length > 0
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: tools.model

                        ScrollBar.vertical: ScrollBar {}

                        // 视图把展开/收起回报给模型：refresh() 靠这份记录快照。
                        // 视图对 model reset 的反应是全部收起，旧行触发的信号
                        // 由模型的代数守卫丢弃。
                        onExpanded: function(row, depth) {
                            tools.model.setNodeExpanded(fileTree.index(row, 0), true)
                        }
                        onCollapsed: function(row, recursively) {
                            tools.model.setNodeExpanded(fileTree.index(row, 0), false)
                        }

                        delegate: Item {
                            id: treeRow

                            implicitWidth: fileTree.width
                            implicitHeight: 28

                            required property TreeView treeView
                            required property bool isTreeNode
                            required property bool expanded
                            required property bool hasChildren
                            required property int depth
                            required property int row
                            required property int column
                            // role 注入：TreeView delegate 里 model 上下文与
                            // required property 混用会静默取不到值，role 一律
                            // 经 required property 声明。
                            required property string name
                            required property string relativePath
                            required property bool isDir

                            // 悬停高亮：读树时逐行反馈落点。
                            Rectangle {
                                anchors.fill: parent
                                radius: theme.radiusControl
                                color: theme.surfaceHoverBg
                                visible: rowHover.hovered
                            }

                            HoverHandler {
                                id: rowHover
                            }

                            // 单击目录：先兜底 fetch 再切换展开（TreeView 不保证
                            // 替懒加载模型驱动 fetchMore）。文件双击插入见
                            // onDoubleTapped。
                            TapHandler {
                                onSingleTapped: {
                                    if (treeRow.isDir) {
                                        tools.model.fetchChildren(
                                                    treeView.index(treeRow.row,
                                                                   treeRow.column))
                                        treeView.toggleExpanded(treeRow.row)
                                    }
                                }
                            }

                            // 展开箭头：收起朝右，展开转 90 度朝下。
                            Image {
                                x: 4 + treeRow.depth * 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 14
                                source: "qrc:/icons/chevron-right.svg"
                                visible: treeRow.isTreeNode && treeRow.hasChildren
                                rotation: treeRow.expanded ? 90 : 0

                                Behavior on rotation {
                                    NumberAnimation {
                                        duration: theme.durationFast
                                    }
                                }
                            }

                            Image {
                                x: 4 + treeRow.depth * 16 + 18
                                anchors.verticalCenter: parent.verticalCenter
                                width: 16
                                height: 16
                                source: treeRow.isDir ? "qrc:/icons/folder.svg"
                                                      : "qrc:/icons/file.svg"
                            }

                            Label {
                                x: 4 + treeRow.depth * 16 + 40
                                width: parent.width - x - 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: treeRow.name
                                color: theme.textPrimary
                                font.pixelSize: theme.fontSizeSmall
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: tools.currentWorkspace.length > 0
                                 && tools.model.topLevelCount === 0
                        Layout.margins: theme.spacingM
                        text: qsTr("The workspace folder is empty or unavailable.")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeSmall
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    AEmptyState {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: tools.currentWorkspace.length === 0
                        iconSource: "qrc:/icons/folder.svg"
                        title: qsTr("No workspace selected")
                        description: qsTr("Add a folder to browse its files and insert references into the prompt.")
                        actionText: qsTr("Add Folder...")
                        onActionClicked: folderDialog.open()
                    }
                }
            }
        }
    }

    // watcher 触发的刷新没有 QML 调用点，经门面的 refreshFinished 统一收口。
    Connections {
        target: tools

        // callLater：视图在事件循环里排空 model reset 之后再恢复展开，
        // forceLayout 保证 rowAtIndex 拿到的是重排后的行号。
        function onRefreshFinished() {
            Qt.callLater(page.reexpandTree)
        }
    }
}
