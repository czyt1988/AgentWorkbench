import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 侧栏：只回答「去哪里」，永远不放业务动作。按 section 分组渲染导航模型
// （含徽标、键盘序号），底部是系统页图标按钮 + 折叠手柄。
//
// 布局（固定结构，见 designs.md）：顶部直接开始工作流页（main/extensions
// 区，不再放应用图标 + 名称的 header——那是窗口标题栏的职责）；系统页
// （system 区，如 Settings）以纯图标按钮**钉在侧栏最底部**，与折叠手柄
// 同排——无论列表多长都留在底部。
//
// 根是 Item 而非 Rectangle：侧栏是一块毛玻璃面板，视觉本体分层声明在
// 子项里（光斑底衬 → 半透明玻璃底 → 反光纱 → 内容），内容与拖拽手柄
// 层叠其上。
Item {
    id: sidebar

    // 折叠态：由 MainWindow 与 ShellController 保持同步。
    property bool collapsed: false

    // 拖拽手柄的进行时状态：resizing 期间宽度用 pendingWidth（本地值，
    // 不落盘），抬起时经 shell.setSidebarWidth 一次性提交并持久化。
    property bool resizing: false
    property int pendingWidth: 0

    // 展开宽度：取设置里的 window.sidebarWidth（ShellController 持有），
    // 键被显式清空（0）时回退主题令牌，两者默认都是 240；拖拽期间用
    // pendingWidth 即时贴住指针。
    // 用 implicitWidth 而不是 width：本项由 MainWindow 的 RowLayout 接管，
    // 布局只跟随隐式尺寸的变化重新分配——子项绕过布局直改 width 会让
    // 工作区冻在旧尺寸，折叠后的侧栏旁留出一条空隙。
    implicitWidth: collapsed ? theme.sidebarCollapsedWidth
                             : (resizing ? pendingWidth
                                         : (shell.sidebarWidth > 0
                                            ? shell.sidebarWidth
                                            : theme.sidebarWidth))

    Behavior on implicitWidth {
        // 拖拽期间禁用：宽度必须逐帧贴住指针，动画的平滑延迟会让拖拽
        // 手感发"皮"。
        enabled: !sidebar.resizing
        NumberAnimation { duration: theme.durationNormal }
    }

    // --- 玻璃面板（毛玻璃近似，配方同 designs.md §2.3 / §3.1） -----------
    // Qt Quick 没有 backdrop blur、DWM 亚克力是全窗口效果，真模糊不可
    // 移植；「半透明 + 底衬光斑 + 反光纱」是既定的折衷近似（AMenu 同款
    // 结论）。层次自下而上：光斑（玻璃「透」的光源）→ 半透明玻璃底
    // （光斑经它隐约透出）→ 顶部反光纱 → 内容。透明度深色低浅色高：
    // 深色下光斑透得多一点才可见，浅色下文字底需要更实的衬底。
    AWorkspaceGlow {
        anchors.fill: parent
    }

    Rectangle {
        anchors.fill: parent
        color: theme.alpha(theme.sidebarBg,
                           theme.variant === "dark" ? 0.72 : 0.88)

        // 顶部反光纱：上半区一层极淡的纵向渐变，玻璃的反光感。
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: theme.alpha(theme.textOnAccent, 0.05)
                }
                GradientStop {
                    position: 0.35
                    color: theme.alpha(theme.textOnAccent, 0.0)
                }
            }
        }
    }

    // 右缘分界线：玻璃底半透明后，侧栏与工作区的纯色差变弱，显式画一条
    // 竖线分隔；深色主题在它内侧再叠 1px 微高光做玻璃厚度感（浅色主题
    // 白高光不可见，留空——AgentCard/AMenu 同款规则）。
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: theme.borderSubtle
    }
    Rectangle {
        visible: theme.variant === "dark"
        anchors.right: parent.right
        anchors.rightMargin: 1
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: theme.alpha(theme.textOnAccent, 0.06)
    }

    // 可滚动工作流列表里的一行导航（main/extensions 区）。新区段的
    // 分割线由每个非 main 区的首行画在自己上方；system 页不在这里，
    // 渲染在下方的 footer 里。
    component NavRow: Item {
        id: row

        // 是否为本区段的第一行（用于判断是否需要画分割线）。
        readonly property bool firstInSection:
            index === nav.rowOfFirstInSection(model.section)
        // 需要分割线：非 main 区段的首行上方留出空隙并画线。
        readonly property bool needsDivider:
            firstInSection && model.section !== "main"

        Layout.fillWidth: true
        Layout.preferredHeight: visible ? (36 + (needsDivider ? 9 : 0)) : 0
        visible: model.enabled && model.section !== "system"
        // 行是 delegate（页面插件会来去）——悬停中的行被销毁时不能让
        // 共享 tooltip 冻在屏幕上。
        Component.onDestruction: ToolTip.hide()

        Rectangle {
            visible: row.needsDivider
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: theme.spacingS
            anchors.rightMargin: theme.spacingS
            height: 1
            color: theme.separator
        }

        // 当前项背景 + 3px accent 指示条。
        Rectangle {
            anchors.top: row.needsDivider ? dividerSpace.bottom : parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            // 折叠态：更紧的边距让高亮在窄侧栏里包住居中图标，
            // 保持近似方块的胶囊形。
            anchors.leftMargin: sidebar.collapsed ? theme.spacingXs
                                                  : theme.spacingS
            anchors.rightMargin: sidebar.collapsed ? theme.spacingXs
                                                   : theme.spacingS
            height: 36
            radius: theme.radiusControl
            color: nav.currentPageId === model.pageId ? theme.surfaceBg
                                                      : (rowMouse.containsMouse
                                                         ? theme.surfaceHoverBg
                                                         : "transparent")
            Behavior on color { ColorAnimation { duration: theme.durationFast } }
        }
        Item {
            id: dividerSpace
            visible: false
            anchors.top: parent.top
            height: row.needsDivider ? 9 : 0
        }
        Rectangle {
            anchors.top: row.needsDivider ? dividerSpace.bottom : parent.top
            anchors.left: parent.left
            width: 3
            height: 36
            radius: 1
            visible: nav.currentPageId === model.pageId
            color: theme.accent
        }

        RowLayout {
            anchors.top: row.needsDivider ? dividerSpace.bottom : parent.top
            anchors.left: parent.left
            // 折叠态：24px 图标槽（宽度在下方槽位上定死）在
            // 图标宽度的侧栏里水平居中。
            anchors.leftMargin: sidebar.collapsed
                ? (theme.sidebarCollapsedWidth - 24) / 2
                : theme.spacingM
            anchors.right: parent.right
            anchors.rightMargin: theme.spacingS
            height: 36
            spacing: theme.spacingS

            Item {
                Layout.alignment: Qt.AlignVCenter
                width: 24
                height: 24
                Image {
                    anchors.centerIn: parent
                    source: model.iconSource
                    sourceSize: Qt.size(18, 18)
                    fillMode: Image.PreserveAspectFit
                }
            }
            Label {
                visible: !sidebar.collapsed
                Layout.fillWidth: true
                text: model.title
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                elide: Text.ElideRight
            }
            APill {
                visible: !sidebar.collapsed && model.badgeText.length > 0
                text: model.badgeText
            }
        }

        MouseArea {
            id: rowMouse
            anchors.top: row.needsDivider ? dividerSpace.bottom : parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 36
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: nav.setCurrentPageId(model.pageId)
        }

        // 折叠态：tooltip 显示页面标题（文字被收起时补足说明）。
        ToolTip.visible: sidebar.collapsed && rowMouse.containsMouse
        ToolTip.delay: 300
        ToolTip.timeout: 10000
        ToolTip.text: model.title
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 顶部不再放应用图标 + 名称的 header（与窗口标题栏重复，2026-09
        // 移除），只留一条呼吸空隙让首行导航不贴窗口顶边。
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.spacingS
        }

        // --- 可滚动的工作流列表（main + extensions 区；模型已按
        // section + order 排好，新区段的分割线由各行画在自己上方）-------
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: rowsColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AScrollBar {}

            ColumnLayout {
                id: rowsColumn
                width: flick.width
                spacing: 0

                Repeater {
                    model: nav
                    NavRow {}
                }
            }
        }

        // --- footer：系统页图标 + 折叠手柄 -----------------------------------
        // 系统页（Settings 一类）不进工作流列表，以纯图标按钮渲染（标题由
        // tooltip 承载），与折叠手柄同排。展开：图标在左、手柄在右；
        // 收起：图标堆在手柄上方，全部在图标宽度的侧栏里居中。
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingS
            Layout.rightMargin: theme.spacingS
            height: 1
            color: theme.separator
        }

        Item {
            id: footer

            // 28 = AIconButton 常规隐式尺寸，需与它保持同步；buttonGap
            // 既是收起时垂直堆叠的间距，也是 footer 的边缘留白。
            readonly property int buttonSize: 28
            readonly property int buttonGap: 6

            // system 区的页面数（决定收起时的堆叠高度）。
            readonly property int systemCount: nav.countInSection("system")

            Layout.fillWidth: true
            Layout.preferredHeight: sidebar.collapsed
                ? 2 * footer.buttonGap
                  + footer.systemCount * (footer.buttonSize + footer.buttonGap)
                  + footer.buttonSize
                : 40

            Repeater {
                model: nav
                AIconButton {
                    // 在 system 页里的堆叠序号（模型按 section 排序，
                    // system 在最后）。
                    readonly property int systemIndex:
                        index - nav.rowOfFirstInSection("system")

                    visible: model.section === "system"
                    iconSource: model.iconSource
                    tooltip: model.title
                    active: nav.currentPageId === model.pageId
                    onClicked: nav.setCurrentPageId(model.pageId)
                    // delegate 随模型生灭（页面会来去）——悬停中的按钮被
                    // 销毁时不能让共享 tooltip 冻在屏幕上。
                    Component.onDestruction: ToolTip.hide()

                    x: sidebar.collapsed
                       ? (footer.width - width) / 2
                       : theme.spacingS
                         + systemIndex * (footer.buttonSize + footer.buttonGap)
                    y: sidebar.collapsed
                       ? footer.buttonGap
                         + systemIndex * (footer.buttonSize + footer.buttonGap)
                       : (footer.height - height) / 2
                }
            }

            AIconButton {
                iconSource: sidebar.collapsed ? "qrc:/icons/chevron-right.svg"
                                              : "qrc:/icons/chevron-left.svg"
                tooltip: sidebar.collapsed ? qsTr("Expand sidebar")
                                           : qsTr("Collapse sidebar")
                onClicked: shell.sidebarCollapsed = !shell.sidebarCollapsed

                x: sidebar.collapsed
                   ? (footer.width - width) / 2
                   : footer.width - width - theme.spacingS
                y: sidebar.collapsed
                   ? footer.buttonGap
                     + footer.systemCount * (footer.buttonSize + footer.buttonGap)
                   : (footer.height - height) / 2
            }
        }
    }

    // --- 侧栏右缘的拖拽手柄 -----------------------------------------------
    // 悬停时光标变水平双向箭头，按住拖动调侧栏宽度；极限值由
    // shell.sidebarMinWidth/MaxWidth（180–480）钳制，抬起时经
    // shell.setSidebarWidth 一次性提交（C++ 侧再钳一道并落盘）。声明在
    // 内容之后，压在导航行/页脚的鼠标区上。折叠态隐藏（窄条没有可调
    // 的意义，手柄位置也与折叠按钮冲突）。
    Item {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        visible: !sidebar.collapsed

        // 拖拽/悬停时的竖线提示。
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: 2
            width: 2
            height: 28
            radius: 1
            color: handleArea.containsMouse ? theme.borderStrong
                                            : theme.borderSubtle
            Behavior on color {
                ColorAnimation { duration: theme.durationFast }
            }
        }

        MouseArea {
            id: handleArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor

            // 按下瞬间的侧栏宽度与指针 x。指针位置映射到 sidebar.parent
            // （MainWindow 的 RowLayout）再取差值：父布局的几何不随侧栏
            // 宽度变化，映射结果在拖拽全程稳定——直接用 mouse.x 的话，
            // 手柄自己跟着右缘移动，增量会互相抵消。
            property real widthAtPress: 0
            property real pressX: 0

            onPressed: function(mouse) {
                widthAtPress = sidebar.width
                pressX = mapToItem(sidebar.parent, mouse.x, 0).x
                sidebar.pendingWidth = Math.round(sidebar.width)
                sidebar.resizing = true
            }
            onPositionChanged: function(mouse) {
                if (!sidebar.resizing) {
                    return
                }
                const x = mapToItem(sidebar.parent, mouse.x, 0).x
                const target = widthAtPress + x - pressX
                sidebar.pendingWidth = Math.round(
                        Math.max(shell.sidebarMinWidth,
                                 Math.min(shell.sidebarMaxWidth, target)))
            }
            onReleased: {
                // QML 侧已按同一对边界钳过，这里提交不会再有视觉跳变。
                sidebar.resizing = false
                shell.setSidebarWidth(sidebar.pendingWidth)
                sidebar.pendingWidth = 0
            }
            // 抓取被抢走（窗口失活等）：恢复原宽，不提交半截值。
            onCanceled: {
                sidebar.resizing = false
                sidebar.pendingWidth = 0
            }
        }
    }
}
