import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Hover detail flyout : full description + metadata table.
// Opened by the card after 400 ms; closes 300 ms after the pointer leaves
// both the card and this popup, on any key press, or on page scroll.
Popup {
    id: flyout

    property string skillFilePath: ""
    property bool closePending: false

    // The definition behind the path (facade lookup).
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

    // The pointer entered the flyout: cancel the pending close.
    HoverHandler {
        onHoveredChanged: {
            if (hovered)
                flyout.closePending = false
        }
    }

    function tryCloseLater() {
        closePending = true
        closeTimer.restart()
    }

    Timer {
        id: closeTimer
        interval: 300
        onTriggered: {
            if (flyout.closePending && !flyoutHostHover.hovered)
                flyout.close()
        }
    }

    HoverHandler {
        id: flyoutHostHover
    }

    // Key-press close lives on the card, not here: Keys can only
    // attach to an Item, and a Popup is a QObject — attaching it here just
    // logged "Could not attach Keys property … is not an Item" once per card.

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

                // Metadata table.
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
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 300
                            ToolTip.timeout: 10000
                            ToolTip.text: flyout.info.skillFilePath || ""
                        }
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

                // Extra frontmatter scalars.
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

                        // Extra rows are rebuilt whenever the flyout follows a
                        // different skill — a hovered row dying here froze the
                        // shared tooltip, exactly like the card delegates.
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
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 300
                                ToolTip.timeout: 10000
                                ToolTip.text: modelData.value
                            }
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
