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
Rectangle {
    id: sidebar

    // 折叠态：由 MainWindow 与 ShellController 保持同步。
    property bool collapsed: false

    color: theme.sidebarBg
    // 展开宽度：取设置里的 window.sidebarWidth（ShellController 持有），
    // 键被显式清空（0）时回退主题令牌，两者默认都是 240。
    // 用 implicitWidth 而不是 width：本项由 MainWindow 的 RowLayout 接管，
    // 布局只跟随隐式尺寸的变化重新分配——子项绕过布局直改 width 会让
    // 工作区冻在旧尺寸，折叠后的侧栏旁留出一条空隙。
    implicitWidth: collapsed ? theme.sidebarCollapsedWidth
                             : (shell.sidebarWidth > 0 ? shell.sidebarWidth
                                                       : theme.sidebarWidth)

    Behavior on implicitWidth {
        NumberAnimation { duration: theme.durationNormal }
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
}
