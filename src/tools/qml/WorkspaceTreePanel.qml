import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 工作区文件树面板（工作台页的左栏）：树是「拿什么素材」的浏览器，不是
// 动作区。
//
// 容器件：工作区清单与文件树都来自自己域的 tools；凡是页面或别的域该做
// 的事一律发信号——开合（页面里两种预设各有自己的开合状态，还要落盘）、
// 双击打开文件、插引用、复制路径、预览 Markdown、选文件夹、通知。
// 预览能力的判据由宿主注入（canPreview）：哪些后缀归预览管是纯层的事实，
// QML 不写后缀判断。
Rectangle {
    id: root
    objectName: "leftPanel"

    ///< 展开态；收起时只剩一条窄边（页面里两种预设各有自己的开合状态，
    ///< 所以开合由宿主决定，本组件只报请求）
    property bool expanded: true
    ///< 面板宽度，由本组件持有：只在面板内部用（右缘手柄拖拽），不落盘，
    ///< 也不参与会话保存
    property real panelWidth: 320
    ///< 判断一个路径能否预览 Markdown——策略在宿主，这里只问
    property var canPreview: function(path) { return false }

    ///< 请求展开（窄边上的按钮是收起态唯一的还原入口）
    signal expandRequested()
    ///< 请求收起（段头按钮，或右缘手柄拖过下限）
    signal collapseRequested()
    ///< 请求选一个工作区文件夹
    signal addWorkspaceRequested()
    ///< 双击了一个文件
    signal fileActivated(string path)
    ///< 请求把文件引用插到光标处（引用串已由本域生成）
    signal referenceRequested(string reference)
    ///< 请求把一段文本复制到剪贴板
    signal copyRequested(string text)
    ///< 请求预览一份 Markdown
    signal previewRequested(string path)
    ///< 请求弹一条通知
    signal notifyRequested(string level, string title, string body)

    readonly property bool stripMode: !root.expanded

    Layout.fillHeight: true
    Layout.preferredWidth: stripMode ? 26 : root.panelWidth
    Layout.minimumWidth: 26
    color: stripMode ? theme.chromeBg : theme.surfaceBg
    border.color: stripMode ? "transparent" : theme.borderSubtle
    border.width: 1
    clip: true

    // 窄边：一条竖排的展开按钮（恢复上次宽度）。
    Item {
        anchors.fill: parent
        visible: root.stripMode

        AIconButton {
            objectName: "leftPanelRestoreButton"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: theme.spacingXs
            iconSource: "qrc:/icons/chevron-right.svg"
            tooltip: qsTr("Show the workspace tree")
            onClicked: root.expandRequested()
        }
    }

    // 展开态：头部一排（工作区切换 + 选文件夹 + 刷新）+ 文件树。
    // 与原提示词页同一套结构，objectName 契约不变。
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: !root.stripMode

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: theme.spacingXs
            spacing: theme.spacingXs

            AComboBox {
                id: workspaceCombo

                Layout.fillWidth: true
                Layout.minimumWidth: 110
                model: tools.workspaces
                currentIndex: {
                    for (let i = 0; i < tools.workspaces.length; ++i) {
                        if (tools.workspaces[i] === tools.currentWorkspace)
                            return i
                    }
                    return -1
                }
                enabled: tools.workspaces.length > 0

                // 路径可能很长，中部省略让盘符与末段同时可见。
                contentItem: Text {
                    text: workspaceCombo.displayText
                    font.pixelSize: theme.fontSizeCaption
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

                    property var scope: root

                    width: workspaceCombo.width
                    highlighted: workspaceCombo.highlightedIndex === index
                    // 弹层底色随 AComboBox 主题化后，条目悬停态也要跟令
                    // 牌走（默认 ItemDelegate 背景是硬编码浅色系）。
                    background: Rectangle {
                        color: workspaceChoice.hovered
                               || workspaceChoice.highlighted
                               ? theme.surfaceHoverBg : "transparent"
                    }

                    contentItem: RowLayout {
                        spacing: theme.spacingS

                        Label {
                            Layout.fillWidth: true
                            text: workspaceChoice.modelData
                            color: theme.textPrimary
                            font.pixelSize: theme.fontSizeCaption
                            elide: Text.ElideMiddle
                        }

                        AIconButton {
                            iconSource: "qrc:/icons/close.svg"
                            tooltip: qsTr("Remove this workspace")
                            onClicked: {
                                const result =
                                        tools.removeWorkspace(workspaceChoice.modelData)
                                if (!result.ok)
                                    scope.notifyRequested("error",
                                                     qsTr("Could not remove the workspace"),
                                                     result.error)
                            }
                        }
                    }
                }
            }

            // 选工作区文件夹：只放图标，说明走 tooltip。
            AIconButton {
                iconSource: "qrc:/icons/folder.svg"
                tooltip: qsTr("Choose a workspace folder")
                onClicked: root.addWorkspaceRequested()
            }

            AIconButton {
                iconSource: "qrc:/icons/refresh.svg"
                tooltip: qsTr("Refresh the file tree")
                enabled: tools.currentWorkspace.length > 0
                onClicked: tools.refresh()
            }
        }

        // --- 段 · 工作区与文件树 --------------------------------
        // 左栏唯一的一段，不再有段级折叠（2026-10 用户报告：段折叠
        // 是早期左栏多段布局的遗留，收起语义应当属于整个左栏）。
        // 段头右侧的收起键收起整个左栏（与分隔条拖到 140 以下同效，
        // 窄边上的展开键是还原入口）。
        ColumnLayout {
            id: workspaceSection
            objectName: "workspaceSection"

            // 树吃掉全部剩余高度；无工作区时空态占位
            //（不另设弹簧项）。
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: theme.spacingXs

            ASectionHeader {
                Layout.fillWidth: true
                text: qsTr("Workspace")

                AIconButton {
                    objectName: "workspaceSectionToggle"
                    iconSource: "qrc:/icons/chevron-left.svg"
                    tooltip: qsTr("Hide the workspace tree")
                    onClicked: root.collapseRequested()
                }
            }

            // 文件树消费扁平投影（FileTreeFlatModel，经 tools.model
            // 暴露）：Qt 6.3 的 TreeView 在 Qt 5.15 不存在，两个版本
            // 统一用 ListView + depth/expanded role 渲染同一份数据。
            // 交互级冒烟（tst_toolsui）经 objectName 定位本树。
            ListView {
                id: fileTree
                objectName: "fileTree"

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

                // DropArea 据此识别拖拽来源（见草稿区的 onDropped）。
                readonly property bool isFileReferenceDrag: true
                // 行被回收进池子到再次被复用之间为真。模型 reset
                // 期间 role 会短暂换值，动画要闭嘴。
                property bool rebinding: false
                // 页面引用显式传入：编译型 QML 的 delegate 信号
                // 处理器里解析文档根 id 不可靠（fileTab 同一条
                // 纪律），菜单动作经它调页面动作层。
                property var scope: root

                // role 一律经 required property 声明；index 同理：
                // Qt 5.15 里声明了任何 required property 后
                // contextObject 即被清空，裸 index 会抛
                // ReferenceError。
                required property string name
                required property string path
                required property string relativePath
                required property bool isDir
                required property string iconSource
                required property int depth
                required property bool expanded
                required property bool hasChildren
                required property int index

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

                // 双击文件 = 打开它（动作层 openFileFromTree；
                // 目录的双击被两次单击的展开/收起抵消）。
                TapHandler {
                    onDoubleTapped: {
                        if (!treeRow.isDir)
                            scope.fileActivated(treeRow.path)
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

                // 右键：预览、插引用、复制路径。条目对本行不适
                // 用时**禁用而不隐藏**（2026-10 用户报告：隐藏
                // 会让菜单顶部空出一块，且用户无从得知「这个
                // 动作存在但此处不可用」）。可用性交给
                // scope.canPreview()——哪些后缀归
                // 预览管是纯层的事实，QML 不写后缀判断。
                AMenu {
                    id: pathMenu
                    // tst_toolsui 的锚点：右键弹出后按名字找到这份
                    // 菜单，再断言「Preview Markdown」条目的可用态。
                    objectName: "treeRowMenu"

                    AMenuItem {
                        enabled: scope.canPreview(treeRow.path)
                        icon.source: "qrc:/icons/file.svg"
                        text: qsTr("Preview Markdown")
                        onTriggered: scope.previewRequested(treeRow.path)
                    }
                    AMenuSeparator {}
                    // 双击改开文件后，插引用的指针入口（原双击
                    // 的行为挪到这里）。
                    AMenuItem {
                        icon.source: "qrc:/icons/paste.svg"
                        text: qsTr("Insert path at cursor")
                        onTriggered: scope.referenceRequested(
                            tools.fileReference(
                                treeRow.relativePath))
                    }
                    AMenuItem {
                        icon.source: "qrc:/icons/copy.svg"
                        text: qsTr("Copy relative path")
                        onTriggered: scope.copyRequested(treeRow.relativePath)
                    }
                    AMenuItem {
                        icon.source: "qrc:/icons/copy.svg"
                        text: qsTr("Copy absolute path")
                        onTriggered: scope.copyRequested(treeRow.path)
                    }
                }

                // 展开箭头：收起朝右，展开转 90 度朝下。
                Image {
                    x: 4 + treeRow.depth * 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    source: "qrc:/icons/chevron-right.svg"
                    // sourceSize 与显示尺寸一致：SVG 不设时按
                    // 24px 自然尺寸栅格再缩小绘制，箭头会发糊。
                    sourceSize: Qt.size(14, 14)
                    visible: treeRow.hasChildren
                    rotation: treeRow.expanded ? 90 : 0

                    Behavior on rotation {
                        enabled: !treeRow.rebinding
                        NumberAnimation {
                            duration: theme.durationFast
                        }
                    }
                }

                // 图标由模型按文件名/后缀查表给出（见 FileIcons）。
                Image {
                    x: 4 + treeRow.depth * 16 + 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    source: treeRow.iconSource
                    sourceSize: Qt.size(16, 16)
                }

                Label {
                    x: 4 + treeRow.depth * 16 + 40
                    width: parent.width - x - 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: treeRow.name
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeCaption
                    elide: Text.ElideMiddle
                }
            }
        }

            // 树的空态与未选态跟在树后面（无工作区时占位）。
            Label {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                visible: tools.currentWorkspace.length > 0
                         && tools.model.visibleCount === 0
                Layout.margins: theme.spacingM
                text: qsTr("The workspace folder is empty or unavailable.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            AEmptyState {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                visible: tools.currentWorkspace.length === 0
                iconSource: "qrc:/icons/folder.svg"
                title: qsTr("No workspace selected")
                description: qsTr("Add a folder to browse its files and insert references into the prompt.")
                actionText: qsTr("Add Folder...")
                onActionClicked: root.addWorkspaceRequested()
            }
        // workspaceSection 的收尾（树与两个空态到此为止）。
        }
    }
}
