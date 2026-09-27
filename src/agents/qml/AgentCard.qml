import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

Item {
    id: root
    height: theme.cardHeight

    // Cards are model delegates; agents CRUD/refilter destroys them while
    // hovered and the shared tooltip's `visible` binding dies with the
    // hovered child — hide it here so it cannot freeze on screen.
    Component.onDestruction: ToolTip.hide()

    // Alias model roles to distinct local properties (avoids shadowing by
    // Rectangle.color etc.).
    property string agentId_p: agentId
    property string name_p: name
    property string icon_p: icon
    property string agentColor: color
    property string cardColor_p: cardColor
    property bool running_p: running
    property bool launching_p: launching
    property bool installed_p: installed
    property string version_p: version
    property bool installing_p: installing
    property string installCommand_p: installCommand
    property string setupCommand_p: setupCommand
    property bool setupping_p: setupping
    property bool setupDone_p: setupDone
    property bool checkingVersion_p: checkingVersion
    property string consoleOutput_p: consoleOutput
    property string webUrl_p: webUrl

    signal configureRequested(string id)

    // At-place launch/stop error feedback: briefly tint the border red and
    // show the (elided) reason in the status slot. The full message also pops
    // up centrally (main.qml). We set `flashing` explicitly (not via a binding
    // to Timer.running, which is non-NOTIFYable) so updates actually fire.
    property string flashMessage: ""
    property bool flashing: false
    Timer {
        id: flashTimer
        interval: 4000
        onTriggered: { root.flashing = false; root.flashMessage = "" }
    }
    Connections {
        target: agents
        function onLaunchFailed(id, message) {
            if (id === root.agentId_p) {
                root.flashMessage = message
                root.flashing = true
                flashTimer.restart()
            }
        }
    }

    // Transient UI state for the stop button: set immediately on click so
    // the card shows "Stopping…" + a spinner before the health check (500ms
    // later) confirms the agent is down. Cleared when running_p goes false.
    property bool stopping: false
    onRunning_pChanged: if (!running_p) stopping = false

    // Console panel visibility. The panel shows live install/update/setup
    // output; it is shown while a command runs, hidden on success, shown for
    // 5s on failure, and can be dismissed (×) or re-opened (context menu).
    property bool consoleVisible: false
    Timer {
        id: consoleHideTimer
        interval: 5000
        onTriggered: root.consoleVisible = false
    }
    // A new install/update/setup run (re)shows the panel and cancels any
    // pending hide timer from a previous run.
    onInstalling_pChanged: if (installing_p) { consoleVisible = true; consoleHideTimer.stop() }
    onSetupping_pChanged: if (setupping_p) { consoleVisible = true; consoleHideTimer.stop() }
    Connections {
        target: agents
        function onInstallFinished(id, success, message) {
            if (id !== root.agentId_p)
                return
            if (success) {
                // Success: hide the panel immediately.
                consoleVisible = false
                consoleHideTimer.stop()
            } else {
                // Failure: keep it visible for 5s so the user can read the
                // error, then auto-hide.
                consoleVisible = true
                consoleHideTimer.restart()
            }
        }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: theme.radiusCard

        // Visual state: running => tinted background with colored border.
        color: root.running_p
              ? Qt.rgba(tintRed(root.agentColor), tintGreen(root.agentColor), tintBlue(root.agentColor), 0.16)
              : (root.cardColor_p.length > 0 ? root.cardColor_p : theme.surfaceBg)
        border.width: root.running_p ? 2.5 : 1
        border.color: root.flashing ? theme.danger
                                    : (root.running_p ? root.agentColor : theme.borderSubtle)
        Behavior on color { ColorAnimation { duration: theme.durationNormal } }
        Behavior on border.color { ColorAnimation { duration: theme.durationNormal } }

        // Click the card body: open web UI when running, otherwise launch.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onClicked: {
                if (root.running_p)
                    workbench.openWeb(root.agentId_p)
                else if (!root.launching_p && !root.setupping_p)
                    agents.launch(root.agentId_p)
            }
        }

        // Right-click context menu.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: contextMenu.popup()
        }

        Menu {
            id: contextMenu

            MenuItem {
                text: root.running_p ? qsTr("Close") : qsTr("Start")
                onTriggered: {
                    if (root.running_p)
                        agents.stop(root.agentId_p)
                    else
                        agents.launch(root.agentId_p)
                }
            }
            MenuItem {
                text: qsTr("Force Stop")
                enabled: root.running_p
                onTriggered: forceStopConfirm.open()
            }
            MenuItem {
                text: root.installed_p ? qsTr("Update") : qsTr("Install")
                enabled: !root.installing_p && !root.running_p && root.installCommand_p.length > 0
                onTriggered: {
                    if (root.installed_p)
                        agents.updateTool(root.agentId_p)
                    else
                        agents.install(root.agentId_p)
                }
            }
            MenuItem {
                text: qsTr("Show output")
                // Only meaningful when there is captured output to display.
                enabled: root.consoleOutput_p.length > 0
                onTriggered: {
                    root.consoleVisible = true
                    consoleHideTimer.stop()
                }
            }
            MenuItem {
                text: qsTr("Configure")
                onTriggered: root.configureRequested(root.agentId_p)
            }
            MenuItem {
                text: qsTr("Open config folder")
                onTriggered: workbench.openConfigDir(root.agentId_p)
            }
            MenuItem {
                text: qsTr("Re-initialize")
                enabled: root.setupCommand_p.length > 0
                onTriggered: agents.resetSetup(root.agentId_p)
            }
        }

        // Top-left indicator: "checking…" label (version check in progress),
        // version label (installed), download icon (not installed), or spinner
        // (installing). Mirrors the top-right × stop button's positioning.
        Item {
            id: versionIndicator
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: theme.spacingS
            anchors.leftMargin: theme.spacingS
            width: 60
            height: 22

            // Checking version state: small spinner
            BusyIndicator {
                visible: root.checkingVersion_p
                running: root.checkingVersion_p
                width: 16
                height: 16
                anchors.centerIn: parent
            }

            // Installing state: spinner
            BusyIndicator {
                visible: root.installing_p
                running: root.installing_p
                width: 16
                height: 16
                anchors.centerIn: parent
            }

            // Not installed: download icon (clickable → install)
            Item {
                visible: !root.installed_p && !root.installing_p && !root.checkingVersion_p
                anchors.fill: parent

                Rectangle {
                    anchors.fill: parent
                    radius: theme.radiusPill
                    color: downloadArea.containsMouse
                           ? theme.alpha(theme.accent, 0.22)
                           : "transparent"
                }
                Image {
                    anchors.centerIn: parent
                    source: downloadArea.containsMouse
                            ? "qrc:/icons/download-hover.svg"
                            : "qrc:/icons/download.svg"
                    sourceSize.width: 16
                    sourceSize.height: 16
                    width: 16
                    height: 16
                    fillMode: Image.PreserveAspectFit
                }
                MouseArea {
                    id: downloadArea
                    anchors.fill: parent
                    hoverEnabled: true
                    ToolTip.text: qsTr("Install")
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                    onClicked: {
                        if (root.running_p) {
                            root.flashMessage = qsTr("Please close before installing")
                            root.flashing = true
                            flashTimer.restart()
                            return
                        }
                        agents.install(root.agentId_p)
                    }
                }
            }

            // Installed: version label + update button
            Item {
                visible: root.installed_p && !root.installing_p && !root.checkingVersion_p
                anchors.fill: parent

                Label {
                    id: versionLabel
                    text: root.version_p.length > 0 ? root.version_p : qsTr("Installed")
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    width: 40
                    horizontalAlignment: Text.AlignHCenter
                }

                // Update icon button (↻)
                Item {
                    id: updateButton
                    anchors.left: versionLabel.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16

                    Rectangle {
                        anchors.fill: parent
                        radius: theme.radiusControl
                        color: updateArea2.containsMouse
                               ? theme.alpha(theme.accent, 0.22)
                               : "transparent"
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "\u21BB"
                        color: updateArea2.containsMouse ? theme.accent : theme.textMuted
                        font.pixelSize: theme.fontSizeBody
                        font.bold: true
                    }
                    MouseArea {
                        id: updateArea2
                        anchors.fill: parent
                        hoverEnabled: true
                        ToolTip.text: qsTr("Update")
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 300
                        ToolTip.timeout: 10000
                        onClicked: {
                        if (root.running_p) {
                            root.flashMessage = qsTr("Please close before updating")
                            root.flashing = true
                            flashTimer.restart()
                            return
                        }
                        agents.updateTool(root.agentId_p)
                    }
                    }
                }
            }
        }

        // Header stack (icon, name, status) anchored directly to the card so
        // the console panel can anchor to statusLabel as a sibling — QML only
        // allows anchoring to a parent or sibling, not to a child of another
        // item. Keeping these out of a Column also means toggling the console
        // panel doesn't shift the header.
        Row {
            id: iconRow
            anchors.top: parent.top
            anchors.topMargin: theme.spacingL
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: theme.spacingM

            Image {
                source: root.icon_p
                sourceSize.width: 40
                sourceSize.height: 40
                width: 40
                height: 40
                fillMode: Image.PreserveAspectFit
            }

            AStatusDot {
                anchors.verticalCenter: parent.verticalCenter
                diameter: 12
                on: root.running_p
                onColor: root.agentColor
            }
        }

        Label {
            id: nameLabel
            text: root.name_p
            color: theme.textPrimary
            font.pixelSize: theme.fontSizeCardTitle
            font.bold: true
            anchors.top: iconRow.bottom
            anchors.topMargin: theme.spacingS
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: theme.spacingL
            anchors.rightMargin: theme.spacingL
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            id: statusLabel
            text: root.flashing ? root.flashMessage
                                : (root.setupping_p ? qsTr("Setting up...")
                                                      : (root.installing_p ? qsTr("Installing...")
                                                      : (root.launching_p ? qsTr("Starting...")
                                                                          : (root.stopping ? qsTr("Stopping...")
                                                                                            : (root.running_p ? qsTr("Running") : qsTr("Stopped"))))))
            color: root.flashing ? theme.danger
                                 : ((root.setupping_p || root.installing_p || root.launching_p || root.stopping || root.running_p) ? root.agentColor : theme.textMuted)
            font.pixelSize: theme.fontSizeBody
            anchors.top: nameLabel.bottom
            anchors.topMargin: theme.spacingS
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: theme.spacingL
            anchors.rightMargin: theme.spacingL
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        // Live console output: while an install/update/setup command is
        // running (or its output is still showing), the empty space below the
        // status line down to the buttons becomes a scrollable log so the user
        // can watch progress instead of a bare spinner. Auto-scrolls to the
        // latest line as new output streams in. Visibility is governed by
        // consoleVisible (see the handlers above); the × in the corner lets
        // the user dismiss it, and the context menu can bring it back.
        Rectangle {
            id: consolePanel
            anchors.top: statusLabel.bottom
            anchors.topMargin: theme.spacingS
            anchors.bottom: buttonRow.top
            anchors.bottomMargin: theme.spacingS
            anchors.left: parent.left
            anchors.leftMargin: theme.spacingL
            anchors.right: parent.right
            anchors.rightMargin: theme.spacingL
            visible: root.consoleOutput_p.length > 0 && root.consoleVisible
            color: theme.consoleBg
            radius: 6
            border.color: theme.borderSubtle
            border.width: 1
            clip: true

            Flickable {
                id: consoleFlick
                anchors.fill: parent
                anchors.margins: theme.spacingXs
                anchors.rightMargin: theme.spacingL // leave room for the × button
                clip: true
                contentWidth: width
                contentHeight: consoleText.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                Text {
                    id: consoleText
                    width: consoleFlick.width
                    text: root.consoleOutput_p
                    color: theme.textSecondary
                    font.family: theme.monoFamily
                    font.pixelSize: theme.fontSizeCaption
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }

                // Keep the most recent lines in view as output grows.
                onContentHeightChanged: {
                    if (contentHeight > height)
                        contentY = contentHeight - height
                    else
                        contentY = 0
                }
            }

            // Dismiss button: hides the panel (the output is retained so the
            // context-menu "Show output" action can bring it back).
            Item {
                id: consoleCloseButton
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: theme.spacingXs
                anchors.rightMargin: theme.spacingXs
                width: 16
                height: 16

                Rectangle {
                    anchors.fill: parent
                    radius: theme.radiusControl
                    color: consoleCloseArea.containsMouse
                           ? theme.alpha(theme.danger, 0.22)
                           : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "\u00D7"
                    color: consoleCloseArea.containsMouse ? theme.danger : theme.textMuted
                    font.pixelSize: theme.fontSizeBody
                    font.bold: true
                }
                MouseArea {
                    id: consoleCloseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    ToolTip.text: qsTr("Hide output")
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                    onClicked: {
                        root.consoleVisible = false
                        consoleHideTimer.stop()
                    }
                }
            }
        }

        // Button row anchored to the bottom of the card so there's no
        // large empty gap below the buttons.
        Row {
            id: buttonRow
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: theme.spacingL
            spacing: theme.spacingS

            AButton {
                id: actionButton
                variant: "primary"
                accentColor: root.agentColor
                width: (parent.width - 10) / 2
                // While the agent is booting up or setting up, disable
                // the button (no double-launch) and show a spinner in
                // place of the label until the health check confirms it
                // is running.
                enabled: !root.launching_p && !root.setupping_p
                text: root.launching_p ? "" : (root.running_p ? qsTr("Open") : qsTr("Start"))

                BusyIndicator {
                    anchors.centerIn: parent
                    visible: root.launching_p || root.setupping_p
                    running: root.launching_p || root.setupping_p
                    width: 24
                    height: 24
                }
                onClicked: {
                    if (root.running_p)
                        workbench.openWeb(root.agentId_p)
                    else
                        agents.launch(root.agentId_p)
                }
            }

            AButton {
                text: qsTr("Configure")
                width: (parent.width - 10) / 2
                onClicked: root.configureRequested(root.agentId_p)
            }
        }

        // Subtle "stop" affordance: a faint × in the top-right corner, only
        // while the agent is running. Brightens on hover. Terminates the
        // process tree this launcher started (see AgentLauncher::stop).
        // While stopping, shows a spinner instead of × and is disabled.
        Item {
            id: stopButton
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: theme.spacingS
            anchors.rightMargin: theme.spacingS
            width: 22
            height: 22
            visible: root.running_p

            Rectangle {
                anchors.fill: parent
                radius: theme.radiusPill
                color: stopArea.containsMouse ? theme.alpha(theme.danger, 0.22) : "transparent"
            }
            Text {
                anchors.centerIn: parent
                text: "\u00D7"
                color: stopArea.containsMouse ? theme.danger : theme.textMuted
                font.pixelSize: theme.fontSizeSubtitle
                font.bold: true
                visible: !root.stopping
            }
            BusyIndicator {
                anchors.centerIn: parent
                visible: root.stopping
                running: root.stopping
                width: 16
                height: 16
            }
            MouseArea {
                id: stopArea
                anchors.fill: parent
                hoverEnabled: true
                enabled: !root.stopping
                ToolTip.text: qsTr("Close")
                ToolTip.visible: containsMouse && !root.stopping
                ToolTip.delay: 300
                ToolTip.timeout: 10000
                onClicked: {
                    root.stopping = true
                    if (!agents.stop(root.agentId_p))
                        root.stopping = false
                }
            }
        }
    }

    // Force-stop confirmation. Kills the process listening on this agent's
    // web port even when the launcher didn't start it (no tracked PID), so
    // agents started elsewhere can still be terminated. Danger styling
    // marks it as a destructive action. Centered over the window (not the
    // 260px card).
    AConfirmDialog {
        id: forceStopConfirm
        parent: Overlay.overlay
        danger: true
        width: 440
        titleText: qsTr("Force Stop")
        message: qsTr("Force stop %1? This will terminate the process serving %2.")
            .arg(root.name_p).arg(root.webUrl_p)
        confirmText: qsTr("Force Stop")
        cancelText: qsTr("Cancel")

        onConfirmed: {
            root.stopping = true
            agents.forceStop(root.agentId_p)
        }
    }

    function tintRed(hex) { return parseInt(hex.substring(1, 3), 16) / 255 }
    function tintGreen(hex) { return parseInt(hex.substring(3, 5), 16) / 255 }
    function tintBlue(hex) { return parseInt(hex.substring(5, 7), 16) / 255 }
}
