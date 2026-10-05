import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 工作台窗口命令条的降级形态：一条普通工具栏。AWB_ENABLE_RIBBON 关闭的
// 构建里 WorkbenchRibbon.qml 根本不进资源清单，窗口的 Loader 装载它失败
// 后换装载本文件；单测引擎（没注册 SARibbon 类型）同样走到这里。
//
// 与 WorkbenchRibbon.qml 是同一批动作的两份皮：都只把点击转发给
// win.*（窗口的动作层），不许在这里长逻辑——两处各写一份行为的漂移
// 比少一个按钮更糟。按钮集对齐右键菜单（MarkdownContextMenu）与
// Ribbon「开始 / 视图」两页的现有动作。
Rectangle {
    id: commandToolbar

    /// 宿主窗口（MarkdownWorkbenchWindow）：动作与状态的唯一来源。
    property var win: null

    implicitHeight: 40
    color: theme.chromeBg

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.spacingS
        anchors.rightMargin: theme.spacingS
        spacing: theme.spacingXs

        AIconButton {
            iconSource: "qrc:/icons/bold.svg"
            tooltip: qsTr("Bold")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("bold")
        }
        AIconButton {
            iconSource: "qrc:/icons/code.svg"
            tooltip: qsTr("Code")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("code")
        }
        AIconButton {
            iconSource: "qrc:/icons/bullet-list.svg"
            tooltip: qsTr("Bulleted list")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("bullet")
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: theme.spacingXs
            Layout.rightMargin: theme.spacingXs
            implicitWidth: 1
            implicitHeight: 24
            color: theme.separator
        }

        AIconButton {
            iconSource: "qrc:/icons/undo.svg"
            tooltip: qsTr("Undo")
            enabled: win && win.activeEditor && win.activeEditor.canUndo
            onClicked: if (win) win.editorAction("undo")
        }
        AIconButton {
            iconSource: "qrc:/icons/redo.svg"
            tooltip: qsTr("Redo")
            enabled: win && win.activeEditor && win.activeEditor.canRedo
            onClicked: if (win) win.editorAction("redo")
        }
        AIconButton {
            iconSource: "qrc:/icons/cut.svg"
            tooltip: qsTr("Cut")
            enabled: win && win.activeEditor
                     && win.activeEditor.selectedText.length > 0
            onClicked: if (win) win.editorAction("cut")
        }
        AIconButton {
            iconSource: "qrc:/icons/copy.svg"
            tooltip: qsTr("Copy")
            enabled: win && win.activeEditor
                     && win.activeEditor.selectedText.length > 0
            onClicked: if (win) win.editorAction("copy")
        }
        AIconButton {
            iconSource: "qrc:/icons/paste.svg"
            tooltip: qsTr("Paste")
            enabled: win && win.activeEditor && win.activeEditor.canPaste
            onClicked: if (win) win.editorAction("paste")
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: theme.spacingXs
            Layout.rightMargin: theme.spacingXs
            implicitWidth: 1
            implicitHeight: 24
            color: theme.separator
        }

        // 主交付动作：整份草稿进剪贴板。
        AButton {
            text: qsTr("Copy Prompt")
            variant: "primary"
            onClicked: if (win) win.copyPrompt()
        }

        // 模板与收藏（M2；与 Ribbon 提示词页同一批 win.* 入口）。
        AIconButton {
            objectName: "insertTemplateButton"
            iconSource: "qrc:/icons/file.svg"
            tooltip: qsTr("Insert a template")
            enabled: win && markdown.draftActive
            onClicked: if (win) win.openTemplatePicker()
        }
        AIconButton {
            objectName: "saveTemplateButton"
            iconSource: "qrc:/icons/code.svg"
            tooltip: qsTr("Save the draft as a template")
            enabled: win && markdown.draftActive
                     && tools.draft.length > 0
            onClicked: if (win) win.saveCurrentAsTemplate()
        }
        AIconButton {
            objectName: "saveFavoriteButton"
            iconSource: "qrc:/icons/copy.svg"
            tooltip: qsTr("Save the draft to favorites")
            enabled: win && markdown.draftActive
                     && tools.draft.length > 0
            onClicked: if (win) win.saveCurrentToFavorites()
        }

        AIconButton {
            iconSource: "qrc:/icons/search.svg"
            tooltip: qsTr("Search in document")
            enabled: win && win.preset === "document"
            onClicked: if (win) win.openSearch()
        }
        AIconButton {
            iconSource: "qrc:/icons/download.svg"
            tooltip: qsTr("Save")
            enabled: win && win.preset === "document" && markdown.canEdit
            onClicked: if (win) win.saveDocument()
        }

        Item {
            Layout.fillWidth: true
        }

        // 三态与面板开关（文档模式才有意义）。
        AButton {
            visible: win && win.preset === "document"
            text: qsTr("Preview")
            Layout.preferredWidth: 72
            variant: markdown.mode === "preview" ? "primary" : "ghost"
            onClicked: markdown.setMode("preview")
        }
        AButton {
            visible: win && win.preset === "document"
            text: qsTr("Split")
            Layout.preferredWidth: 64
            variant: markdown.mode === "split" ? "primary" : "ghost"
            enabled: markdown.splitAvailable
            onClicked: markdown.setMode("split")
        }
        AButton {
            visible: win && win.preset === "document"
            text: qsTr("Source")
            Layout.preferredWidth: 72
            variant: markdown.mode === "source" ? "primary" : "ghost"
            enabled: markdown.splitAvailable
            onClicked: markdown.setMode("source")
        }

        AIconButton {
            iconSource: "qrc:/icons/bullet-list.svg"
            tooltip: qsTr("Show outline")
            active: markdown.showOutline
            visible: win && win.preset === "document"
            onClicked: markdown.setShowOutline(!markdown.showOutline)
        }

        // 「完整工具」即预设切换（与 Ribbon 视图页同一开关）。
        AButton {
            text: qsTr("Full Tools")
            variant: win && win.preset === "prompt" ? "primary" : "ghost"
            enabled: win && (win.preset === "prompt"
                             || (markdown.available
                                 && markdown.fileTitle.length > 0))
            onClicked: if (win) win.toggleFullTools()
        }
    }
}
