import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 工作区宿主：同一时刻只显示一个页面，按导航模型给的 source 装载。切换
// 即销毁旧页——必须跨页存活的状态放在 C++（页面可毁掉重建是默认契约）。
// 例外是 keepAlive 页（Web 页与提示词工作台页——WebEngineView 的页面
// 状态、编辑器的撤销栈与分屏比例都搬不进 C++，销毁即丢）：常驻 Repeater
// 托管，但**首次访问才实例化**（active 绑「来过一次」），别的页面在前面
// 时仅隐藏。代价：keepAlive 页以 0x0 创建、首次显示才拿到真实尺寸（布
// 局约束见 designs.md「右侧主区规则」）。
Rectangle {
    id: workspace

    color: theme.workspaceBg

    // keepAlive 页只在「它是当前页」时可见；与下方 Loader 的可见性互斥，
    // 二者叠放在同一位置。
    Repeater {
        model: nav.keepAlivePages

        delegate: Loader {
            required property var modelData
            readonly property string pageId: modelData.id

            anchors.fill: parent
            visible: nav.currentPageId === pageId
            // 去过一次就常驻：没去过的页不付常驻的代价——冷启动不该为
            // 「还没点开过的工具页」预热 WebEngineView 或恢复整个 tab 组。
            // 一旦访问过（active 期间），此后仅隐藏不销毁。
            active: visited
            property bool visited: false
            source: modelData.source
            // 初值：上一页恢复可能直接落在本页（创建时 visible 已为真，
            // 不会再有变化事件）；运行中的切页走 onVisibleChanged。
            Component.onCompleted: if (visible) visited = true
            onVisibleChanged: if (visible) visited = true
            onStatusChanged: {
                if (status === Loader.Error) {
                    console.error("Workspace: failed to load page",
                                  source)
                    workbench.notify("error",
                                     qsTr("Page failed to load"),
                                     String(source))
                }
            }
        }
    }

    // 非 keepAlive 的普通页：切换即销毁（页面状态能毁掉重建是默认契约，
    // 见 designs.md「右侧主区规则」）。
    Loader {
        id: pageLoader
        anchors.fill: parent
        visible: !isKeepAliveCurrent
        // 当前页是 keepAlive 页时不能把它的 source 喂进普通 Loader，
        // 否则同一页面会实例化两份。
        source: isKeepAliveCurrent
                ? "" : (nav.currentPage.source.length > 0
                        ? nav.currentPage.source : "")
        property bool isKeepAliveCurrent: nav.currentPage.keepAlive === true

        onStatusChanged: {
            if (status === Loader.Error) {
                console.error("Workspace: failed to load page",
                              source)
                workbench.notify("error", qsTr("Page failed to load"),
                                 String(source))
            }
        }
    }

    // 尚未选中任何页面（启动时会恢复上次的页面，所以只是过渡态）。
    // keepAlive 页常驻后 Loader 不再参与，判空改为依据当前页 id：
    // 没有任何选中页（含 keepAlive 页不可见的瞬间）都算「未就绪」。
    ColumnLayout {
        anchors.centerIn: parent
        visible: nav.currentPageId.length === 0
        spacing: theme.spacingM

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Pick a page from the sidebar")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeSubtitle
        }
    }
}
