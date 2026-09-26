import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// The workspace host: one page at a time, loaded by source from the
// navigation model (specs/01 §8.3). Switching destroys the previous page;
// state that must survive lives in C++.
Rectangle {
    id: workspace

    color: theme.workspaceBg

    Loader {
        id: pageLoader
        anchors.fill: parent
        source: nav.currentPage.source !== undefined
                && nav.currentPage.source.length > 0
                ? nav.currentPage.source : ""
        onStatusChanged: {
            if (status === Loader.Error) {
                console.error("Workspace: failed to load page",
                              source)
                workbench.notify("error", qsTr("Page failed to load"),
                                 String(source))
            }
        }
    }

    // Nothing selected yet (startup restores the last page, so this is a
    // transient state).
    ColumnLayout {
        anchors.centerIn: parent
        visible: pageLoader.status !== Loader.Ready
        spacing: theme.spacingM

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Pick a page from the sidebar")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeSubtitle
        }
    }
}
