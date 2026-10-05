import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 「Markdown」启动页：侧栏条目背后的薄壳。编辑器本体在工作台窗口里，
// 这里只回答三件事——窗口怎么开、草稿从哪续、最近在看哪几份文档。窗口
// 跨页常驻：开着之后切到任何 agent 页粘贴都不会丢撤销栈与滚动位置。
Item {
    id: page

    // 最近文档（MRU 新在前）：取前 6 条展示；点击 = 开窗并打开该 tab。
    readonly property var recentDocuments: {
        const all = workbench.recentMarkdownDocuments()
        const top = []
        for (let i = 0; i < all.length && i < 6; ++i)
            top.push(all[i])
        return top
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            title: qsTr("Markdown")
            subtitle: qsTr("Prompts and documents in one workbench window")
        }

        // 主按钮 + 空态引导：窗口没开时邀请打开，开着时按钮同样是
        // 「打开工作台」（已开 = 抬到前台）。
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 210
            Layout.margins: theme.spacingL

            AEmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - theme.spacingL * 2, 520)
                iconSource: "qrc:/icons/tools.svg"
                title: qsTr("The Markdown workbench")
                description: qsTr("Compose prompts with the workspace tree at hand, or open a Markdown document in the same window. Nothing is ever sent from the window: copying is the only way out.")
                actionText: qsTr("Open the workbench")
                onActionClicked: workbench.openMarkdownWorkbench()
            }
        }

        // 最近文档：有记录才有这一段。
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingL
            visible: page.recentDocuments.length > 0
            spacing: theme.spacingXs

            Label {
                text: qsTr("Recent documents")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                font.bold: true
            }

            Repeater {
                model: page.recentDocuments

                // 行内容注入 AListRow 的插槽（AListRow 没有标题属性，
                // 只有 contentData）。文件名在前、目录尾段辨认用的在后：
                // 整条绝对路径在 44 px 的行里读不完。
                delegate: AListRow {
                    id: recentRow
                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    rowHeight: 44

                    Image {
                        source: "qrc:/icons/filetypes/markdown.svg"
                        sourceSize: Qt.size(16, 16)
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Label {
                        Layout.fillWidth: true
                        text: {
                            const slash = recentRow.modelData.lastIndexOf("/")
                            const name = slash >= 0
                                    ? recentRow.modelData.substring(slash + 1)
                                    : recentRow.modelData
                            const dirSlash =
                                    recentRow.modelData.lastIndexOf("/",
                                                                    slash - 1)
                            const dir = dirSlash >= 0
                                    ? recentRow.modelData.substring(
                                          dirSlash + 1, slash)
                                    : ""
                            return dir.length > 0
                                    ? name + " — " + dir : name
                        }
                        color: theme.textPrimary
                        font.pixelSize: theme.fontSizeBody
                        elide: Text.ElideMiddle
                    }

                    TapHandler {
                        onSingleTapped:
                            workbench.previewMarkdown(recentRow.modelData)
                    }
                }
            }
        }
    }
}
