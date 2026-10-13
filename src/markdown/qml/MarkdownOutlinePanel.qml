import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 悬浮大纲：正在读的这份文档的导航，悬浮在内容之上（不占布局，也不改
// 分栏比例），可收可调——头部按钮把面板收成一行标题，左边界调宽、右下角
// 手柄调高。
//
// 展示件：条目由宿主注入，点一条发信号；本组件不认识任何门面。三个几何
// 状态（收起位、拖出的宽与高）由本组件自己持有——它的生存期就是所在页面
// 的生存期，宿主不必再为此维护一份页面状态。高度为 0 表示「通高」。
Rectangle {
    id: root

    ///< 大纲条目（level / text / anchor / blockIndex），宿主注入
    property var entries: []
    ///< 收起态：整个面板只剩一行标题
    property bool collapsed: false
    ///< 面板宽度；左侧调宽手柄在 160–320 之间改它
    property real panelWidth: 224
    ///< 用户拖出的高度；0 = 通高（保持初见观感）
    property real panelHeight: 0

    ///< 点了一条纲目：宿主据此把那一块的解析插队到分片队列前面
    signal anchorActivated(string anchor, int blockIndex)

    // 顶部锚定不锚底：高度由折叠态与用户拖出的高度决定。
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.margins: theme.spacingS
    width: root.panelWidth
    height: {
        if (root.collapsed)
            return outlineHeader.implicitHeight + theme.spacingS * 2
        const full = root.parent.height - theme.spacingS * 2
        return root.panelHeight > 0
                ? Math.min(root.panelHeight, full)
                : full
    }
    // 高不透明：悬浮在正文之上，文字要稳读。
    color: theme.alpha(theme.surfaceBg, 0.93)
    border.color: outlineEdgeResize.containsMouse
                   || outlineGripDrag.containsMouse
          ? theme.borderStrong : theme.borderSubtle
    border.width: 1
    radius: theme.radiusControl

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.spacingS
        spacing: theme.spacingXs

        // 头部：标题 + 收起开关（收起态下整个面板只剩这一行，开关是唯一
        // 的还原入口）。
        RowLayout {
            id: outlineHeader
            Layout.fillWidth: true
            spacing: theme.spacingXs

            Label {
                Layout.fillWidth: true
                text: qsTr("Outline")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                font.bold: true
            }

            AIconButton {
                objectName: "outlineCollapseButton"
                iconSource: "qrc:/icons/chevron-down.svg"
                // 展开时箭头朝上（点了收起），收起时朝下（点了展开）。
                rotation: root.collapsed ? 0 : 180
                tooltip: root.collapsed ? qsTr("Expand the outline")
                                        : qsTr("Collapse the outline")
                onClicked: root.collapsed = !root.collapsed
            }
        }

        // 数据由宿主算好（level / text / anchor / blockIndex），这里只画。
        // 收起时整块隐藏（Layout 对不可见子项不占位，面板随之缩到标题行）。
        ListView {
            id: outlineList
            objectName: "outlineList"

            visible: !root.collapsed

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.entries

            ScrollBar.vertical: AScrollBar {}

            delegate: Item {
                id: outlineRow

                required property var modelData
                required property int index

                // 同 fileTab：作用域显式传入（编译型 QML 的 delegate 处理器
                // 里解析文档根 id 会 ReferenceError）。
                property var scope: root

                width: outlineList.width
                height: 26

                Rectangle {
                    anchors.fill: parent
                    radius: theme.radiusControl
                    color: theme.surfaceHoverBg
                    visible: outlineHover.hovered
                }

                HoverHandler {
                    id: outlineHover
                }

                TapHandler {
                    onSingleTapped: scope.anchorActivated(
                        outlineRow.modelData.anchor,
                        outlineRow.modelData.blockIndex)
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    // 缩进按标题级别：level 1 不缩进。
                    x: theme.spacingS
                       + Math.max(0, outlineRow.modelData.level - 1)
                         * theme.spacingM
                    width: parent.width - x - theme.spacingS
                    text: outlineRow.modelData.text
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                    elide: Text.ElideRight
                }
            }

            Label {
                anchors.centerIn: parent
                visible: outlineList.count === 0
                text: qsTr("No headings")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
            }
        }
    }

    // --- 左边界：调宽（视觉只染边框，命中 8px） ---------------------
    // 右缘是锚定的：往左拖面板变宽、往右拖变窄。宽度按指针在宿主区域里的
    // 位置反推（与分栏分隔条同一条绝对坐标纪律）。
    MouseArea {
        id: outlineEdgeResize
        objectName: "outlineWidthEdge"

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 8
        cursorShape: Qt.SplitHCursor
        hoverEnabled: true

        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            const at = mapToItem(root.parent, mouse.x, 0)
            const edge = root.parent.width - theme.spacingS
            root.panelWidth = Math.max(160, Math.min(320, edge - at.x))
        }
    }

    // --- 右下角手柄：调高（QSizeGrip 形态） -------------------------
    // 收起态高度只有一行，调高没有意义，手柄退场。
    Item {
        id: outlineGrip
        objectName: "outlineSizeGrip"

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 14
        height: 14
        visible: !root.collapsed

        // 三道斜纹：QSizeGrip 的经典记号，主题弱化色（纯装饰，命中区是
        // 整个 14×14）。主题换色时重画（Canvas 不跟令牌走绑定）。
        Canvas {
            anchors.fill: parent
            property color gripColor: theme.textDisabled
            onGripColorChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = gripColor
                ctx.lineWidth = 1.4
                for (var i = 1; i <= 3; ++i) {
                    var o = i * 4
                    ctx.beginPath()
                    ctx.moveTo(width - o - 1, height - 3)
                    ctx.lineTo(width - 3, height - o - 1)
                    ctx.stroke()
                }
            }
        }

        MouseArea {
            id: outlineGripDrag
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            hoverEnabled: true
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const at = mapToItem(root.parent, 0, mouse.y)
                root.panelHeight = Math.max(
                            110,
                            Math.min(root.parent.height - theme.spacingS * 2,
                                     at.y - root.y))
            }
        }
    }
}
