import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Skills 主页：搜索、来源 facet 过滤、排序、玻璃卡片网格，扫描期的骨架屏
// 与部分失败的尾部提示。列表状态在 skills.model（C++ 侧），页面本身可毁
// 掉重建。网格用 GridView 而非 Flow+Repeater：skill 可达百级，惰性实例化
// 是切页不卡的前提（见下方网格注释）。
Item {
    id: page

    // 卡片规格宽（= SkillCard 的兜底宽；网格内实际宽度由 cell 拉伸覆盖）。
    readonly property real cardMinWidth: theme.cardMinWidth + 80
    // 卡片规格高（与 SkillCard 的兜底高一致，cellHeight 由此推导）。
    readonly property real cardHeight: 160

    // facet 过滤的全部来源类型（“全部”按钮单独处理，不在此列）。
    readonly property var kinds: ["agents", "claude", "codex", "plugin",
                                  "project", "custom"]

    // 切换一个来源 facet：空串 = 清空全部；已选中的再点一次取消。
    function applyFacet(kind) {
        if (kind.length === 0) {
            skills.model.activeKinds = []
            return
        }
        let active = skills.model.activeKinds.slice()
        const at = active.indexOf(kind)
        if (at >= 0)
            active.splice(at, 1)
        else
            active.push(kind)
        skills.model.activeKinds = active
    }

    // 页面底衬光斑（玻璃卡透光用），见 AWorkspaceGlow 头注释。
    AWorkspaceGlow {
        anchors.fill: parent
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            title: qsTr("Skills")
            subtitle: qsTr("Local SKILL.md files discovered in the configured roots")

            AButton {
                text: qsTr("Rescan")
                busy: skills.scanning
                onClicked: skills.refresh()
            }
        }

        // --- 工具栏：搜索 + facet + 排序 --------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingM
            spacing: theme.spacingM

            ASearchField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search name or description...")
                onTextChanged: skills.model.searchText = text
            }

            Flow {
                Layout.preferredWidth: implicitWidth
                spacing: theme.spacingXs

                AButton {
                    text: qsTr("All")
                    variant: skills.model.activeKinds.length === 0
                             ? "primary" : "ghost"
                    onClicked: page.applyFacet("")
                }
                Repeater {
                    model: page.kinds
                    delegate: AButton {
                        required property string modelData
                        text: skills.kindLabel(modelData)
                        variant: skills.model.activeKinds.includes(modelData)
                                 ? "primary" : "ghost"
                        onClicked: page.applyFacet(modelData)
                    }
                }
            }

            AComboBox {
                id: sortCombo
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("Sort: name"), value: "name" },
                    { text: qsTr("Sort: modified"), value: "modified" },
                    { text: qsTr("Sort: source"), value: "kind" }
                ]
                onActivated: skills.model.sortMode = currentValue
            }
        }

        // --- 首扫骨架屏（尚无缓存数据） ----------------------------------------
        // 数据经启动时的缓存恢复先行到位；这里的骨架只服务首次启动
        // （无缓存）或缓存为空的扫描期——已有数据时后台重扫不打扰网格。
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingL
            visible: skills.scanning && skills.model.totalCount === 0

            Repeater {
                model: 6
                delegate: Rectangle {
                    required property int index
                    width: theme.cardMinWidth + 80
                    height: 160
                    radius: theme.radiusCard
                    color: theme.surfaceBg
                    opacity: 0.5 + 0.2 * Math.sin(index)

                    // 缓慢的明暗脉冲，做成轻微的 shimmer 动效。
                    SequentialAnimation on opacity {
                        running: visible && skills.scanning
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.8; duration: 600 }
                        NumberAnimation { to: 0.4; duration: 600 }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingS
            visible: skills.scanning && skills.model.totalCount === 0
            text: qsTr("Scanning skill directories for the first time...")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
        }

        // --- 空状态 ------------------------------------------------------------
        // 完全没有 skill（根目录里一个 SKILL.md 都没有）。
        AEmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !skills.scanning && skills.model.count === 0
                     && skills.model.totalCount === 0
            iconSource: "qrc:/icons/skills.svg"
            title: qsTr("No skills found")
            description: qsTr("No SKILL.md files were found in the scanned roots. Add a directory in Settings.")
            actionText: qsTr("Open settings")
            onActionClicked: workbench.showPage("settings")
        }

        // 过滤后为空（有 skill，但都不命中当前条件）。
        AEmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !skills.scanning && skills.model.count === 0
                     && skills.model.totalCount > 0
            iconSource: "qrc:/icons/search.svg"
            title: qsTr("No matching skills")
            description: qsTr("No skill matches the current search or facet filters.")
            actionText: qsTr("Clear filters")
            onActionClicked: {
                searchField.text = ""
                skills.model.activeKinds = []
            }
        }

        // --- 网格 ----------------------------------------------------------------
        // 已有数据（缓存恢复）时扫描中也不隐藏网格：后台重扫静默进行，
        // 结果落地后模型整体刷新。
        //
        // GridView 是性能前提，不是风格选择：skill 可达百级，Flow+Repeater
        // 会在页面创建时同步实例化全部 delegate（每张卡各带一份详情浮层
        // 与右键菜单），实测 151 个 skill 换页冻结约 2s；GridView 只实例化
        // 视口内的卡片，cacheBuffer 额外预取约两行，reuseItems 让滚出视口
        // 的卡片回池复用。列数随视口宽度自适应，cellWidth 取整防浮点误差
        // 多算一列（GridView 按整数除法算列，溢出会出横向滚动）。
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            // 与工具栏行的左右留白对齐：缺了这两行，首张卡片会贴着
            // 侧栏，和上面的搜索框、标题错开一整个 spacingL。
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            visible: skills.model.count > 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AScrollBar {}

            // 列数 = 视口宽能容纳几列「最小卡宽 + 一列间距」。
            readonly property int columns: Math.max(1, Math.floor(
                (width + theme.spacingL) / (page.cardMinWidth + theme.spacingL)))
            // cell = 卡 + 右侧一列间距；先从视口宽里扣掉每列的间距再整除，
            // 保证 columns*cellWidth ≤ width（浮点偏高会溢出，出横向滚动）。
            // 下限 spacingL：视口极窄时 cellWidth 仍 ≥ 一列间距，卡宽
            // （cellWidth - spacingL）不为负。
            cellWidth: Math.max(theme.spacingL,
                                Math.floor((width - columns * theme.spacingL) / columns)
                                + theme.spacingL)
            // cell = 卡高 + 底部一行间距；与 SkillCard 的兜底高同步维护。
            cellHeight: page.cardHeight + theme.spacingL

            // 视口外上下各预取约两行，快速滚动少出现空白。
            cacheBuffer: 360
            // 滚出视口的卡回池复用：滚动不再反复建卡销卡；模型 reset
            // （过滤/重扫）时仍整批销毁重建。
            reuseItems: true

            // 首行卡片悬停升举（SkillCard 的 -spacingXs 平移）需要一格
            // 顶部余量，否则 contentY=0 时被视口裁掉上缘。
            header: Item { height: theme.spacingXs }

            model: skills.model
            delegate: SkillCard {
                width: grid.cellWidth - theme.spacingL
                height: page.cardHeight
                skillFilePath: model.skillFilePath
                skillName: model.name
                description: model.description
                dirPath: model.dirPath
                kind: model.kind
                rootLabel: model.rootLabel
                pluginVersion: model.pluginVersion
            }
        }

        // --- 尾部：统计与部分失败的提示 --------------------------------------------
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            Layout.bottomMargin: theme.spacingS
            visible: text.length > 0
            text: skills.statsText
            color: skills.partialFailure ? theme.warning : theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            elide: Text.ElideRight
        }
    }
}
