import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·Markdown 分区：工作台编辑器的输入行为（A 批「输入手感」的
// 可配置项）。读经 workbench 的 markdownEditor* 属性（任一键变化都会
// 重发绑定），写经 setMarkdownEditor*（内部顺手持久化）。
ScrollView {
    id: page

    contentWidth: availableWidth
    ScrollBar.vertical: AScrollBar {}

    ColumnLayout {
        width: page.availableWidth
        spacing: theme.spacingM

        PageHeader {
            title: qsTr("Markdown")
            subtitle: qsTr("Typing behavior of the Markdown workbench editor")
        }

        // --- 缩进（Tab / Shift+Tab） --------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Indent with spaces")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            Switch {
                checked: workbench.markdownEditorIndentWithSpaces
                onToggled:
                    workbench.setMarkdownEditorIndentWithSpaces(checked)
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            text: qsTr("Tab inserts spaces instead of a tab character. Markdown renderers treat a tab as code indentation, so spaces are the portable way to nest lists.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Spaces per indent")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            AComboBox {
                textRole: "text"
                valueRole: "value"
                model: [{ text: qsTr("2 spaces"), value: 2 },
                        { text: qsTr("4 spaces"), value: 4 },
                        { text: qsTr("8 spaces"), value: 8 }]
                // 绑定而非 onCompleted：设置在别处改动时（页面常驻）下拉
                // 也要跟上。
                currentIndex: indexOfValue(
                                  workbench.markdownEditorIndentWidth)
                enabled: workbench.markdownEditorIndentWithSpaces
                onActivated:
                    workbench.setMarkdownEditorIndentWidth(currentValue)
            }
        }

        // --- 智能回车 -------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Continue lists on Enter")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            Switch {
                checked: workbench.markdownEditorSmartEnter
                onToggled: workbench.setMarkdownEditorSmartEnter(checked)
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            text: qsTr("Pressing Enter on a list, numbered, quote or task item repeats its marker; on an empty item it leaves the list.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }

        // --- 格式化快捷键 ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Formatting shortcuts")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            Switch {
                checked: workbench.markdownEditorFormatShortcuts
                onToggled:
                    workbench.setMarkdownEditorFormatShortcuts(checked)
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            text: qsTr("While the editor is focused: Ctrl+B bold, Ctrl+I italic, Ctrl+K link, Ctrl+E inline code. Elsewhere Ctrl+B still collapses the sidebar.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }

        // --- 自动配对 -------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Auto-pair markdown marks")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            Switch {
                checked: workbench.markdownEditorAutoPair
                onToggled: workbench.setMarkdownEditorAutoPair(checked)
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            text: qsTr("Typing * ` ~ [ ( inserts the closing mark and puts the cursor in between; typing the same mark again skips over the closer.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }

        // --- 图片目录 -------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Image assets folder")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            ATextField {
                Layout.preferredWidth: 260
                text: workbench.markdownEditorImageAssetsDir
                onEditingFinished: {
                    if (!workbench.setMarkdownEditorImageAssetsDir(text)) {
                        workbench.notify("warning",
                                         qsTr("Invalid image folder"),
                                         qsTr("Use a relative path inside the workspace, such as docs/assets/pic."))
                    }
                    // 非法值不落盘：回显当前生效值（规范化后的旧值）。
                    text = workbench.markdownEditorImageAssetsDir
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            text: qsTr("Where pasted images are saved, relative to the workspace root (or the document folder when no workspace is open). Multi-level paths such as docs/assets/pic are allowed. Images outside the document folder are inserted with .. paths and are not shown in the built-in preview.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }
    }
}
