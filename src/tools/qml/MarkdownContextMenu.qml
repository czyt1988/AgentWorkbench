import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// Agent Tools 编辑区的右键菜单：模仿 Office 的异型菜单——顶部一条
// markdown 格式化工具栏（加粗 / 代码 / 要点），下方是常规的复制 /
// 粘贴条目。Menu 的 contentItem（ListView）按声明顺序竖排直接子项，
// 混排普通 Item 与 MenuItem 即得到「工具栏在上、条目在下」的形态；
// 未显式设宽的子项会被 Menu 拉到菜单宽，工具栏只声明高度。
Menu {
    id: root

    /// 操作的目标编辑器（ToolsPage 的提示词编辑区）。
    property var editor: null

    width: 210
    padding: theme.spacingS

    // MenuItem 的文字 / 图标 / 悬停高亮全走 palette 角色，不覆盖模板。
    palette.windowText: theme.textPrimary
    palette.highlight: theme.surfaceHoverBg
    palette.highlightedText: theme.textPrimary

    background: Rectangle {
        radius: theme.radiusOverlay
        color: theme.surfaceBg
        border.color: theme.borderSubtle
        border.width: 1
    }

    // --- 顶部工具栏：markdown 格式化 -------------------------------------
    Item {
        height: 36

        // 一整条内衬把工具栏与普通菜单条目区分开。
        Rectangle {
            anchors.fill: parent
            radius: theme.radiusControl
            color: theme.chromeBg
        }

        Row {
            anchors.centerIn: parent
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

    // 工具栏与菜单条目之间的呼吸。
    Item {
        height: theme.spacingXs
    }

    MenuItem {
        icon.width: 16
        icon.height: 16
        icon.source: "qrc:/icons/copy.svg"
        text: qsTr("Copy")
        enabled: root.editor && root.editor.selectedText.length > 0
        onTriggered: root.run(function() {
            root.editor.copy()
        })
    }

    MenuItem {
        icon.width: 16
        icon.height: 16
        icon.source: "qrc:/icons/paste.svg"
        text: qsTr("Paste")
        enabled: root.editor && root.editor.canPaste
        onTriggered: root.run(function() {
            root.editor.paste()
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
