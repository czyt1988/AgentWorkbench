import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// launcher 主页：带页面动作的 header、搜索 + 显示过滤，以及卡片网格。
// 卡片交互沿用 0.3.0 的行为（AgentCard.qml）。
Item {
    id: page

    // 搜索框文本（空 = 不过滤）。
    property string filterText: ""
    // 显示过滤：0 = 全部，1 = 运行中，2 = 未安装。
    property int displayFilter: 0
    // 当前过滤后可见的卡片数（由 recountShown 维护）。
    property int shownCount: 0

    // shownCount 从隐藏计数 delegate 的 `matches` 属性汇总而来，绝不从
    // 卡片可见性读：Item.visible 读回的是*有效*可见性，ScrollView 内创建
    // 的卡片（shownCount === 0 时整块隐藏）永远读不到 visible=true——
    // 那样计数会死锁在 0，「无匹配 launcher」空状态就钉死在配置完整的
    // 模型上面。
    function recountShown() {
        let n = 0
        for (let i = 0; i < counterBox.children.length; ++i) {
            if (counterBox.children[i].matches === true)
                ++n
        }
        page.shownCount = n
    }

    // 单个模型条目是否命中当前搜索 + 显示过滤。
    function matchesFilter(m) {
        if (displayFilter === 1 && !m.running)
            return false
        if (displayFilter === 2 && m.installed)
            return false
        if (filterText.length === 0)
            return true
        const needle = filterText.toLowerCase()
        return m.name.toLowerCase().includes(needle)
               || m.command.toLowerCase().includes(needle)
               || m.webUrl.toLowerCase().includes(needle)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            title: qsTr("Agent Launcher")
            subtitle: qsTr("Launch AI coding agents and open their web UI")

            AButton {
                variant: "primary"
                text: qsTr("Add Launcher")
                onClicked: editDialog.openFor("")
            }
            AButton {
                text: qsTr("Restore Defaults")
                onClicked: {
                    if (!agents.restoreDefaults())
                        errorPopup.open()
                }
            }
        }

        // --- 搜索 + 显示过滤 -----------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingM
            spacing: theme.spacingM

            ASearchField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search launchers...")
                onTextChanged: page.filterText = text
            }

            Row {
                spacing: theme.spacingXs

                AButton {
                    text: qsTr("All")
                    variant: page.displayFilter === 0 ? "primary" : "ghost"
                    onClicked: page.displayFilter = 0
                }
                AButton {
                    text: qsTr("Running")
                    variant: page.displayFilter === 1 ? "primary" : "ghost"
                    onClicked: page.displayFilter = 1
                }
                AButton {
                    text: qsTr("Not installed")
                    variant: page.displayFilter === 2 ? "primary" : "ghost"
                    onClicked: page.displayFilter = 2
                }
            }
        }

        // --- 空状态 ---------------------------------------------------------
        // 隐藏计数器：launcher 总数与逐条匹配状态。`matches` 镜像卡片
        // visible 绑定算出的值，但放在永远隐藏的 delegate 上，这样
        // recountShown() 的自举不依赖 ScrollView 的可见性。
        Item {
            id: counterBox
            visible: false
            Repeater {
                id: totalRepeater
                model: agents.model
                delegate: Item {
                    width: 0
                    height: 0
                    readonly property bool matches: page.matchesFilter(model)
                    onMatchesChanged: Qt.callLater(page.recountShown)
                }
            }
        }

        AEmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: totalRepeater.count === 0
            iconSource: "qrc:/icons/terminal.svg"
            title: qsTr("No launchers configured yet")
            description: qsTr("Add your first AI coding agent, or restore the built-in defaults.")
            actionText: qsTr("Add Launcher")
            onActionClicked: editDialog.openFor("")
        }

        AEmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: totalRepeater.count > 0 && page.shownCount === 0
            iconSource: "qrc:/icons/search.svg"
            title: qsTr("No matching launchers")
            description: qsTr("No launcher matches the current search or filter.")
            actionText: qsTr("Clear filters")
            onActionClicked: {
                searchField.text = ""
                page.displayFilter = 0
            }
        }

        // --- 卡片网格 --------------------------------------------------------
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            // 与搜索行同侧边距，第一张卡片不会贴死在侧栏边。
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            // 只在有内容可显示时出现：零卡片时它（fillHeight）和空状态
            // （fillHeight）会争抢同一列的高度。
            visible: page.shownCount > 0
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: AScrollBar {}

            Flow {
                id: flow
                width: scrollView.availableWidth
                spacing: theme.spacingL
                onChildrenChanged: Qt.callLater(page.recountShown)

                Repeater {
                    model: agents.model
                    delegate: AgentCard {
                        width: theme.cardMinWidth
                        visible: page.matchesFilter(model)
                        onConfigureRequested: function(id) {
                            editDialog.openFor(id)
                        }
                    }
                }
            }
        }
    }

    AgentEditDialog {
        id: editDialog
    }

    // 启动/停止失败的居中错误展示；命中的卡片同时闪红就地反馈。
    AAlertDialog {
        id: errorPopup
        width: 500
        titleText: qsTr("Launch failed")
        property string message: ""
        detail: errorPopup.message
        dismissText: qsTr("OK")
    }

    Connections {
        target: agents
        function onLaunchFailed(id, message) {
            errorPopup.message = message
            errorPopup.open()
        }
        function onInstallFinished(id, success, message) {
            if (!success) {
                errorPopup.message = message
                errorPopup.open()
            }
        }
    }

    Component.onCompleted: page.recountShown()
}
