import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 体检结果悬浮窗（非模态）：草稿的规则命中清单，点一条跳到编辑区对应
// 区间，「不再提醒」按规则关闭该类命中。
//
// 容器件：认识自己域的 tools（停用规则写的就是 tools.json 的 checks 段），
// 需要页面做的事一律发信号——跳区间、停用后重跑、关闭窗口都是宿主的。
Popup {
    id: root

    ///< 体检结果；null = 还没跑过（显示引导文案）
    property var results: null
    ///< 右侧给悬浮大纲让出的宽度（大纲面板 224 加两侧间距）
    property real reservedRight: 248
    ///< 距页面顶部的位置（避开命令条）。注意不能叫 topMargin——Popup 自己
    ///< 的 topMargin 是 FINAL 的内边距，覆写会让整个类型不可用。
    property real topOffset: 108

    ///< 用户点了关闭
    signal closeRequested()
    ///< 停用了一条规则，宿主应重跑一次让该类命中从清单消失
    signal rerunRequested()
    ///< 点了一条命中：跳到草稿里的 [start, length) 区间
    signal rangeRequested(int start, int length)

    x: Math.max(root.parent.width - width - root.reservedRight, theme.spacingM)
    y: root.topOffset
    width: 300
    height: Math.min(checksColumn.implicitHeight + theme.spacingS * 2,
                     root.parent.height - y - theme.spacingM)
    modal: false
    closePolicy: Popup.NoAutoClose | Popup.CloseOnEscape

    background: Rectangle {
        color: theme.surfaceBg
        border.color: theme.borderSubtle
        border.width: 1
        radius: theme.radiusCard
    }

    ColumnLayout {
        id: checksColumn
        anchors.fill: parent
        anchors.margins: theme.spacingS
        spacing: theme.spacingS

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.spacingS

            Label {
                Layout.fillWidth: true
                text: qsTr("Checks")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            AIconButton {
                iconSource: "qrc:/icons/close.svg"
                tooltip: qsTr("Close")
                onClicked: root.closeRequested()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.results === null
            text: qsTr("Run a check for concrete suggestions.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.Wrap
        }

        // 命中列表：色点按严重度（0 提示 / 1 提醒 / 2 警告），点行跳到
        // 编辑区对应区间并选中。「不再提醒」按规则关闭（写 tools.json 的
        // checks 段），该类命中即刻隐去。
        ListView {
            id: checkList
            objectName: "checkList"

            visible: root.results !== null
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.results || []

            ScrollBar.vertical: AScrollBar {}

            delegate: Item {
                id: checkRow

                required property var modelData
                required property int index

                // 同 fileTab：作用域显式传入（编译型 QML 的 delegate 处理器
                // 里解析文档根 id 会 ReferenceError）。
                property var scope: root

                width: checkList.width
                // 两行文字 + 关闭按钮的行高；消息长短不定，按内容给。
                height: checkText.implicitHeight + theme.spacingS

                Rectangle {
                    anchors.fill: parent
                    radius: theme.radiusControl
                    color: theme.surfaceHoverBg
                    visible: checkHover.hovered
                }

                HoverHandler {
                    id: checkHover
                }

                TapHandler {
                    // 区间可定位（start >= 0）才可跳转；规则级的命中
                    //（如缺验收标准）没有对应区间。
                    onSingleTapped: {
                        if (checkRow.modelData.start >= 0)
                            scope.rangeRequested(checkRow.modelData.start,
                                                 checkRow.modelData.length)
                    }
                }

                // 严重度色点：提醒用注意色、警告用警示色、提示用弱化色
                //（不抢注意力）。
                Rectangle {
                    x: theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6
                    height: 6
                    radius: 3
                    color: checkRow.modelData.severity === 2
                           ? theme.danger
                           : (checkRow.modelData.severity === 1
                              ? theme.warning : theme.textDisabled)
                }

                Label {
                    id: checkText
                    x: theme.spacingS + 14
                    width: parent.width - x - theme.spacingS - 20
                    text: checkRow.modelData.message
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeCaption
                    wrapMode: Text.Wrap
                }

                AIconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: theme.spacingXs
                    anchors.verticalCenter: parent.verticalCenter
                    iconSource: "qrc:/icons/close.svg"
                    tooltip: qsTr("Don't remind me of this again")
                    onClicked: {
                        const disabled = tools.disabledCheckIds.slice()
                        disabled.push(checkRow.modelData.ruleId)
                        tools.disabledCheckIds = disabled
                        // 关掉即重跑：该类命中从当前清单消失。
                        scope.rerunRequested()
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: checkList.count === 0
                text: qsTr("No issues found.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
            }
        }
    }
}
