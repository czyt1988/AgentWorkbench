import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 工作台页面命令条的降级形态：一条普通工具栏。AWB_ENABLE_RIBBON 关闭的
// 构建里 WorkbenchRibbon.qml 根本不进资源清单，页面的 Loader 装载它失败
// 后换装载本文件；单测引擎（没注册 SARibbon 类型）同样走到这里。
//
// 与 WorkbenchRibbon.qml 是同一批动作的两份皮：都只把点击转发给
// win.*（窗口的动作层），不许在这里长逻辑——两处各写一份行为的漂移
// 比少一个按钮更糟。按钮集与 Ribbon 的三个标签页对齐：格式动作、模板
// 与收藏（菜单形态承载 gallery 的插入入口）、体检与历史（开页面的悬浮
// 窗）、查找（输入框常驻）、视图选项。
Rectangle {
    id: commandToolbar

    /// 宿主页面（MarkdownWorkbenchPage）：动作与状态的唯一来源。
    property var win: null

    // 工具栏形态没有折叠预览（Ribbon 形态才有）；保留同签名，页面统一绑定。
    readonly property bool categoryPreviewUp: false

    implicitHeight: 40
    color: theme.chromeBg

    // Ctrl+F 的落点：焦点进查找框并全选。
    function focusFind() {
        toolbarFindField.forceActiveFocus()
        toolbarFindField.selectAll()
    }

    // 工具栏形态没有标签页；跨页跳转的转发入口保持两份皮同签名（页面
    // 的 setRibbonTab 经命令条调用，Ribbon 形态才有实际切换）。
    function setTabIndex(index) {
        // 无事可做：保留签名对齐（见上）。
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.spacingS
        anchors.rightMargin: theme.spacingS
        spacing: theme.spacingXs

        // --- 格式动作（icon-only，与 Ribbon「开始」页同集） -----------
        AIconButton {
            iconSource: "qrc:/icons/bold.svg"
            tooltip: qsTr("Bold")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("bold")
        }
        AIconButton {
            iconSource: "qrc:/icons/bold.svg"
            tooltip: qsTr("Italic")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("italic")
        }
        AIconButton {
            iconSource: "qrc:/icons/bold.svg"
            tooltip: qsTr("Underline")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("underline")
        }
        AIconButton {
            iconSource: "qrc:/icons/bold.svg"
            tooltip: qsTr("Strikethrough")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("strike")
        }
        AIconButton {
            iconSource: "qrc:/icons/code.svg"
            tooltip: qsTr("Inline code")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("code")
        }
        AIconButton {
            objectName: "numberedButton"
            iconSource: "qrc:/icons/bullet-list.svg"
            tooltip: qsTr("Numbered list")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("numbered")
        }
        AIconButton {
            iconSource: "qrc:/icons/bullet-list.svg"
            tooltip: qsTr("Task list")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("task")
        }
        AIconButton {
            iconSource: "qrc:/icons/code.svg"
            tooltip: qsTr("Block quote")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("quote")
        }
        AIconButton {
            iconSource: "qrc:/icons/code.svg"
            tooltip: qsTr("Table")
            enabled: win && win.activeEditor
            onClicked: if (win) win.applyFormat("table")
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: theme.spacingXs
            Layout.rightMargin: theme.spacingXs
            implicitWidth: 1
            implicitHeight: 24
            color: theme.separator
        }

        // --- 主交付与剪贴板 ---------------------------------------------
        AButton {
            text: qsTr("Copy Prompt")
            variant: "primary"
            onClicked: if (win) win.copyPrompt()
        }

        AIconButton {
            iconSource: "qrc:/icons/paste.svg"
            tooltip: qsTr("Paste")
            enabled: win && win.activeEditor && win.activeEditor.canPaste
            onClicked: if (win) win.editorAction("paste")
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

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: theme.spacingXs
            Layout.rightMargin: theme.spacingXs
            implicitWidth: 1
            implicitHeight: 24
            color: theme.separator
        }

        // --- 模板与收藏（菜单承载 gallery 的插入入口） -------------------
        AIconButton {
            objectName: "insertTemplateButton"
            iconSource: "qrc:/icons/file.svg"
            tooltip: qsTr("Insert a template")
            onClicked: templateMenu.popup()
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

        // --- 体检与历史：开页面的悬浮窗 ----------------------------------
        AIconButton {
            objectName: "checkPromptToolbarButton"
            iconSource: "qrc:/icons/search.svg"
            tooltip: qsTr("Check the prompt")
            active: win && win.checksVisible
            enabled: win
            onClicked: if (win) win.toggleChecksWindow()
        }
        AIconButton {
            objectName: "historyToolbarButton"
            iconSource: "qrc:/icons/refresh.svg"
            tooltip: qsTr("Draft history")
            active: win && win.historyVisible
            enabled: win
            onClicked: if (win) win.toggleHistoryWindow()
        }

        // --- 查找：输入框常驻（回车查找/再回车下一个） -------------------
        ATextField {
            id: toolbarFindField
            objectName: "toolbarFindField"

            Layout.preferredWidth: 150
            placeholderText: qsTr("Find")
            onAccepted: if (win) win.findNext(text)
        }

        Item {
            Layout.fillWidth: true
        }

        // --- 视图选项（与 Ribbon「视图」页同一语义） ---------------------
        AButton {
            visible: markdown.available
            text: qsTr("Preview")
            Layout.preferredWidth: 72
            variant: markdown.mode === "preview" ? "primary" : "ghost"
            onClicked: markdown.setMode("preview")
        }
        AButton {
            visible: markdown.available
            text: qsTr("Split")
            Layout.preferredWidth: 64
            variant: markdown.mode === "split" ? "primary" : "ghost"
            enabled: markdown.splitAvailable
            onClicked: markdown.setMode("split")
        }
        AButton {
            visible: markdown.available
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
            visible: markdown.available
            onClicked: markdown.setShowOutline(!markdown.showOutline)
        }
    }

    // 模板插入菜单（gallery 的降级形态：菜单条目 = 模板表）。
    AMenu {
        id: templateMenu

        AMenuItem {
            icon.source: "qrc:/icons/code.svg"
            text: qsTr("Save the draft as a template")
            enabled: win && markdown.draftActive
                     && tools.draft.length > 0
            onTriggered: if (win) win.saveCurrentAsTemplate()
        }

        AMenuSeparator {
            visible: tools.templates.length > 0
        }

        // 动态条目：Repeater 挂在 AMenu 的内容里按模板表生成。
        Repeater {
            model: tools.templates

            delegate: AMenuItem {
                required property var modelData

                text: modelData.title
                onTriggered: if (win) win.insertTemplate(modelData.id)
            }
        }
    }
}
