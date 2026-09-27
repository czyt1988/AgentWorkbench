import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Agent Tools 页：提示词编写台。解决两个痛点——引用文件要手拼相对路径、
// 在 agent CLI 里误触回车把半成品发送出去。本页没有发送动作：回车只换行，
// 写好后点 Copy 粘贴进目标 agent；右侧文件树浏览当前工作区，
// 把文件行拖进编辑区（或双击文件行）即插入 `./相对路径` 引用。
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

                // 拖放目标：接住来自文件树的行，把文件引用插到落点光标处。
                DropArea {
                    anchors.fill: parent

                    onEntered: function(drag) {
                        drag.accept(Qt.CopyAction)
                    }
                    onDropped: function(drop) {
                        // 只接受文件树来的行：编辑区自身的选区拖动
                        // （source 为空）不在此列。
                        if (!drop.source || !drop.source.isFileReferenceDrag)
                            return
                        // positionAt 要的是内容坐标；未滚动时 contentX/Y 为 0。
                        const pos = promptEditor.positionAt(
                                    drop.x + promptEditor.contentX,
                                    drop.y + promptEditor.contentY)
                        promptEditor.insert(pos, drop.text)
                        drop.acceptProposedAction()
                    }
                }
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

                        delegate: Item {
                            id: treeRow

                            implicitWidth: fileTree.width
                            implicitHeight: 28

                            // DropArea 据此识别拖拽来源（见编辑区的 onDropped）。
                            readonly property bool isFileReferenceDrag: true
                            // 行被回收进池子到再次被复用之间为真。一次展开/收起
                            // 会让 TreeView 把所有可见行回收再复用（实测：50 行
                            // 全部 pooled+reused），这期间 role 换成别行的值，
                            // 动画要闭嘴，否则整棵树的箭头会一起转一遍。
                            property bool rebinding: false

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

                            TableView.onPooled: treeRow.rebinding = true
                            TableView.onReused: treeRow.rebinding = false

                            // 拖出文件引用：Automatic 型拖拽，Drag.active 置真
                            // 即开始（mimeData 在会话开始时求值）。
                            Drag.dragType: Drag.Automatic
                            Drag.supportedActions: Qt.CopyAction
                            Drag.mimeData: {
                                "text/plain": tools.fileReference(treeRow.relativePath)
                            }
                            Drag.active: rowDragHandler.active

                            DragHandler {
                                id: rowDragHandler
                                target: null
                            }

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

                            // 单击目录：先兜底 fetch 再切换展开。Qt 6.7 的
                            // TreeView 也会经内部 proxy 替懒加载模型驱动
                            // fetchMore，但那是实现细节，不去依赖它。
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

                            // 双击文件：在编辑区光标处插入引用（目录的双击被
                            // 两次单击的展开/收起抵消）。
                            TapHandler {
                                onDoubleTapped: {
                                    if (!treeRow.isDir) {
                                        promptEditor.insert(
                                                    promptEditor.cursorPosition,
                                                    tools.fileReference(
                                                        treeRow.relativePath))
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
                                    enabled: !treeRow.rebinding
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
}
