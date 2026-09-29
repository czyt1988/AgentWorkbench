import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 工作区宿主：同一时刻只显示一个页面，按导航模型给的 source 装载。切换
// 即销毁旧页——必须跨页存活的状态放在 C++（页面可毁掉重建是默认契约）。
// 例外是 keepAlive 页（Web 页——WebEngineView 的状态搬不进 C++，销毁即
// 整页重载）：由下方常驻 Repeater 实例化一次，别的页面在前面时仅隐藏。
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
            active: true
            source: modelData.source
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
