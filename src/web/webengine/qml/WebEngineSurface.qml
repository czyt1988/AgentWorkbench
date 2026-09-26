import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtWebEngine
import AgentWorkbench
import AgentWorkbench.App

// The embedded view for one tab (specs/02 §6.3/§6.4): the WebEngineView
// plus its state overlays. All platform events (popups, downloads,
// fullscreen, crashes, permissions) are handled here — "no click does
// nothing" (specs/03 S5-T4).
Item {
    id: surface

    // The WebTab this view renders (a role of the tabs model).
    property var tab: null
    // Fullscreen hides the tab bar (02 §6.4); Esc leaves it first.
    signal fullScreenToggled(bool active)

    readonly property bool hasTab: tab !== null && tab !== undefined

    WebEngineView {
        id: view
        anchors.fill: parent

        visible: surface.hasTab && tab.state !== "released"
                 && tab.state !== "offline"
        url: surface.hasTab ? tab.url : ""
        zoomFactor: surface.hasTab ? tab.zoom : 1.0
        profile: surface.hasTab
                 ? WebProfiles.createProfile(tab.agentId) : null

        // Memory policy (02 §6.5): only the active tab keeps a live view;
        // the rest freeze (session kept) until released by the LRU.
        lifecycleState: {
            if (!web.freezeInactiveTabs)
                return WebEngineView.Active
            return surface.visible && web.activeTabId === tab.id
                   && tab.state === "ready"
                   ? WebEngineView.Active : WebEngineView.Frozen
        }

        onUrlChanged: {
            if (surface.hasTab)
                web.setTabUrl(tab.id, String(url))
        }
        onTitleChanged: {
            if (surface.hasTab)
                web.setTabTitle(tab.id, title)
        }
        onLoadProgressChanged: {
            if (surface.hasTab)
                web.setTabProgress(tab.id, loadProgress)
        }
        // Qt 6 has no loadFinished — load results arrive via loadingChanged
        // with a LoadStatus enum (02 §6.3 state machine).
        onLoadingChanged: function(loadingInfo) {
            if (!surface.hasTab)
                return
            if (loadingInfo.status === WebEngineView.LoadSucceededStatus) {
                web.setTabProgress(tab.id, 100)
                web.setTabState(tab.id, "ready")
            } else if (loadingInfo.status === WebEngineView.LoadFailedStatus) {
                console.error("WebEngine: failed to load",
                              String(loadingInfo.url))
                web.setTabLastError(tab.id, qsTr("Failed to load %1")
                                               .arg(String(loadingInfo.url)))
                web.setTabState(tab.id, "error")
            }
        }
        // Do NOT auto-reload after a renderer crash — crash loops are worse
        // than a manual reload (02 §6.4).
        onRenderProcessTerminated: function(status, exitCode) {
            console.error("WebEngine: render process terminated", status,
                          exitCode)
            if (surface.hasTab) {
                web.setTabLastError(tab.id, qsTr("The render process was terminated (code %1)").arg(exitCode))
                web.setTabState(tab.id, "crashed")
            }
        }

        // --- Popups: loopback -> new in-app tab; anything else -> system
        // browser (02 §6.4). request.accepted is mandatory or the request
        // fails silently.
        onNewWindowRequested: function(request) {
            request.accepted = true
            const target = String(request.requestedUrl)
            const hostMatch = /^https?:\/\/([^\/?#:]+)/.exec(target)
            const host = hostMatch ? hostMatch[1].toLowerCase() : ""
            const loopback = host === "127.0.0.1" || host === "localhost"
                             || host === "::1" || host.startsWith("127.")
            if (loopback) {
                web.openDetachedTab(tab.agentId, target, host)
            } else {
                workbench.openExternalUrl(target)
            }
        }

        // --- Fullscreen: accept and let the page hide its tab bar.
        onFullScreenRequested: function(request) {
            request.accepted = true
            surface.fullScreenToggled(request.fullScreen)
        }

        // --- Permissions: all denied in v1, with a visible notice (02 §6.4).
        onFeaturePermissionRequested: function(securityOrigin, feature) {
            view.rejectFeature(feature)
            workbench.notify("warning", qsTr("Permission denied"),
                             qsTr("This page requested a browser permission; the current version does not support it."))
        }
    }

    // --- DevTools in a separate window (Debug builds only, 02 §6.6) --------
    Window {
        id: devToolsWindow
        width: 900
        height: 600
        visible: false
        title: qsTr("Developer tools")
        color: theme.windowBg

        WebEngineView {
            anchors.fill: parent
            url: devToolsWindow.visible ? view.devToolsUrl : ""
        }
    }

    function openDevTools() {
        devToolsWindow.show()
        devToolsWindow.raise()
        devToolsWindow.requestActivate()
    }

    // --- Downloads (02 §6.4): Qt 6 moved downloadRequested from the view
    // onto the profile — always accepted, into web.downloadDir.
    Connections {
        target: view.profile
        enabled: view.profile !== null
        function onDownloadRequested(download) {
            download.directory = web.downloadDir
            download.accept()
            workbench.notify("info", qsTr("Download started"),
                             download.downloadFileName)
            download.stateChanged.connect(function() {
                if (download.state === WebEngineDownloadRequest.DownloadCompleted) {
                    workbench.notify("success", qsTr("Download finished"),
                                     String(download.path))
                } else if (download.state === WebEngineDownloadRequest.DownloadCancelled
                           || download.state === WebEngineDownloadRequest.DownloadInterrupted) {
                    workbench.notify("warning", qsTr("Download interrupted"),
                                     download.downloadFileName)
                }
            })
        }
    }

    // --- State overlays (02 §6.3) -------------------------------------------
    Rectangle {
        id: overlay
        anchors.fill: parent
        visible: surface.hasTab && (tab.state === "loading"
                                    || tab.state === "offline"
                                    || tab.state === "crashed"
                                    || tab.state === "error")
        color: theme.alpha(theme.windowBg, 0.7)

        ColumnLayout {
            anchors.centerIn: parent
            spacing: theme.spacingM
            width: Math.min(parent.width - 2 * theme.spacingXl, 460)

            // loading -----------------------------------------------------
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: tab.state === "loading"
                running: visible
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: tab.state === "loading"
                text: qsTr("Loading %1...").arg(String(tab.url).replace(/^https?:\/\//, "").split("/")[0])
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: tab.state === "offline"
                text: qsTr("This agent is not running")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: tab.state === "offline"
                text: String(tab.url).replace(/#.*$/, "")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeSmall
                font.family: theme.monoFamily
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: tab.state === "crashed"
                text: qsTr("The page crashed")
                color: theme.danger
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: tab.state === "error"
                text: qsTr("Failed to load the page")
                color: theme.danger
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                visible: (tab.state === "crashed" || tab.state === "error")
                         && tab.lastError.length > 0
                text: tab.lastError
                color: theme.textSecondary
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.WrapAnywhere
                horizontalAlignment: Text.AlignHCenter
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: theme.spacingS

                AButton {
                    visible: tab.state === "loading"
                    text: qsTr("Cancel")
                    onClicked: view.stop()
                }
                AButton {
                    visible: tab.state === "offline"
                    variant: "primary"
                    text: qsTr("Restart agent")
                    onClicked: workbench.launchAgent(tab.agentId)
                }
                AButton {
                    visible: tab.state === "offline" || tab.state === "error"
                    text: qsTr("Retry")
                    onClicked: web.reloadTab(tab.id)
                }
                AButton {
                    visible: tab.state === "crashed" || tab.state === "error"
                    text: qsTr("Reload")
                    onClicked: web.reloadTab(tab.id)
                }
                AButton {
                    visible: tab.state === "crashed" || tab.state === "error"
                    text: qsTr("Open in browser")
                    onClicked: web.openExternal(tab.id)
                }
            }
        }
    }
}
