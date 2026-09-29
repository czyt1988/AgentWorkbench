import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The Skills page: search, source facets, sort, the card
// grid, skeleton while scanning, and the partial-failure footer.
Item {
    id: page

    readonly property var kinds: ["agents", "claude", "codex", "plugin",
                                  "project", "custom"]

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

        // --- Toolbar: search + facets + sort --------------------
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

            ComboBox {
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

        // --- First-scan skeleton (no cached data yet) ------------------
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

                    // Subtle shimmer via a slow pulse.
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

        // --- Empty state ----------------------------------------------------
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

        // Filtered to empty (some skills exist, none match).
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

        // --- Grid ------------------------------------------------------------
        // 已有数据（缓存恢复）时扫描中也不隐藏网格：后台重扫静默进行，
        // 结果落地后模型整体刷新。
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
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

        // --- Footer: stats + partial failures ---------------------
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
