import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Web 页：自己的标签栏充当页面头部，主体为每个标签托管一个 surface
// （WebEngineSurface），空状态列出运行中的 agent 供一键打开。本页经
// PageDescriptor::keepAlive 常驻，页面状态不随切换销毁。
Item {
    id: page

    // 全屏时隐藏标签栏；Esc 先退出全屏。
    property bool chromeHidden: false
    // 本页经 PageDescriptor::keepAlive 常驻（Workspace 只隐藏不销毁），
    // 因此切到别的页面后这里的 ApplicationShortcut 仍然存活。所有全局
    // 快捷键必须门控在「本页是当前页」上，否则 Ctrl+W / F5 / F12 会在
    // 设置页之类的地方关掉/重载隐藏的 Web 标签。
    readonly property bool pageCurrent: nav.currentPageId === "web"
    // 首页视图：按需把 agent 列表（无标签的那页）盖在已开标签上面，
    // 不关闭任何标签——视图开着时回到启动卡片的路。离开它（点标签、
    // 打开 agent、轮换标签）即恢复活动视图。
    property bool homeActive: false
    // 按 tab id 索引的已装载 surface——工具栏经它访问活动标签的
    // surface（devtools 挂在 surface 上）。
    property var surfaceItems: ({})
    // 运行中的 agent 数（运行列表的可见行数，recountRunning 维护）。
    property int runningCount: 0

    // 重数运行列表的可见行：行随模型增删/启停变化后回填 runningCount。
    function recountRunning() {
        let n = 0
        for (let i = 0; i < runningList.children.length; ++i) {
            const child = runningList.children[i]
            if (child.visible && child.height > 0)
                ++n
        }
        page.runningCount = n
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- 标签栏（即页面头部）----------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            visible: !page.chromeHidden
            // 尺寸经 implicitHeight 提供：ColumnLayout 重排时按子项的
            // implicitHeight 决定高度，直接绑定 height 会被覆盖。本页经
            // PageDescriptor::keepAlive 常驻，以 0x0 创建、变可见后才拿到
            // 真实尺寸，必然触发一次重排——tab bar 曾因此被踩成 0 高而
            // 整条消失（chromeHidden 时 visible 为 false，布局会跳过本
            // 项，效果与旧 height 归 0 一致）。
            implicitHeight: theme.tabBarHeight
            color: theme.chromeBg

            // 底部分割线（下边框 theme.separator）。
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: theme.separator
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: tabsRow.implicitWidth
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: tabsRow
                        height: parent.height

                        Repeater {
                            model: web.model
                            delegate: Item {
                                id: tabButton

                                required property string tabId
                                required property string title
                                required property string url
                                required property string state
                                // required property 按「属性名 = role 名」
                                // 匹配，所以这里必须叫 `color`
                                // （WebTabsModel::ColorRole）。delegate
                                // 根也因此必须是 Item 而不是 Rectangle：
                                // 在 Rectangle 上这个属性会遮蔽视觉 color，
                                // 下面的主题绑定落到字符串上，标签体就
                                // 永远画成默认白色（与 AgentCard 的
                                // 根 Item + 内层 Rectangle 同一模式）。
                                required property string color
                                required property string iconSource
                                required property int loadProgress
                                required property string surfaceKind
                                property bool active: web.activeTabId === tabId

                                width: Math.min(Math.max(
                                        tabLabel.implicitWidth
                                        + theme.spacingL * 2
                                        + (closeArea.visible ? 20 : 0),
                                        96), 200)
                                height: theme.tabBarHeight

                                Rectangle {
                                    id: tabBackground
                                    anchors.fill: parent
                                    color: tabButton.active ? theme.tabActiveBg
                                                  : (tabMouse.containsMouse
                                                     ? theme.surfaceHoverBg
                                                     : theme.tabInactiveBg)
                                }

                                // 活动标签：顶部的 2px accent 指示条。
                                Rectangle {
                                    visible: tabButton.active
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 2
                                    color: theme.accent
                                }

                                // 声明在内容行之前：关闭 ×（行内）必须压在
                                // 这个整块 MouseArea 之上，否则点它只会
                                // 激活标签。
                                MouseArea {
                                    id: tabMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // 中键必须 ACCEPT，否则按钮收不到
                                    // 后续处理器。
                                    acceptedButtons: Qt.LeftButton
                                                     | Qt.MiddleButton
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.MiddleButton)
                                            web.closeTab(tabButton.tabId)
                                        else {
                                            page.homeActive = false
                                            web.activateTab(tabButton.tabId)
                                        }
                                    }
                                    onDoubleClicked: function(mouse) {
                                        if (mouse.button === Qt.LeftButton)
                                            web.reloadTab(tabButton.tabId)
                                    }
                                }

                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: theme.spacingS
                                    anchors.rightMargin: theme.spacingS
                                    spacing: theme.spacingXs

                                    // 状态点（永远不只靠颜色：标签的
                                    // tooltip 承载文字状态）。
                                    AStatusDot {
                                        diameter: 8
                                        on: state !== "offline"
                                        onColor: {
                                            if (state === "crashed"
                                                || state === "error")
                                                return theme.danger
                                            return tabButton.color.length > 0
                                                   ? tabButton.color
                                                   : theme.accent
                                        }
                                    }

                                    // 标签图标 16px：agent 图标，
                                    // 回退 web.svg。
                                    Image {
                                        width: 16
                                        height: 16
                                        source: tabButton.iconSource.length > 0
                                                ? tabButton.iconSource
                                                : "qrc:/icons/web.svg"
                                        sourceSize: Qt.size(16, 16)
                                        fillMode: Image.PreserveAspectFit
                                    }

                                    Label {
                                        id: tabLabel
                                        Layout.fillWidth: true
                                        text: title.length > 0 ? title
                                             : String(url).replace(/^https?:\/\//, "").replace(/[?#].*$/, "")
                                        color: active ? theme.textPrimary
                                                      : theme.textMuted
                                        font.pixelSize: theme.fontSizeSmall
                                        elide: Text.ElideMiddle
                                    }

                                    // 加载中的忙碌指示。
                                    BusyIndicator {
                                        visible: state === "loading"
                                        running: visible
                                        implicitWidth: 12
                                        implicitHeight: 12
                                    }

                                    Item {
                                        id: closeArea
                                        width: visible ? 16 : 0
                                        height: 16
                                        visible: tabButton.active
                                                 || tabMouse.containsMouse

                                        Image {
                                            anchors.centerIn: parent
                                            source: "qrc:/icons/close.svg"
                                            sourceSize: Qt.size(10, 10)
                                            fillMode: Image.PreserveAspectFit
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: web.closeTab(tabButton.tabId)
                                        }
                                    }
                                }

                                // 栏底的加载进度线。
                                Rectangle {
                                    visible: state === "loading"
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    height: 2
                                    width: parent.width * loadProgress / 100
                                    color: theme.accent
                                }

                                ToolTip.visible: tabMouse.containsMouse
                                ToolTip.delay: 300
                                ToolTip.timeout: 10000
                                ToolTip.text: state === "offline"
                                              ? qsTr("This agent is not running")
                                              : state === "crashed"
                                                ? qsTr("The page crashed")
                                                : state === "error"
                                                  ? qsTr("Failed to load the page")
                                                  : title
                                // 共享 tooltip 比这个 delegate 长命：
                                // 标签行（及其悬停源）消失时要隐藏它，
                                // 否则它会一直留在屏幕上。
                                Component.onDestruction: ToolTip.hide()
                            }
                        }
                    }
                }

                // --- 工具栏（作用于活动标签）------------------------------
                Row {
                    spacing: theme.spacingXs
                    rightPadding: theme.spacingS

                    // 首页：配了 web.homeUrl 时打开它（重复按激活同一
                    // Home 标签），否则回到 agent 列表（无标签页）而不
                    // 关闭任何标签。配了 homeUrl 时随时可回（空态也开）。
                    AIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: "qrc:/icons/home.svg"
                        tooltip: web.homeUrl.length > 0
                                  ? qsTr("Home (configured start page)")
                                  : qsTr("Home")
                        enabled: web.tabCount > 0 || web.homeUrl.length > 0
                        onClicked: {
                            if (web.homeUrl.length > 0) {
                                page.homeActive = false
                                web.openHome()
                            } else {
                                page.homeActive = true
                            }
                        }
                    }

                    // ⟳ 重载 / ✕ 停止加载——按钮跟随活动标签的状态切换。
                    AIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: web.activeState === "loading"
                                    ? "qrc:/icons/close.svg"
                                    : "qrc:/icons/refresh.svg"
                        tooltip: web.activeState === "loading"
                                 ? qsTr("Stop loading") : qsTr("Reload")
                        enabled: web.activeTabId.length > 0
                        onClicked: {
                            if (web.activeState === "loading") {
                                const item = page.surfaceItems[web.activeTabId]
                                if (item)
                                    item.stopLoading()
                            } else {
                                web.reloadTab(web.activeTabId)
                            }
                        }
                    }
                    // 「在浏览器打开」任何时刻都可见——逃生口永远不能藏。
                    AIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: "qrc:/icons/external-link.svg"
                        tooltip: qsTr("Open in browser")
                        enabled: web.activeTabId.length > 0
                        onClicked: web.openExternal(web.activeTabId)
                    }
                    // Qt Quick Controls 2 没有 MenuButton 类型（那是
                    // Qt 5 Controls 1 的东西）——图标按钮弹菜单是受支持
                    // 的形态。
                    AIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: web.activeTabId.length > 0
                        iconSource: "qrc:/icons/menu.svg"
                        tooltip: qsTr("More actions")
                        onClicked: tabMenu.popup()
                    }

                    AMenu {
                        id: tabMenu
                            AMenuItem {
                                text: qsTr("Copy URL")
                                onTriggered: {
                                    const item = page.surfaceItems[web.activeTabId]
                                    if (item)
                                        workbench.copyText(String(item.tab.url))
                                }
                            }
                            AMenuSeparator {}
                            AMenuItem {
                                text: qsTr("Zoom in")
                                onTriggered: stepZoom(0.1)
                            }
                            AMenuItem {
                                text: qsTr("Zoom out")
                                onTriggered: stepZoom(-0.1)
                            }
                            AMenuItem {
                                text: qsTr("Reset zoom")
                                onTriggered: web.setTabZoom(web.activeTabId, 1.0)
                            }
                            AMenuSeparator {}
                            AMenuItem {
                                visible: web.devToolsEnabled
                                text: qsTr("Developer tools")
                                onTriggered: {
                                    const item = page.surfaceItems[web.activeTabId]
                                    if (item)
                                        item.openDevTools()
                                }
                            }
                            AMenuItem {
                                text: qsTr("Close tab")
                                onTriggered: web.closeTab(web.activeTabId)
                            }
                        }
                }
            }
        }

        // --- 主体：每个标签一个 surface ----------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 运行中 agent 的一键打开列表（空状态；homeActive 时也作为
            // 已开标签之上的首页视图——surface 把自身可见性绑到本项
            // 隐藏上）。
            AEmptyState {
                id: emptyState
                anchors.fill: parent
                // tabCount 带 NOTIFY；rowCount() 没有通知信号，绑定
                // 它在第一个标签之后就永不重算。
                visible: web.tabCount === 0 || page.homeActive
                iconSource: "qrc:/icons/web.svg"
                title: page.homeActive ? qsTr("Home")
                                       : qsTr("No Web views open")
                description: web.engineAvailable
                              ? qsTr("Open a view from a running agent's card, or from the list below.")
                              : qsTr("This build opens agent WebUIs in the system browser. Start an agent below to open it.")
                actionText: qsTr("Go to launcher")
                onActionClicked: workbench.showPage("agents")

                extra: [
                    // 高度封顶的滚动：长运行列表不会把动作按钮顶出页面。
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 320)
                        contentWidth: width
                        contentHeight: rowsColumn.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: rowsColumn
                            width: parent.width
                            spacing: theme.spacingS

                            Repeater {
                                id: runningList
                                model: agents.model
                                delegate: AListRow {
                                    Layout.fillWidth: true
                                    visible: model.running
                                    // 这里不手写高度：布局按 AListRow 的
                                    // implicitHeight（48）给行定尺寸并跳过
                                    // 隐藏行，Flickable 的 contentHeight
                                    // 因此保持真实。
                                    onVisibleChanged: Qt.callLater(page.recountRunning)

                                    AgentAvatar {
                                        iconSource: model.icon
                                        agentColor: model.color
                                        running: model.running
                                    }
                                    ColumnLayout {
                                        spacing: 0
                                        Layout.fillWidth: true
                                        Label {
                                            text: model.name
                                            color: theme.textPrimary
                                            font.pixelSize: theme.fontSizeBody
                                            font.bold: true
                                        }
                                        Label {
                                            text: model.webUrl
                                            color: theme.textMuted
                                            font.pixelSize: theme.fontSizeSmall
                                            font.family: theme.monoFamily
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }
                                    }
                                    AButton {
                                        variant: "primary"
                                        text: qsTr("Open")
                                        onClicked: {
                                            page.homeActive = false
                                            workbench.openWeb(model.agentId)
                                        }
                                    }
                                }
                            }
                        }
                    },

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        visible: page.runningCount === 0
                        text: qsTr("No agent is running - start one from the launcher page.")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeSmall
                    }
                ]
            }

            // surface：每个标签保留自己的位置；released 的视图销毁
            // （Loader inactive），frozen 的视图留在内存。
            Repeater {
                model: web.model
                delegate: Item {
                    id: tabHost

                    required property string tabId
                    required property string state
                    required property string surfaceKind
                    required property var tabObject

                    anchors.fill: parent
                    visible: web.activeTabId === tabId
                             && emptyState.visible === false

                    // released 态：灰占位，直到恢复。
                    Rectangle {
                        anchors.fill: parent
                        visible: tabHost.state === "released"
                        color: theme.windowBg

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: theme.spacingM
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("View released to free memory")
                                color: theme.textMuted
                                font.pixelSize: theme.fontSizeBody
                            }
                            AButton {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("Restore view")
                                onClicked: web.reopen(tabHost.tabId)
                            }
                        }
                    }

                    Loader {
                        id: surfaceLoader
                        anchors.fill: parent
                        active: tabHost.state !== "released"
                        visible: tabHost.state !== "released"
                        source: active ? web.surfaceUrl(tabHost.surfaceKind)
                                       : ""

                        onLoaded: {
                            item.tab = tabHost.tabObject
                            item.fullScreenToggled.connect(
                                function(active) {
                                    page.chromeHidden = active
                                })
                            // 重载时条目会被覆盖；已关标签的陈旧条目
                            // 不会被查询（菜单只读活动标签的新条目）。
                            page.surfaceItems[tabHost.tabId] = item
                        }
                    }
                }
            }
        }
    }

    // --- 快捷键（用 ApplicationShortcut，Chromium 才不会吃掉）--------------
    // 全部门控在 pageCurrent（本页是当前页）上——本页常驻，见 pageCurrent
    // 属性的说明。
    // 活动标签的缩放步进（0.5..2.0 钳制）。
    function stepZoom(delta) {
        const id = web.activeTabId
        if (id.length === 0)
            return
        const tab = web.tabObject(id)
        if (!tab)
            return
        web.setTabZoom(id, Math.min(2.0, Math.max(0.5, tab.zoom + delta)))
    }

    Shortcut {
        sequences: [StandardKey.Close]
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent && web.activeTabId.length > 0
        onActivated: web.closeTab(web.activeTabId)
    }
    Shortcut {
        sequence: StandardKey.Refresh
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent && web.activeTabId.length > 0
        onActivated: web.reloadTab(web.activeTabId)
    }
    Shortcut {
        sequences: [StandardKey.NextChild]
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent
        onActivated: {
            page.homeActive = false
            web.stepActiveTab(1)
        }
    }
    Shortcut {
        sequences: [StandardKey.PreviousChild]
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent
        onActivated: {
            page.homeActive = false
            web.stepActiveTab(-1)
        }
    }
    Shortcut {
        sequences: ["Ctrl+=", "Ctrl++"]
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent && web.activeTabId.length > 0
        onActivated: stepZoom(0.1)
    }
    Shortcut {
        sequence: "Ctrl+-"
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent && web.activeTabId.length > 0
        onActivated: stepZoom(-0.1)
    }
    Shortcut {
        sequence: "Ctrl+0"
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent && web.activeTabId.length > 0
        onActivated: web.setTabZoom(web.activeTabId, 1.0)
    }
    // F12 打开 devtools——仅 Debug 构建。
    Shortcut {
        sequence: "F12"
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent && web.devToolsEnabled
                 && web.activeTabId.length > 0
        onActivated: {
            const item = page.surfaceItems[web.activeTabId]
            if (item)
                item.openDevTools()
        }
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: page.pageCurrent && page.chromeHidden
        onActivated: page.chromeHidden = false
    }
}
