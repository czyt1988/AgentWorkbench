import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// The Skills page (specs/02 §7): search, source facets, sort, the card
// grid, skeleton while scanning, and the partial-failure footer.
Item {
    id: page

    readonly property var kinds: ["agents", "claude", "codex", "plugin",
                                  "project", "custom"]

    // Facet labels must go through literal qsTr() calls — qsTr(modelData)
    // is invisible to lupdate and would never be translated.
    function kindLabel(kind) {
        switch (kind) {
        case "agents": return qsTr("Agents")
        case "claude": return qsTr("Claude")
        case "codex": return qsTr("Codex")
        case "plugin": return qsTr("Plugin")
        case "project": return qsTr("Project")
        case "custom": return qsTr("Custom")
        default: return kind
        }
    }

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
                onClicked: skills.refresh()
            }
        }

        // --- Toolbar: search + facets + sort (02 §7.5) --------------------
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
                        text: page.kindLabel(modelData)
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

        // --- Skeleton while scanning (02 §7.5) -----------------------------
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingL
            visible: skills.scanning

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
                        running: skills.scanning
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.8; duration: 600 }
                        NumberAnimation { to: 0.4; duration: 600 }
                    }
                }
            }
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
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !skills.scanning && skills.model.count > 0
            clip: true
            contentWidth: availableWidth

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

        // --- Footer: stats + partial failures (02 §7.5) ---------------------
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

    Component.onCompleted: {
        // First visit scans (async-shaped API: returns immediately).
        if (skills.model.totalCount === 0 && !skills.scanning)
            skills.refresh()
    }
}
