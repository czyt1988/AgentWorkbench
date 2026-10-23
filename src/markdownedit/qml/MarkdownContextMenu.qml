import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// Agent Tools 编辑区的右键菜单：Office 式异型菜单——顶部一条 markdown
// 格式化工具栏（加粗 / 代码 / 要点，图标左对齐、无独立背景，与条目
// 融成一个窗口菜单），下方是撤销/重做/剪切/复制/粘贴条目。菜单本体
// 走 AMenu（玻璃质感、全仓统一形态，见 designs.md）。Menu 的
// contentItem（ListView）按声明顺序竖排直接子项，混排普通 Item 与
// AMenuItem 即得到「工具栏在上、条目在下」的形态。
AMenu {
    id: root

    /// 操作的目标编辑器（ToolsPage 的提示词编辑区）。
    property var editor: null

    /// 粘贴请求：纯图片剪贴板时编辑器自己的 paste() 不可用，由宿主页面
    /// 决定走图片导入还是原生粘贴，菜单不直接调编辑器。
    signal pasteRequested()

    width: 220

    // --- 顶部工具栏：markdown 格式化 -------------------------------------
    // 与菜单条目融合：无背景色、图标左对齐（不居中），像窗口菜单的
    // 工具区。下缘一条极淡分隔线与条目区分。
    Item {
        height: 34

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.bottom: parent.bottom
            implicitHeight: 1
            color: theme.separator
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: theme.spacingXs

            AIconButton {
                iconSource: "qrc:/icons/bold.svg"
                tooltip: qsTr("Bold")
                onClicked: root.run(function() {
                    MarkdownEdit.applyBold(root.editor)
                })
            }

            AIconButton {
                iconSource: "qrc:/icons/code.svg"
                tooltip: qsTr("Code")
                onClicked: root.run(function() {
                    MarkdownEdit.applyCode(root.editor)
                })
            }

            AIconButton {
                iconSource: "qrc:/icons/bullet-list.svg"
                tooltip: qsTr("Bulleted list")
                onClicked: root.run(function() {
                    MarkdownEdit.applyBullet(root.editor)
                })
            }
        }
    }

    AMenuItem {
        icon.source: "qrc:/icons/undo.svg"
        text: qsTr("Undo")
        enabled: root.editor && root.editor.canUndo
        onTriggered: root.run(function() {
            root.editor.undo()
        })
    }
    AMenuItem {
        icon.source: "qrc:/icons/redo.svg"
        text: qsTr("Redo")
        enabled: root.editor && root.editor.canRedo
        onTriggered: root.run(function() {
            root.editor.redo()
        })
    }

    AMenuSeparator {}

    AMenuItem {
        icon.source: "qrc:/icons/cut.svg"
        text: qsTr("Cut")
        enabled: root.editor && root.editor.selectedText.length > 0
        onTriggered: root.run(function() {
            root.editor.cut()
        })
    }
    AMenuItem {
        icon.source: "qrc:/icons/copy.svg"
        text: qsTr("Copy")
        enabled: root.editor && root.editor.selectedText.length > 0
        onTriggered: root.run(function() {
            root.editor.copy()
        })
    }
    AMenuItem {
        icon.source: "qrc:/icons/paste.svg"
        text: qsTr("Paste")
        // 纯图片剪贴板时 canPaste 为假，不能拿它门控；真正的分派在宿主
        // 页面（图片导入失败才回落到编辑器原生粘贴）。
        enabled: root.editor !== null
        onTriggered: root.run(function() {
            root.pasteRequested()
        })
    }

    AMenuSeparator {}

    // 行级操作（B 批次）：目标是光标/选区覆盖到的行。动作直接走
    // MarkdownEdit 纯层，与编辑器组合键同一批实现。
    AMenuItem {
        icon.source: "qrc:/icons/arrow-up.svg"
        text: qsTr("Move line up")
        enabled: root.editor && !root.editor.readOnly
        onTriggered: root.run(function() {
            MarkdownEdit.moveLineUp(root.editor)
        })
    }
    AMenuItem {
        icon.source: "qrc:/icons/arrow-down.svg"
        text: qsTr("Move line down")
        enabled: root.editor && !root.editor.readOnly
        onTriggered: root.run(function() {
            MarkdownEdit.moveLineDown(root.editor)
        })
    }
    AMenuItem {
        icon.source: "qrc:/icons/copy.svg"
        text: qsTr("Duplicate line")
        enabled: root.editor && !root.editor.readOnly
        onTriggered: root.run(function() {
            MarkdownEdit.duplicateLine(root.editor)
        })
    }
    AMenuItem {
        icon.source: "qrc:/icons/close.svg"
        text: qsTr("Delete line")
        enabled: root.editor && !root.editor.readOnly
        onTriggered: root.run(function() {
            MarkdownEdit.deleteLine(root.editor)
        })
    }
    AMenuItem {
        icon.source: "qrc:/icons/code.svg"
        text: qsTr("Select line")
        onTriggered: root.run(function() {
            MarkdownEdit.selectLine(root.editor)
        })
    }

    AMenuSeparator {}

    // 大小写转换（作用于选区；无选区时光标处的词）。
    AMenuItem {
        text: qsTr("UPPERCASE")
        enabled: root.editor && !root.editor.readOnly
        onTriggered: root.run(function() {
            MarkdownEdit.convertCase(root.editor,
                                     MarkdownEdit.UpperCase)
        })
    }
    AMenuItem {
        text: qsTr("lowercase")
        enabled: root.editor && !root.editor.readOnly
        onTriggered: root.run(function() {
            MarkdownEdit.convertCase(root.editor,
                                     MarkdownEdit.LowerCase)
        })
    }
    AMenuItem {
        text: qsTr("Title Case")
        enabled: root.editor && !root.editor.readOnly
        onTriggered: root.run(function() {
            MarkdownEdit.convertCase(root.editor,
                                     MarkdownEdit.TitleCase)
        })
    }

    /// 关菜单、把焦点还给编辑器，再执行动作。菜单打开时焦点在菜单上，
    /// 编辑器拿回焦点后，随后的输入与选区写入才落在它身上。
    function run(action) {
        close()
        if (editor)
            editor.forceActiveFocus()
        action()
    }
}
