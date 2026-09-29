import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 悬停详情浮层：完整描述 + 元数据表。由卡片悬停 400ms 后打开；指针
// 离开卡片与浮层 300ms 后关闭，任意按键或页面滚动也会关闭。定位与
// 开关由 SkillCard 驱动，本组件只负责呈现。
Popup {
    id: flyout

    // 目标 skill 的 SKILL.md 路径（门面查询的定位键）。
    property string skillFilePath: ""
    // 延迟关闭标记：tryCloseLater 置真后指针若回到浮层上会被取消。
    property bool closePending: false

    // 路径对应的 skill 定义（经 skills 门面查询）。
    readonly property var info: skills.skill(skillFilePath)

    modal: false
    focus: false
    padding: theme.spacingM
    width: 420
    // 不绑 height：Popup 的隐式高度 = contentItem 的 implicitHeight（见
    // QQuickControlPrivate::getContentHeight）。此前两版都把高度绑在
    // Flickable 上（implicitHeight 恒 0 会压扁浮层；contentHeight 又会与
    // contentItem 尺寸同步互馈——contentHeight 求值 → 通知 Popup 隐式高度
    // → resizeContent → Flickable 几何变化重读 contentHeight，判成绑定循
    // 环，每张卡片都刷警告）。现在的形状与 WebTabsPage 的 Flickable 一致：
    // 尺寸链经过布局引擎的 polish（异步），同步栈上没有环——外层
    // ColumnLayout 当 contentItem 提供隐式高度，Flickable 用
    // Layout.preferredHeight 封顶，超过上限就滚动。
    closePolicy: Popup.NoAutoClose

    background: Rectangle {
        color: theme.overlayBg
        border.color: theme.borderSubtle
        border.width: 1
        radius: theme.radiusOverlay
    }

    // 指针进出浮层：进入取消待定的关闭（指针在卡片与浮层之间移动时不
    // 关）；离开重新武装延迟关闭。离开这一半不可省——指针从浮层直接移
    // 到页面空白处时，卡片的 hover 早已是 false，不会再有任何路径触发
    // 关闭，浮层就滞留在屏上，直到指针重新扫过那张卡片。
    HoverHandler {
        id: flyoutHostHover
        onHoveredChanged: {
            if (flyoutHostHover.hovered)
                flyout.closePending = false
            else
                flyout.tryCloseLater()
        }
    }

    // 延迟关闭：先标记，300ms 后指针仍不在卡片/浮层上才真正关闭。
    function tryCloseLater() {
        closePending = true
        closeTimer.restart()
    }

    // 取消待定的关闭：指针回到宿主卡片上时由 SkillCard 调用。卡片悬停
    // 的 400ms 重开晚于这里的 300ms 关闭，不取消的话浮层会先关再开闪
    // 一次。
    function cancelClose() {
        closePending = false
    }

    Timer {
        id: closeTimer
        interval: 300
        onTriggered: {
            if (flyout.closePending && !flyoutHostHover.hovered)
                flyout.close()
        }
    }

    // 按键关闭放在卡片上而不是这里：Keys 只能附加到 Item，而 Popup 是
    // QObject——在这里附加只会每张卡片打一条 "Could not attach Keys
    // property … is not an Item"。

    // 内容列：名称/来源/描述 + 元数据表。
    contentItem: ColumnLayout {
        id: body
        spacing: 0

        // Flickable 的 contentHeight 不能直接绑 details.implicitHeight：
        // 浮层关闭时不在场景里，布局引擎不会 polish 它，绑定求值中读
        // details.implicitHeight 会同步触发布局，链条（Flickable 高度变化
        // → fixupY 重读 contentHeight）又同步回到本绑定，被 QQmlBinding
        // 判成 "Binding loop detected"（每张卡片一条）。改为在布局完成
        // 与内容变化后用 Qt.callLater 异步回填——写回发生在同步栈之外，
        // 判定不再触发，打开时数值也已就绪。
        Flickable {
            id: scroller

            Layout.fillWidth: true
            // 内容超过上限改为滚动：上限按浮层总高 320 折算（减去上下 padding）。
            Layout.preferredHeight: Math.min(contentHeight,
                                             320 - 2 * flyout.padding)
            contentWidth: width
            contentHeight: 0
            clip: true

            onHeightChanged: Qt.callLater(scroller.syncContentHeight)
            onWidthChanged: Qt.callLater(scroller.syncContentHeight)
            Connections {
                target: details
                function onImplicitHeightChanged() {
                    Qt.callLater(scroller.syncContentHeight)
                }
            }
            function syncContentHeight() {
                contentHeight = details.implicitHeight
            }

            ColumnLayout {
                id: details
                width: scroller.width
                spacing: theme.spacingS

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingS
                    Label {
                        Layout.fillWidth: true
                        text: flyout.info.name || ""
                        color: theme.textPrimary
                        font.pixelSize: theme.fontSizeSubtitle
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    APill {
                        text: flyout.info.kind || ""
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: (flyout.info.rootLabel || "").length > 0
                    text: flyout.info.rootLabel || ""
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeCaption
                }

                Label {
                    Layout.fillWidth: true
                    visible: (flyout.info.description || "").length > 0
                    text: flyout.info.description || ""
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme.separator
                }

                // 元数据表：SKILL.md 路径、修改时间、大小。
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: theme.spacingM
                    rowSpacing: theme.spacingXs

                    Label {
                        text: qsTr("SKILL.md")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                    }
                    Label {
                        Layout.fillWidth: true
                        text: flyout.info.skillFilePath || ""
                        color: theme.textSecondary
                        font.pixelSize: theme.fontSizeCaption
                        font.family: theme.monoFamily
                        elide: Text.ElideMiddle
                        // tooltip 用被动的 HoverHandler 而不是 hoverEnabled 的
                        // MouseArea：hover 是独占投递，MouseArea 会截断根
                        // HoverHandler——指针停在这条路径上时浮层自己会在
                        // 300ms 后关闭。
                        HoverHandler { id: pathHover }
                        ToolTip.visible: pathHover.hovered
                        ToolTip.delay: 300
                        ToolTip.timeout: 10000
                        ToolTip.text: flyout.info.skillFilePath || ""
                    }

                    Label {
                        text: qsTr("Modified")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                    }
                    Label {
                        text: flyout.info.lastModified !== undefined
                              ? Qt.formatDateTime(flyout.info.lastModified,
                                                  "yyyy-MM-dd hh:mm")
                              : ""
                        color: theme.textSecondary
                        font.pixelSize: theme.fontSizeCaption
                    }

                    Label {
                        text: qsTr("Size")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                    }
                    Label {
                        text: {
                            const bytes = flyout.info.sizeBytes || 0
                            return (bytes / 1024).toFixed(1) + " KB"
                        }
                        color: theme.textSecondary
                        font.pixelSize: theme.fontSizeCaption
                    }
                }

                // frontmatter 的其余标量字段（名称-值成对展示）。
                Repeater {
                    model: {
                        const extras = flyout.info.extras || {}
                        const rows = []
                        for (const key in extras)
                            rows.push({ key: key, value: String(extras[key]) })
                        return rows
                    }
                    delegate: GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: theme.spacingM

                        // 浮层跟随不同 skill 时这些行会重建——悬停中的行
                        // 在这里死掉会冻住共享 tooltip，与卡片 delegate
                        // 同理。
                        Component.onDestruction: ToolTip.hide()

                        Label {
                            text: modelData.key
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: theme.textSecondary
                            font.pixelSize: theme.fontSizeCaption
                            elide: Text.ElideRight
                            // 同上：被动 HoverHandler，不截断根 HoverHandler。
                            HoverHandler { id: valueHover }
                            ToolTip.visible: valueHover.hovered
                            ToolTip.delay: 300
                            ToolTip.timeout: 10000
                            ToolTip.text: modelData.value
                        }
                    }
                }

                Label {
                    text: qsTr("Click the card to copy the path")
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                    font.italic: true
                }
            }
        }
    }
}
