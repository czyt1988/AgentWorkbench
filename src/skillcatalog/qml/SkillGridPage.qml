import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Skills 主页：搜索、来源 facet 过滤、排序、卡片网格，扫描期的骨架屏
// 与部分失败的尾部提示。列表状态在 skills.model（C++ 侧），页面本身
// 可毁掉重建。
Item {
    id: page

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
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            // 与工具栏行的左右留白对齐：缺了这两行，首张卡片会贴着
            // 侧栏，和上面的搜索框、标题错开一整个 spacingL。
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            visible: skills.model.count > 0
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: AScrollBar {}

            Flow {
                width: scrollView.availableWidth
                spacing: theme.spacingL

                Repeater {
                    model: skills.model
                    delegate: SkillCard {
                        skillFilePath: model.skillFilePath
                        skillName: model.name
                        description: model.description
                        dirPath: model.dirPath
                        kind: model.kind
                        rootLabel: model.rootLabel
                        pluginVersion: model.pluginVersion
                    }
                }
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
