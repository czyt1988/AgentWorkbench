import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App
import "RibbonIcons.js" as RibbonIcons

// 提示词标签页的模板库 gallery：SARibbon 声明式 gallery 的自绘替身。
//
// SARibbon 的 RibbonGalleryGroup 是静态 listProperty（items 只能声明式
// 追加、无 Q_INVOKABLE 清空），模板是用户数据、增删都要重建列表——
// 动态模型塞不进它的声明面。本组件按 gallery 的组织形态自绘：一行可
// 横向滚动的模板格子 + 左上 ‹ › 分组翻页（按 category 聚组），点击格子
// 走 scope.insertTemplate(id)（动作出口在页面动作层，这里只转发）。
//
// 颜色全部用 theme 令牌：它住在 SARibbon 的 panel 里，面板背景经主题桥
// 已与 theme.surfaceBg 同值，两套主题下都协调。
Rectangle {
    id: gallery

    /// 宿主页面（MarkdownWorkbenchPage）：插入动作的唯一出口。
    property var scope: null

    /// 模板条目表（tools.filteredTemplates 的形状：
    /// {id, title, category, builtin}）。
    property var entries: []

    /// 分类空串显示成的组名（模板未分类时的归属）。
    property string defaultGroupTitle: qsTr("Templates")

    readonly property color glyphColor: theme.textPrimary

    // 分组清单（按 category 聚合，空串归默认组；顺序稳定 = 首见序）。
    readonly property var groups: {
        const order = []
        const byName = {}
        for (let i = 0; i < entries.length; ++i) {
            const name = entries[i].category && entries[i].category.length > 0
                    ? entries[i].category : defaultGroupTitle
            if (byName[name] === undefined) {
                byName[name] = []
                order.push(name)
            }
            byName[name].push(entries[i])
        }
        return { order: order, byName: byName }
    }

    property int currentGroupIndex: 0

    // 当前组的条目（组越界时夹取）。
    readonly property var currentEntries: {
        const order = groups.order
        if (order.length === 0)
            return []
        const index = Math.min(Math.max(currentGroupIndex, 0),
                               order.length - 1)
        return groups.byName[order[index]]
    }

    readonly property string currentGroupTitle: groups.order.length > 0
        ? groups.order[Math.min(Math.max(currentGroupIndex, 0),
                                groups.order.length - 1)]
        : defaultGroupTitle

    onEntriesChanged: currentGroupIndex = 0

    implicitWidth: 280
    implicitHeight: groupRowHeight + cellHeight + theme.spacingS * 2
    radius: theme.radiusControl
    color: theme.alpha(theme.surfaceBg, 0.55)
    border.width: 1
    border.color: theme.borderSubtle

    // 组切换与格子行共用一组尺寸常量。
    readonly property int cellHeight: 38
    readonly property int cellWidth: 132
    readonly property int groupRowHeight: 22

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.spacingS
        spacing: theme.spacingXs

        // --- 分组切换行：‹ 组名 ›（单组时箭头禁用） ---------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: gallery.groupRowHeight
            spacing: theme.spacingXs

            // 页面私有微型交互件（designs.md §3 的例外条款）：18px 箭头。
            Item {
                width: 18
                height: 18

                Image {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    sourceSize: Qt.size(14, 14)
                    source: RibbonIcons.iconUrl("chevronLeft",
                                                gallery.glyphColor)
                    opacity: gallery.groups.order.length > 1 ? 1 : 0.4
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: gallery.groups.order.length > 1
                    cursorShape: enabled ? Qt.PointingHandCursor
                                          : Qt.ArrowCursor
                    onClicked: {
                        if (gallery.currentGroupIndex > 0)
                            gallery.currentGroupIndex -= 1
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: gallery.currentGroupTitle
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                elide: Text.ElideMiddle
            }

            Item {
                width: 18
                height: 18

                Image {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    sourceSize: Qt.size(14, 14)
                    source: RibbonIcons.iconUrl("chevronRight",
                                                gallery.glyphColor)
                    opacity: gallery.groups.order.length > 1 ? 1 : 0.4
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: gallery.groups.order.length > 1
                    cursorShape: enabled ? Qt.PointingHandCursor
                                          : Qt.ArrowCursor
                    onClicked: {
                        if (gallery.currentGroupIndex
                                < gallery.groups.order.length - 1)
                            gallery.currentGroupIndex += 1
                    }
                }
            }
        }

        // --- 模板格子行：横向滚动 + 滚轮翻格 -----------------------
        Flickable {
            id: strip

            Layout.fillWidth: true
            Layout.preferredHeight: gallery.cellHeight
            contentWidth: rowLayout.width
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            // Flickable 的滚轮默认驱动纵向；横向条把它折成横向位移。
            WheelHandler {
                onWheel: function (event) {
                    strip.contentX = Math.max(
                                0, Math.min(strip.contentWidth - strip.width,
                                            strip.contentX
                                            + event.angleDelta.y))
                    event.accepted = true
                }
            }

            Row {
                id: rowLayout

                spacing: theme.spacingXs

                Repeater {
                    model: gallery.currentEntries

                    delegate: Rectangle {
                        id: cell

                        // 测试锚点（tst_ribbonui 经真实点击驱动插入）。
                        objectName: "galleryCell"
                        required property var modelData

                        width: gallery.cellWidth
                        height: gallery.cellHeight
                        radius: theme.radiusControl
                        color: cellHover.hovered ? theme.surfaceHoverBg
                                                 : "transparent"
                        border.width: 1
                        border.color: cellHover.hovered
                                      ? theme.borderStrong
                                      : theme.borderSubtle

                        // 内建模板给一个小色点标记（浅色主题下边框差异
                        // 小，点比描边更稳）。
                        Rectangle {
                            anchors.left: parent.left
                            anchors.leftMargin: theme.spacingS
                            anchors.verticalCenter: parent.verticalCenter
                            width: 4
                            height: 4
                            radius: 2
                            visible: cell.modelData.builtin
                            color: theme.textMuted
                        }

                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: theme.spacingS + 10
                            anchors.right: parent.right
                            anchors.rightMargin: theme.spacingS
                            anchors.verticalCenter: parent.verticalCenter
                            text: cell.modelData.title
                            color: theme.textPrimary
                            font.pixelSize: theme.fontSizeCaption
                            elide: Text.ElideMiddle
                        }

                        HoverHandler {
                            id: cellHover
                        }

                        TapHandler {
                            onSingleTapped: {
                                if (gallery.scope)
                                    gallery.scope.insertTemplate(
                                                cell.modelData.id)
                            }
                        }
                    }
                }
            }

            // 空态：没有模板时的整行提示。
            Label {
                anchors.centerIn: parent
                visible: gallery.currentEntries.length === 0
                text: qsTr("No templates yet. Save the draft as a template.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
            }
        }
    }
}
