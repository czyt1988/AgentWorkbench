import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 「Markdown」启动页：侧栏条目背后的薄壳。编辑器本体在工作台窗口里，
// 这里只回答两件事——窗口怎么开、草稿从哪续。窗口跨页常驻：开着之后
// 切到任何 agent 页粘贴都不会丢撤销栈与滚动位置，回来时仍在。
Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            title: qsTr("Markdown")
            subtitle: qsTr("Prompts and documents in one workbench window")
        }

        // 主按钮 + 空态引导：窗口没开时邀请打开，开着时按钮同样是
        // 「打开工作台」（已开 = 抬到前台）。M0 的启动页就这两个动作；
        // 最近草稿/最近文档列表随会话记忆一起落地。
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
    }
}
