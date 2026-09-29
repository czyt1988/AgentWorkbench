import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgentWorkbench
import AgentWorkbench.App

// 单张 skill 卡片（玻璃卡）：规格 340x160（网格内宽度随 cell 拉伸）。玻璃
// 质感配方与 AgentCard 同源（见 designs.md「卡片样式」）：半透明底、accent
// 极淡纱层、内缘 1px 高光、ASpotlight 悬停聚光，悬停时整卡升举、按压回落。
// 交互沿旧版：左键复制目录路径，右键弹出复制/打开动作，悬停 400ms 打开
// 详情 flyout（SkillDetailFlyout，Loader 惰性创建）。剪贴板与文件操作经
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

    // Qt 的 hover 是独占投递：指针下第一个接受 hover 的子项会截住整棵子树的事件。
    // 卡内的复制按钮是 Control（hoverEnabled 默认取 styleHints.useHoverEffects，
    // Windows 上为 true），会把卡片的 hover 独占走——把它的 hovered 并进同一个
    // 判据，鼠标移到复制图标上时卡片悬停态才不会闪断（高亮消失、浮层被收起）。
    readonly property bool hovered: hoverHandler.hovered || copyButton.hovered

    // 网格外独立使用时的兜底尺寸；网格内由 delegate 显式覆盖
    // （cellWidth - spacingL × cardHeight，见 SkillGridPage）。
    width: theme.cardMinWidth + 80 // 规格宽度 340
    height: 160

    // --- 悬停升举 / 按压回落 -------------------------------------------
    // 只用视觉变换（transform + z），不动 x/y 本体——GridView 的 cell 定位
    // 写的就是 delegate 的 x/y，动了会跟视图打架。升举幅度压在 cell 间距
    // （spacingL）之内，不会盖住相邻卡片；z 让升举中的卡压过右/下侧邻居
    // （聚光与边框流光会越出卡缘）。按压时落回原位给出「按下去」的手感。
    z: hovered ? 1 : 0
    transform: Translate {
        y: card.hovered && !mouseArea.pressed ? -theme.spacingXs : 0
        Behavior on y {
            NumberAnimation {
                duration: theme.durationFast
                easing.type: Easing.OutCubic
            }
        }
    }

    // 卡片是模型 delegate：搜索/过滤/重扫描会在悬停中销毁它们，而
    // ToolTip 附加属性在每窗口只共享一个可视化 tooltip。悬停宿主死掉时
    // 它的 `ToolTip.visible` 绑定跟着死，再没有人去隐藏那个共享 tooltip
    // ——它会冻在屏幕上。销毁时 hide；每个 tooltip 还带 timeout，漏网的
    // 情况也能自愈。
    Component.onDestruction: ToolTip.hide()

    // 网格回收复用（reuseItems）时 skill 会换人：停掉待开的浮层计时并
    // 收起已开的浮层，旧卡的悬停状态不跟着搬到新位置。
    onSkillFilePathChanged: {
        hoverTimer.stop()
        card.closeFlyoutNow()
    }

    // --- 悬停 flyout（400ms 延时，Loader 惰性创建）---------------------
    // 悬停满 400ms 才打开，避免扫过卡片就闪出详情。浮层经 Loader 按需
    // 创建：未被悬停过的卡不付出 Popup 全价（整页可达百卡，浮层是建卡
    // 成本的大头），首次悬停同步创建后常驻复用。
    Loader {
        id: flyoutLoader
        active: false
        sourceComponent: SkillDetailFlyout {
            skillFilePath: card.skillFilePath
            x: card.width + theme.spacingM
            y: 0
        }
    }

    // Loader 代理函数：浮层未创建时安全短路（回收/竞态路径也会走到）。
    function closeFlyoutNow() {
        if (flyoutLoader.item)
            flyoutLoader.item.close()
    }
    function closeFlyoutLater() {
        if (flyoutLoader.item)
            flyoutLoader.item.tryCloseLater()
    }
    function cancelFlyoutClose() {
        if (flyoutLoader.item)
            flyoutLoader.item.cancelClose()
    }

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
        if (!flyoutLoader.active)
            flyoutLoader.active = true
        const flyout = flyoutLoader.item
        if (!flyout)
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

        // 玻璃底：半透明（页面底衬光斑可透）+ hover 整体提亮一档
        // （theme.hover 在深色主题提亮、浅色主题压暗）。skill 卡没有
        // per-卡语义色（AgentCard 的 tint 来源），统一中性玻璃。
        color: theme.alpha(card.hovered ? theme.hover(background.glassBase)
                                        : background.glassBase,
                           card.hovered ? 0.78 : 0.62)
        readonly property color glassBase: theme.surfaceBg
        border.width: 1
        border.color: card.hovered ? theme.borderStrong : theme.borderSubtle
        Behavior on color { ColorAnimation { duration: theme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: theme.durationFast } }

        // accent 极淡纱（玻璃厚度感）：复刻 AgentCard 的场景色罩层配方、
        // 密度减半——整页可达百卡且共用一个 accent，全密度会整面泛色。
        // 纵向 Gradient 近似参照实现的斜向纱。
        Rectangle {
            anchors.fill: parent
            radius: background.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.alpha(theme.accent, 0.05) }
                GradientStop { position: 0.55; color: theme.alpha(theme.accent, 0.01) }
                GradientStop { position: 1.0; color: theme.alpha(theme.accent, 0.03) }
            }
        }
        // 悬停加深一档的纱（opacity 过渡，合成器免费）。
        Rectangle {
            anchors.fill: parent
            radius: background.radius
            opacity: card.hovered ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: theme.durationNormal } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.alpha(theme.accent, 0.10) }
                GradientStop { position: 0.55; color: theme.alpha(theme.accent, 0.03) }
                GradientStop { position: 1.0; color: theme.alpha(theme.accent, 0.06) }
            }
        }

        // 内缘 1px 高光：玻璃厚度感（参照实现的 inset 0 1px 0，扩成整圈
        // 内缘）。深色主题取 textOnAccent 的微白；浅色主题下白高光不可
        // 见、留空即可。
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(0, background.radius - 1)
            color: "transparent"
            border.width: 1
            border.color: theme.variant === "dark" ? theme.alpha(theme.textOnAccent, 0.07)
                                                   : "transparent"
        }

        // 悬停聚光：光斑跟随指针 + 描边流光（契约见 ASpotlight 头注释），
        // 叠在玻璃底/纱层之上、内容子项之下。
        ASpotlight {
            anchors.fill: parent
            radius: background.radius
            accentColor: theme.accent
            active: card.hovered
            spotX: hoverHandler.point.position.x
            spotY: hoverHandler.point.position.y
        }

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
                    // 这里用被动的 HoverHandler，而不是带 hoverEnabled 的
                    // MouseArea：后者只为 tooltip 存在，却会独占 hover、让整张
                    // 卡片丢掉悬停态（高亮闪断、浮层被收起）。写法与 APill、
                    // ACard 一致。
                    HoverHandler { id: pathHover }
                    ToolTip.visible: pathHover.hovered
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                    ToolTip.text: card.pathText
                }
                AIconButton {
                    id: copyButton
                    iconSource: "qrc:/icons/copy.svg"
                    tooltip: qsTr("Copy path")
                    onClicked: card.copyPath()
                }
            }
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    // 悬停进入 → 400ms 后开浮层；离开 → 收起。判据是卡片自己的 hovered
    // （已并进卡内独占 hover 的子项），不是 hoverHandler.hovered。进入时
    // 先取消浮层待定的延迟关闭：从浮层挪回卡片的场景里，浮层的 300ms
    // 关闭计时已在跑，不取消会先关再开闪一下。
    onHoveredChanged: {
        if (hovered) {
            cancelFlyoutClose()
            hoverTimer.start()
        } else {
            hoverTimer.stop()
            closeFlyoutLater()
        }
    }

    // 整卡的点击 / 右键 / 滚轮入口。不要给它开 hoverEnabled：hover 是独占投递，
    // 它作为卡片最上层的子项会截住整棵子树的 hover，卡片的 HoverHandler（悬停
    // 高亮 + 400ms 浮层）和卡内所有 tooltip 都会静默失效——点击后浮层能出来只是
    // 因为走的是焦点路径（onPressed → forceActiveFocus → openFlyout）。点击与
    // 滚轮都不依赖它：滚轮走 pointerTargets 命中指针下的项，与 hover 无关。
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: card.forceActiveFocus()
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                contextMenu.popup()
            else
                card.copyPath()
        }
        // 滚轮划过卡片时立即收起 flyout（在别处滚动由上方的 hover-out 路径
        // 覆盖）。wheel.accepted 置 false 让事件继续传给背后的 GridView——
        // 不用 WheelHandler.blocking 做这件事：该属性 Qt 6.2 才引入，Qt 5 下
        // 对它赋值会让整个 SkillCard 连同 Skills 页加载失败。
        onWheel: function(wheel) {
            card.closeFlyoutNow()
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
            card.closeFlyoutLater()
    }
    Keys.onReturnPressed: copyPath()
    Keys.onEnterPressed: copyPath()
    Keys.onPressed: function(event) {
        // 任意按键先关掉 flyout；再按一次才生效。
        if (flyoutLoader.item && flyoutLoader.item.opened) {
            card.closeFlyoutNow()
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
    AMenu {
        id: contextMenu
        AMenuItem {
            text: qsTr("Copy path")
            onTriggered: card.copyPath()
        }
        AMenuItem {
            text: qsTr("Copy SKILL.md path")
            onTriggered: {
                const result = skills.copySkillFile(card.skillFilePath)
                if (result.ok)
                    workbench.notify("success", qsTr("Path copied"),
                                     card.skillFilePath)
                else
                    workbench.notify("error", qsTr("Copy failed"), result.error)
            }
        }
        AMenuItem {
            text: qsTr("Copy name")
            onTriggered: {
                const result = skills.copyName(card.skillFilePath)
                if (result.ok)
                    workbench.notify("success", qsTr("Copied"),
                                     card.skillName)
                else
                    workbench.notify("error", qsTr("Copy failed"), result.error)
            }
        }
        AMenuSeparator {}
        AMenuItem {
            text: qsTr("Open containing folder")
            onTriggered: {
                const result = skills.openFolder(card.skillFilePath)
                if (!result.ok)
                    workbench.notify("error", qsTr("Cannot open folder"),
                                     result.error)
            }
        }
        AMenuItem {
            text: qsTr("Reveal SKILL.md")
            onTriggered: {
                const result = skills.revealSkillFile(card.skillFilePath)
                if (!result.ok)
                    workbench.notify("error", qsTr("Cannot open folder"),
                                     result.error)
            }
        }
    }
}
