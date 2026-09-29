import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgentWorkbench
import AgentWorkbench.App

// 单张 skill 卡片：340x160，左键复制目录路径，右键弹出复制/打开动作，
// 悬停 400ms 打开详情 flyout（SkillDetailFlyout）。剪贴板与文件操作经
// skills.* 门面，结果用 toast 通知。
Item {
    id: card

    // SKILL.md 文件路径（skills.* 门面操作的定位键）。
    property string skillFilePath: ""
    // skill 名称（frontmatter 的 name）。
    property string skillName: ""
    // skill 描述（frontmatter 的 description）。
    property string description: ""
    // skill 所在目录路径（复制路径的目标）。
    property string dirPath: ""
    // 来源类型（builtin/personal/plugin，徽标文字）。
    property string kind: ""
    // 扫描根的显示标签。
    property string rootLabel: ""
    // 插件版本（plugin 来源时显示）。
    property string pluginVersion: ""

    // 路径展示文本（当前直接用目录路径）。
    readonly property string pathText: dirPath

    width: theme.cardMinWidth + 80 // 规格宽度 340
    height: 160

    // 卡片是模型 delegate：搜索/过滤/重扫描会在悬停中销毁它们，而
    // ToolTip 附加属性在每窗口只共享一个可视化 tooltip。悬停宿主死掉时
    // 它的 `ToolTip.visible` 绑定跟着死，再没有人去隐藏那个共享 tooltip
    // ——它会冻在屏幕上。销毁时 hide；每个 tooltip 还带 timeout，漏网的
    // 情况也能自愈。
    Component.onDestruction: ToolTip.hide()

    // --- 悬停 flyout（400ms 延时）---------------------------------
    // 悬停满 400ms 才打开，避免扫过卡片就闪出详情。
    Timer {
        id: hoverTimer
        interval: 400
        onTriggered: card.openFlyout()
    }

    // 边缘感知的打开：默认落在卡片右侧，会被窗口边缘裁掉时向左/上翻转。
    // height 现在由内容驱动，但保留 320 的兜底——popup 尚未布局时翻转
    // 判断不能用 0。
    function openFlyout() {
        const pos = card.mapToItem(null, 0, 0)
        const win = card.Window.window
        if (!win)
            return
        const gap = theme.spacingM
        // height is content-driven now, but keep the 320 fallback in case
        // the popup has not been laid out yet (flip test must not use 0).
        const fh = flyout.height > 1 ? flyout.height : 320
        flyout.x = (pos.x + card.width + gap + flyout.width > win.width)
                   ? -(flyout.width + gap) : card.width + gap
        flyout.y = (pos.y + fh > win.height)
                   ? -(fh - card.height) : 0
        flyout.open()
    }

    Rectangle {
        id: background
        anchors.fill: parent
        radius: theme.radiusCard
        color: hoverHandler.hovered ? theme.surfaceHoverBg : theme.surfaceBg
        border.color: hoverHandler.hovered ? theme.accent : theme.borderSubtle
        border.width: 1
        Behavior on color { ColorAnimation { duration: theme.durationFast } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: theme.spacingM
            spacing: theme.spacingXs

            // 标题行 + 来源徽标。
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Label {
                    Layout.fillWidth: true
                    text: card.skillName
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeSubtitle
                    font.bold: true
                    elide: Text.ElideRight
                }
                APill {
                    text: card.kind
                    tooltip: card.kind === "plugin"
                             ? qsTr("Plugin: %1").arg(card.pluginVersion)
                             : ""
                }
            }

            // 描述，至多三行。maximumLineCount 已在第三行后省略，
            // implicitHeight 就是正确高度——不用额外钳制（lineHeight 是
            // 倍率不是像素；拿它当像素上限会把这里塌缩到 ~4px）。
            Label {
                Layout.fillWidth: true
                text: card.description.length > 0 ? card.description
                                                  : qsTr("No description.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                clip: true
            }

            Item { Layout.fillHeight: true }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.separator
            }

            // 路径 + 复制按钮。
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Label {
                    Layout.fillWidth: true
                    text: card.pathText
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                    font.family: theme.monoFamily
                    elide: Text.ElideMiddle
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 300
                        ToolTip.timeout: 10000
                        ToolTip.text: card.pathText
                    }
                }
                AIconButton {
                    iconSource: "qrc:/icons/copy.svg"
                    tooltip: qsTr("Copy path")
                    onClicked: card.copyPath()
                }
            }
        }
    }

    HoverHandler {
        id: hoverHandler
        onHoveredChanged: {
            if (hovered)
                hoverTimer.start()
            else {
                hoverTimer.stop()
                flyout.tryCloseLater()
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: card.forceActiveFocus()
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                contextMenu.popup()
            else
                card.copyPath()
        }
        // 滚轮划过卡片时立即收起 flyout（在别处滚动由上方的 hover-out 路径
        // 覆盖）。wheel.accepted 置 false 让事件继续传给背后的 ScrollView——
        // 不用 WheelHandler.blocking 做这件事：该属性 Qt 6.2 才引入，Qt 5 下
        // 对它赋值会让整个 SkillCard 连同 Skills 页加载失败。
        onWheel: function(wheel) {
            if (flyout.opened)
                flyout.close()
            wheel.accepted = false
        }
    }

    // 键盘：Tab 可聚焦卡片（聚焦同时显示 flyout），Enter 复制路径，
    // Ctrl+Enter 打开所在文件夹。用 activeFocusOnTab 而不是 focus: true
    // ——每个 delegate 都设 focus 会让最后创建的卡片抢走页面初始焦点。
    activeFocusOnTab: true
    onActiveFocusChanged: {
        if (activeFocus)
            card.openFlyout()
        else
            flyout.tryCloseLater()
    }
    Keys.onReturnPressed: copyPath()
    Keys.onEnterPressed: copyPath()
    Keys.onPressed: function(event) {
        // 任意按键先关掉 flyout；再按一次才生效。
        if (flyout.opened) {
            flyout.close()
            event.accepted = true
            return
        }
        if ((event.modifiers & Qt.ControlModifier)
            && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            skills.openFolder(card.skillFilePath)
            event.accepted = true
        }
    }

    // 复制目录路径，结果经 toast 反馈。
    function copyPath() {
        const result = skills.copyPath(card.skillFilePath)
        if (result.ok)
            workbench.notify("success", qsTr("Path copied"),
                             card.pathText)
        else
            workbench.notify("error", qsTr("Copy failed"), result.error)
    }

    // 右键菜单：复制路径/SKILL.md/名称、打开所在文件夹、定位文件。
    Menu {
        id: contextMenu
        MenuItem {
            text: qsTr("Copy path")
            onTriggered: card.copyPath()
        }
        MenuItem {
            text: qsTr("Copy SKILL.md path")
            onTriggered: {
                const result = skills.copySkillFile(card.skillFilePath)
                if (result.ok)
                    workbench.notify("success", qsTr("Path copied"),
                                     card.skillFilePath)
                else
                    workbench.notify("error", qsTr("Copy failed"),
                                     result.error)
            }
        }
        MenuItem {
            text: qsTr("Copy name")
            onTriggered: {
                const result = skills.copyName(card.skillFilePath)
                if (result.ok)
                    workbench.notify("success", qsTr("Copied"),
                                     card.skillName)
                else
                    workbench.notify("error", qsTr("Copy failed"),
                                     result.error)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Open containing folder")
            onTriggered: {
                const result = skills.openFolder(card.skillFilePath)
                if (!result.ok)
                    workbench.notify("error", qsTr("Cannot open folder"),
                                     result.error)
            }
        }
        MenuItem {
            text: qsTr("Reveal SKILL.md")
            onTriggered: {
                const result = skills.revealSkillFile(card.skillFilePath)
                if (!result.ok)
                    workbench.notify("error", qsTr("Cannot open folder"),
                                     result.error)
            }
        }
    }

    // 详情 flyout：默认落在卡片右下方，出屏时翻转（openFlyout 定位）。
    SkillDetailFlyout {
        id: flyout
        skillFilePath: card.skillFilePath
        x: card.width + theme.spacingM
        y: 0
    }
}
