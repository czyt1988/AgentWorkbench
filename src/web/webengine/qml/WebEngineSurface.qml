import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtWebEngine
import AgentWorkbench
import AgentWorkbench.App

// The embedded view for one tab: the WebEngineView
// plus its state overlays. All platform events (popups, downloads,
// fullscreen, crashes, permissions) are handled here — "no click does
// nothing".
Item {
    id: surface

    // The WebTab this view renders (a role of the tabs model).
    property var tab: null
    // Fullscreen hides the tab bar; Esc leaves it first.
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

        // Memory policy: only the active tab keeps a live view;
        // the rest freeze (session kept) until released by the LRU.
        // A loading view must stay Active — Frozen suspends the page, which
        // can stall an in-flight load mid-way.
        // LifecycleState is a SCOPED enum: WebEngineView.Active would be
        // undefined and the assignment silently no-op every time.
        lifecycleState: {
            if (!web.freezeInactiveTabs)
                return WebEngineView.LifecycleState.Active
            return surface.visible && web.activeTabId === tab.id
                   && (tab.state === "loading" || tab.state === "ready")
                   ? WebEngineView.LifecycleState.Active
                   : WebEngineView.LifecycleState.Frozen
        }

        onUrlChanged: {
            if (surface.hasTab)
                web.setTabUrl(tab.id, String(url))
        }
        // The tab state machine drives loads: `loading` means "should be
        // loading". reloadTab()/markOnlineForAgent() flip the state WITHOUT
        // touching the URL (the url binding above only fires on url
        // changes), so the transition to loading must trigger the actual
        // load from here. Without this the spinner ran forever and the
        // overlay's Cancel had nothing to stop — view.stop() on an idle
        // view never emits LoadStoppedStatus.
        Connections {
            target: surface.tab
            function onStateChanged() {
                if (surface.hasTab && tab.state === "loading")
                    view.reload()
            }
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
        // with a LoadStatus enum (state machine).
        onLoadingChanged: function(loadingInfo) {
            if (!surface.hasTab)
                return
            if (loadingInfo.status === WebEngineView.LoadSucceededStatus) {
                web.setTabProgress(tab.id, 100)
                web.setTabState(tab.id, "ready")
            } else if (loadingInfo.status === WebEngineView.LoadFailedStatus) {
                console.error("WebEngine: failed to load",
                              String(loadingInfo.url),
                              "domain:", loadingInfo.errorDomain,
                              "code:", loadingInfo.errorCode)
                // A positive error code is the HTTP status (e.g. 401 from a
                // token-gated harness); net errors are negative.
                web.setTabLastError(tab.id,
                    loadingInfo.errorCode > 0
                        ? qsTr("Failed to load %1 (HTTP %2)")
                              .arg(String(loadingInfo.url))
                              .arg(loadingInfo.errorCode)
                        : qsTr("Failed to load %1")
                              .arg(String(loadingInfo.url)))
                web.setTabState(tab.id, "error")
            } else if (loadingInfo.status === WebEngineView.LoadStoppedStatus) {
                // A deliberate stop (toolbar ✕ / overlay Cancel) settles the
                // state machine — otherwise the spinner runs forever.
                web.setTabState(tab.id, "ready")
            }
        }
        // Do NOT auto-reload after a renderer crash — crash loops are worse
        // than a manual reload.
        onRenderProcessTerminated: function(status, exitCode) {
            console.error("WebEngine: render process terminated", status,
                          exitCode)
            if (surface.hasTab) {
                web.setTabLastError(tab.id, qsTr("The render process was terminated (code %1)").arg(exitCode))
                web.setTabState(tab.id, "crashed")
            }
        }

        // --- Popups: loopback -> new in-app tab; anything else -> system
        // browser. request.accepted is mandatory or the request
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

        // --- JS alert()/confirm()/prompt(): the engine blocks the page's
        // JS until answered. Without a handler Qt shows its own dialog, but
        // if that dialog is dismissed in a way that never answers, every
        // later interaction silently stalls. Handle it ourselves: a themed
        // dialog that ALWAYS answers (OK / Cancel), so the page can never
        // stay blocked.
        onJavaScriptDialogRequested: function(request) {
            request.accepted = true
            jsDialog.request = request
            jsDialog.open()
        }

        // --- HTTP basic auth (opencode's login prompt): same shape as the
        // JS dialog — always answers, Cancel rejects.
        onAuthenticationDialogRequested: function(request) {
            request.accepted = true
            authDialog.request = request
            authDialog.open()
        }

        // --- window.close() from the page closes the tab (the page asked
        // for it). Previously the request was silently ignored and the
        // stuck page could not be dismissed.
        onWindowCloseRequested: {
            if (surface.hasTab)
                web.closeTab(tab.id)
        }

        // --- Permissions: all denied in v1, with a visible notice.
        onFeaturePermissionRequested: function(securityOrigin, feature) {
            view.rejectFeature(feature)
            workbench.notify("warning", qsTr("Permission denied"),
                             qsTr("This page requested a browser permission; the current version does not support it."))
        }
    }

    // --- JS alert()/confirm()/prompt() ------------------------------------
    // Themed replacement for the engine's default dialog. IMPORTANT: while
    // open, the page's JS is blocked, so the buttons must ALWAYS answer the
    // request — closing this popup via the dialog's own close handling
    // (e.g. the closePolicy below) must also reject, never leave the
    // request dangling. That is why closePolicy is NoAutoClose and only
    // these two buttons settle it.
    Popup {
        id: jsDialog

        property var request: null
        // alert() has no Cancel; prompt() has an input field.
        readonly property bool isAlert:
            request && request.type === JavaScriptDialogRequest.DialogTypeAlert
        readonly property bool isPrompt:
            request && request.type === JavaScriptDialogRequest.DialogTypePrompt

        function settle(accept) {
            if (!request)
                return
            if (accept) {
                if (isPrompt)
                    request.dialogAccept(promptField.text)
                else
                    request.dialogAccept()
            } else {
                request.dialogReject()
            }
            request = null
            close()
        }

        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        width: 380
        padding: theme.spacingL

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.accent
            border.width: 1
            radius: theme.radiusOverlay
        }

        onOpened: {
            promptField.text = request ? request.defaultText : ""
            if (isPrompt)
                promptField.forceActiveFocus()
        }

        ColumnLayout {
            width: jsDialog.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: jsDialog.request ? jsDialog.request.title : ""
                color: theme.accent
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: jsDialog.request ? jsDialog.request.message : ""
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            TextField {
                id: promptField
                Layout.fillWidth: true
                visible: jsDialog.isPrompt
                color: theme.textPrimary
                placeholderTextColor: theme.textMuted
                font.pixelSize: theme.fontSizeBody
                background: Rectangle {
                    radius: theme.radiusControl
                    color: theme.surfaceAltBg
                    border.color: promptField.activeFocus
                                  ? theme.focusRing : theme.borderSubtle
                    border.width: promptField.activeFocus ? 2 : 1
                }
                onAccepted: jsDialog.settle(true)
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Item { Layout.fillWidth: true }
                AButton {
                    visible: !jsDialog.isAlert
                    text: qsTr("Cancel")
                    onClicked: jsDialog.settle(false)
                }
                AButton {
                    variant: "primary"
                    // confirm() semantics: OK returns true.
                    text: qsTr("OK")
                    onClicked: jsDialog.settle(true)
                }
            }
        }
    }

    // --- HTTP basic / proxy auth ------------------------------------------
    // opencode-style logins: the engine blocks the page until credentials
    // arrive. Same rule as the JS dialog: always answer.
    Popup {
        id: authDialog

        property var request: null

        function settle(accept) {
            if (!request)
                return
            if (accept)
                request.dialogAccept(userField.text, passwordField.text)
            else
                request.dialogReject()
            request = null
            close()
        }

        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        width: 380
        padding: theme.spacingL

        background: Rectangle {
            color: theme.overlayBg
            border.color: theme.accent
            border.width: 1
            radius: theme.radiusOverlay
        }

        onOpened: {
            userField.text = ""
            passwordField.text = ""
            userField.forceActiveFocus()
        }

        ColumnLayout {
            width: authDialog.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Sign in")
                color: theme.accent
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: authDialog.request && authDialog.request.realm.length > 0
                      ? qsTr("The site \"%1\" requires authentication.")
                        .arg(authDialog.request.realm)
                      : qsTr("This site requires authentication.")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            TextField {
                id: userField
                Layout.fillWidth: true
                placeholderText: qsTr("Username")
                color: theme.textPrimary
                placeholderTextColor: theme.textMuted
                font.pixelSize: theme.fontSizeBody
                background: Rectangle {
                    radius: theme.radiusControl
                    color: theme.surfaceAltBg
                    border.color: userField.activeFocus
                                  ? theme.focusRing : theme.borderSubtle
                    border.width: userField.activeFocus ? 2 : 1
                }
                onAccepted: passwordField.forceActiveFocus()
            }
            TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                color: theme.textPrimary
                placeholderTextColor: theme.textMuted
                font.pixelSize: theme.fontSizeBody
                background: Rectangle {
                    radius: theme.radiusControl
                    color: theme.surfaceAltBg
                    border.color: passwordField.activeFocus
                                  ? theme.focusRing : theme.borderSubtle
                    border.width: passwordField.activeFocus ? 2 : 1
                }
                onAccepted: authDialog.settle(true)
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Item { Layout.fillWidth: true }
                AButton {
                    text: qsTr("Cancel")
                    onClicked: authDialog.settle(false)
                }
                AButton {
                    variant: "primary"
                    text: qsTr("Sign in")
                    onClicked: authDialog.settle(true)
                }
            }
        }
    }

    // --- DevTools in a separate window (Debug builds only) --------
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

    // Toolbar "stop loading" (⟳ 重载/✕ 停止加载).
    function stopLoading() {
        view.stop()
    }

    // --- Downloads: Qt 6 moved downloadRequested from the view
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

    // --- State overlays -------------------------------------------
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
