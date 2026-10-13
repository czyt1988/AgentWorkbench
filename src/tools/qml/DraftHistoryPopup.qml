import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 草稿版本历史悬浮窗（非模态）：当前草稿的快照清单与回滚入口。点一版 =
// 回滚（页面先弹确认——回滚是破坏性替换；当前版会被自动存成 rollback
// 快照）。
//
// 容器件：清单数据从自己域的 tools 现取（snapshots() 是方法调用，QML
// 追不到它内部依赖，所以由宿主在门面信号里调 rebuild() 主动重算，与
// 拆分前同一条纪律）；回滚与存版本是页面动作，发信号出去。
Popup {
    id: root

    ///< 与之错位排列的兄弟浮窗（体检窗）：它可见时本窗排在它下面
    property var sibling: null
    ///< 右侧给悬浮大纲让出的宽度（大纲面板 224 加两侧间距）
    property real reservedRight: 248
    ///< 距页面顶部的位置（避开命令条）。注意不能叫 topMargin——Popup 自己
    ///< 的 topMargin 是 FINAL 的内边距，覆写会让整个类型不可用。
    property real topOffset: 108

    ///< 用户点了关闭
    signal closeRequested()
    ///< 用户点了「存版本」
    signal saveVersionRequested()
    ///< 用户点了一版要回滚（参数是该快照的 createdAt）
    signal rollbackRequested(double createdAt)

    ///< 按当前草稿重算快照清单（新在前）。宿主在 activeDraftIdChanged /
    /// draftsChanged 上调用。
    function rebuild() {
        const id = tools.activeDraftId
        snapshotList.rows = id.length === 0 ? [] : tools.snapshots(id)
    }

    x: (root.sibling && root.sibling.visible)
       ? root.sibling.x
       : Math.max(root.parent.width - width - root.reservedRight, theme.spacingM)
    y: (root.sibling && root.sibling.visible)
       ? Math.min(root.sibling.y + root.sibling.height + 8,
                  root.parent.height - height - theme.spacingM)
       : root.topOffset
    width: 300
    height: Math.min(historyColumn.implicitHeight + theme.spacingS * 2, 260)
    modal: false
    closePolicy: Popup.NoAutoClose | Popup.CloseOnEscape

    background: Rectangle {
        color: theme.surfaceBg
        border.color: theme.borderSubtle
        border.width: 1
        radius: theme.radiusCard
    }

    ColumnLayout {
        id: historyColumn
        anchors.fill: parent
        anchors.margins: theme.spacingS
        spacing: theme.spacingXs

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.spacingS

            Label {
                Layout.fillWidth: true
                text: qsTr("History")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            AButton {
                objectName: "saveVersionButton"
                text: qsTr("Save version")
                variant: "ghost"
                onClicked: root.saveVersionRequested()
            }
            AIconButton {
                iconSource: "qrc:/icons/close.svg"
                tooltip: qsTr("Close")
                onClicked: root.closeRequested()
            }
        }

        ListView {
            id: snapshotList
            objectName: "snapshotList"

            Layout.fillWidth: true
            // 按内容给高并封顶（快照列表是浏览态，不需要吃满浮窗）。
            implicitHeight: Math.min(contentHeight, 170)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: 2

            // 同页内其余列表：JS 数组模型（ObjectModel 是 Qt 6.5+ 类型；
            // 数组整体赋值即模型重置）。
            property var rows: []

            model: rows
            delegate: snapshotRow

            Component {
                id: snapshotRow

                AListRow {
                    id: histRow
                    required property var modelData

                    // 同体检行：作用域显式传入。
                    property var scope: root

                    width: snapshotList.width
                    rowHeight: 30

                    // 行内容直接注入（AListRow 的内容 RowLayout 负责摆位）。
                    Label {
                        Layout.fillWidth: true
                        // 时间（HH:mm）+ 原因 + 字符数。
                        text: {
                            const when = new Date(histRow.modelData.createdAt)
                            const hh = ("0" + when.getHours()).slice(-2)
                            const mm = ("0" + when.getMinutes()).slice(-2)
                            return hh + ":" + mm
                                   + "  ·  "
                                   + histRow.modelData.reason
                                   + "  ·  "
                                   + histRow.modelData.charCount
                        }
                        color: theme.textSecondary
                        font.pixelSize: theme.fontSizeCaption
                        elide: Text.ElideMiddle
                    }

                    TapHandler {
                        onSingleTapped:
                            scope.rollbackRequested(
                                histRow.modelData.createdAt)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: snapshotList.count === 0
                text: qsTr("Versions appear when you copy, replace or switch.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
