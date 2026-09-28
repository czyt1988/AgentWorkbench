import QtQuick
import QtQuick.Controls
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

    // 选目录走 ui.pickFolder（C++ 的原生 IFileOpenDialog，Qt 5/Qt 6 同一实
    // 现与同一行为；QML FolderDialog 是 Qt 6 QuickDialogs2 独有）。返回的本
    // 就是本地路径，取消时为空串。
    function addWorkspaceViaDialog() {
        const path = ui.pickFolder(qsTr("Choose a workspace folder"))
        if (path.length === 0)
            return
        const result = tools.addWorkspace(path)
        if (!result.ok)
            workbench.notify("error", qsTr("Could not add the workspace"),
                             result.error)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            title: qsTr("Agent Tools")
            subtitle: qsTr("Compose prompts without accidentally sending them")
        }

        // --- Toolbar: workspace switcher + add + refresh + copy -----------
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
                onClicked: page.addWorkspaceViaDialog()
            }

            AIconButton {
                iconSource: "qrc:/icons/refresh.svg"
                tooltip: qsTr("Refresh the file tree")
                enabled: tools.currentWorkspace.length > 0
                onClicked: tools.refresh()
            }

            Item {
                Layout.fillWidth: true
            }

            // 页面主动作，靠右落在编辑区正上方：与刷新同一水平线，
            // 也贴着它作用的那个编辑框。
            AButton {
                text: qsTr("Copy")
                onClicked: page.copyDraft()
            }
        }

        // --- Body: prompt editor (left) + workspace file tree (right) ----
        // SplitView：中间分割条可拖，编辑区吃剩余宽度。最小宽保证两边
        // 都不会被拖到不可用；preferred 是初始/复位宽度。
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingL
            orientation: Qt.Horizontal

            handle: Rectangle {
                id: splitHandle
                implicitWidth: theme.spacingXs
                implicitHeight: implicitWidth
                color: SplitHandle.pressed ? theme.accent
                       : (SplitHandle.hovered ? theme.borderStrong
                                              : theme.borderSubtle)
                // 视觉只有一条 4px 的线，命中区放大到 12px 才好抓。
                // 引用必须走 id：mask 在首次求值时尚未重父级到 handle，
                // parent 是 null。
                containmentMask: Item {
                    x: (splitHandle.width - width) / 2
                    width: 12
                    height: splitHandle.height
                }
            }

            ATextArea {
                id: promptEditor

                SplitView.fillWidth: true
                SplitView.minimumWidth: 260
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
                        // TextArea 不是 Flickable，没有 contentX/contentY（读出来
                        // 是 undefined，一加就变 NaN，positionAt 于是永远返回 0 =
                        // 文首）。它自己就吃控件坐标，内边距与滚动都由它内部折算。
                        promptEditor.insert(promptEditor.positionAt(drop.x, drop.y),
                                            drop.text)
                        drop.acceptProposedAction()
                    }
                }
            }

            Rectangle {
                id: treePanel

                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 220
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

                    // 文件树消费扁平投影（FileTreeFlatModel，经 tools.model 暴露）：
                    // Qt 6.3 的 TreeView 在 Qt 5.15 不存在，两个版本统一用
                    // ListView + depth/expanded role 渲染同一份数据。
                    ListView {
                        id: fileTree

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: tools.currentWorkspace.length > 0
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: tools.model

                        ScrollBar.vertical: AScrollBar {}

                        delegate: Item {
                            id: treeRow

                            width: fileTree.width
                            height: 28

                            // DropArea 据此识别拖拽来源（见编辑区的 onDropped）。
                            readonly property bool isFileReferenceDrag: true
                            // 行被回收进池子到再次被复用之间为真。模型 reset
                            // 期间 role 会短暂换值，动画要闭嘴，否则整棵树的
                            // 箭头会一起转一遍。
                            property bool rebinding: false

                            // role 一律经 required property 声明（模型上下文
                            // 与之混用会静默取不到值）。
                            required property string name
                            required property string path
                            required property string relativePath
                            required property bool isDir
                            required property string iconSource
                            required property int depth
                            required property bool expanded
                            required property bool hasChildren

                            // ListView 没有 TableView 的池化信号：reset 期间
                            // 用模型的计数变化闭动画。
                            Connections {
                                target: tools.model
                                function onModelReset() {
                                    treeRow.rebinding = true
                                }
                            }
                            Component.onCompleted: treeRow.rebinding = false

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

                            // 单击目录：切换展开（fetch 兜底在模型的
                            // toggleExpanded 内部完成）。
                            TapHandler {
                                onSingleTapped: {
                                    if (treeRow.isDir)
                                        tools.model.toggleExpanded(index)
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

                            // 右键复制路径：只吃右键，左键留给上面的 TapHandler。
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: function(mouse) {
                                    pathMenu.popup(mouse.x, mouse.y)
                                }
                            }

                            Menu {
                                id: pathMenu

                                MenuItem {
                                    text: qsTr("Copy relative path")
                                    onTriggered: workbench.copyText(treeRow.relativePath)
                                }
                                MenuItem {
                                    text: qsTr("Copy absolute path")
                                    onTriggered: workbench.copyText(treeRow.path)
                                }
                            }

                            // 展开箭头：收起朝右，展开转 90 度朝下。
                            Image {
                                x: 4 + treeRow.depth * 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 14
                                source: "qrc:/icons/chevron-right.svg"
                                visible: treeRow.hasChildren
                                rotation: treeRow.expanded ? 90 : 0

                                Behavior on rotation {
                                    enabled: !treeRow.rebinding
                                    NumberAnimation {
                                        duration: theme.durationFast
                                    }
                                }
                            }

                            // 图标由模型按文件名/后缀查表给出（见 FileIcons），
                            // 页面不认识具体后缀。
                            Image {
                                x: 4 + treeRow.depth * 16 + 18
                                anchors.verticalCenter: parent.verticalCenter
                                width: 16
                                height: 16
                                source: treeRow.iconSource
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
                                 && tools.model.visibleCount === 0
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
                        onActionClicked: page.addWorkspaceViaDialog()
                    }
                }
            }
        }
    }
}
