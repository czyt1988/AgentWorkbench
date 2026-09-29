import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// agent 启动卡片：单个 agent 的可视化入口。聚合头像/名称/运行状态、
// 启动与停止、安装/更新/初始化的实时控制台输出、右键菜单与强制停止
// 确认。agent 操作一律经 agents.* 门面，跨域动作（打开 Web、配置目录）
// 走 workbench.*。根是 Item：模型 role 需先别名化，视觉本体在内层
// card 上。
Item {
    id: root
    height: theme.cardHeight

    // 卡片是模型 delegate：agent 的增删/重过滤会在悬停中销毁它们，共享
    // tooltip 的 `visible` 绑定随悬停子项一起死掉——在这里 hide，防止
    // tooltip 冻在屏幕上。
    Component.onDestruction: ToolTip.hide()

    // 模型 role 别名化（避免被 Rectangle.color 之类的属性遮蔽）。
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

    // 请求打开配置编辑（由宿主页面弹 AgentEditDialog）。
    signal configureRequested(string id)

    // 就地的启动/停止错误反馈：边框短暂转红，状态槽显示（省略后的）原因；
    // 完整消息另行居中弹出（main.cpp 侧）。flashing 必须显式赋值而非绑定
    // Timer.running（它无 NOTIFY），更新才会触发。
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

    // 停止按钮的过渡 UI 状态：点击立即置真，让卡片在健康检查（500ms 后）
    // 确认 agent 已停之前先显示「Stopping…」+ 转圈；running_p 变假时清除。
    property bool stopping: false
    onRunning_pChanged: if (!running_p) stopping = false

    // 控制台面板可见性。面板实时显示 install/update/setup 的输出：
    // 命令运行期间显示，成功即隐藏，失败保留 5s，可点 × 关闭或经右键
    // 菜单重新打开。
    property bool consoleVisible: false
    Timer {
        id: consoleHideTimer
        interval: 5000
        onTriggered: root.consoleVisible = false
    }
    // 新一轮 install/update/setup 启动时（重新）显示面板，并取消上一轮
    // 遗留的隐藏计时。
    onInstalling_pChanged: if (installing_p) { consoleVisible = true; consoleHideTimer.stop() }
    onSetupping_pChanged: if (setupping_p) { consoleVisible = true; consoleHideTimer.stop() }
    Connections {
        target: agents
        function onInstallFinished(id, success, message) {
            if (id !== root.agentId_p)
                return
            if (success) {
                // 成功：立即隐藏面板。
                consoleVisible = false
                consoleHideTimer.stop()
            } else {
                // 失败：保留 5s 让用户读到错误，然后自动隐藏。
                consoleVisible = true
                consoleHideTimer.restart()
            }
        }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: theme.radiusCard

        // 视觉状态：运行中 => 按主色淡染的背景 + 彩色描边。
        color: root.running_p
              ? Qt.rgba(tintRed(root.agentColor), tintGreen(root.agentColor), tintBlue(root.agentColor), 0.16)
              : (root.cardColor_p.length > 0 ? root.cardColor_p : theme.surfaceBg)
        border.width: root.running_p ? 2.5 : 1
        border.color: root.flashing ? theme.danger
                                    : (root.running_p ? root.agentColor : theme.borderSubtle)
        Behavior on color { ColorAnimation { duration: theme.durationNormal } }
        Behavior on border.color { ColorAnimation { duration: theme.durationNormal } }

        // 点击卡片本体：运行中打开 WebUI，否则启动。
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

        // 右键菜单。
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
                text: qsTr("Open in browser")
                enabled: root.running_p
                onTriggered: workbench.openWebExternal(root.agentId_p)
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
                // 只在有已捕获的输出可显示时才有意义。
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

        // 左上角指示区：版本检查中（转圈）、已安装（版本标签）、未安装
        // （下载图标）或安装中（转圈），与右上角 × 停止按钮的位置对称。
        Item {
            id: versionIndicator
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: theme.spacingS
            anchors.leftMargin: theme.spacingS
            width: 60
            height: 22

            // 版本检查中：小转圈。
            BusyIndicator {
                visible: root.checkingVersion_p
                running: root.checkingVersion_p
                width: 16
                height: 16
                anchors.centerIn: parent
            }

            // 安装中：转圈。
            BusyIndicator {
                visible: root.installing_p
                running: root.installing_p
                width: 16
                height: 16
                anchors.centerIn: parent
            }

            // 未安装：下载图标（可点击 → 安装）。
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

            // 已安装：版本标签 + 更新按钮。
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

                // 更新图标按钮（↻）。
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

        // 头部（图标、名称、状态）直接锚在卡片上，控制台面板才能以
        // statusLabel 为兄弟锚定——QML 只允许锚到父项或兄弟项，不能锚到
        // 别的项的子项。不把它们收进 Column 还有一个好处：开关控制台
        // 面板不会顶动头部。
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

        // 实时控制台输出：install/update/setup 命令运行期间（或输出仍在
        // 展示时），状态行与按钮行之间的空位变成可滚动日志，让用户看到
        // 进度而不是干等转圈；新输出流入时自动滚到最新一行。可见性由
        // consoleVisible 控制（见上方处理器）；角上的 × 供用户收起，右键
        // 菜单可重新打开。
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
                anchors.rightMargin: theme.spacingL // 给 × 按钮留位置
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

                // 输出增长时保持最新几行可见。
                onContentHeightChanged: {
                    if (contentHeight > height)
                        contentY = contentHeight - height
                    else
                        contentY = 0
                }
            }

            // 收起按钮：隐藏面板（输出保留，右键菜单「Show output」可
            // 重新调出）。
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

        // 按钮行锚在卡片底部，下方不留大片空白。agent 运行时主操作是
        // QToolButton 式下拉按钮：按钮本体在应用内打开 WebUI，按钮内紧随
        // 文字的箭头区弹出备选打开方式菜单。
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
                width: root.running_p
                       ? (parent.width - 10) * 0.6
                       : (parent.width - 10) / 2
                // agent 启动中或初始化中时禁用按钮（防重复启动），文字
                // 位置换成转圈，直到健康检查确认运行。
                enabled: !root.launching_p && !root.setupping_p
                text: root.launching_p ? "" : (root.running_p ? qsTr("Open") : qsTr("Start"))
                dropdown: root.running_p
                menuOpen: openMenu.visible
                onDropdownActivated: openMenu.popup()

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

            Menu {
                id: openMenu
                MenuItem {
                    text: qsTr("Open in browser")
                    onTriggered: workbench.openWebExternal(root.agentId_p)
                }
            }

            AButton {
                text: qsTr("Configure")
                width: root.running_p
                       ? (parent.width - 10) * 0.4
                       : (parent.width - 10) / 2
                onClicked: root.configureRequested(root.agentId_p)
            }
        }

        // 低调的「停止」入口：右上角一个淡淡的 ×，仅在 agent 运行时出现，
        // 悬停时变亮。终止本次由启动器拉起的进程树（见 AgentRuntime::stop）；
        // 停止进行中显示转圈替代 × 并禁用。
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

    // 强制停止确认。按端口杀掉监听该 agent web 端口的进程——即使不是
    // 本启动器拉起的（无跟踪 PID）也能终止，在别处启动的 agent 因此
    // 仍可停。danger 样式标明这是破坏性操作。居中于窗口（而不是 260px
    // 的卡片）。
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

    // 从十六进制颜色串拆出 RGB 分量（0..1），供运行态背景的淡染绑定。
    function tintRed(hex) { return parseInt(hex.substring(1, 3), 16) / 255 }
    function tintGreen(hex) { return parseInt(hex.substring(3, 5), 16) / 255 }
    function tintBlue(hex) { return parseInt(hex.substring(5, 7), 16) / 255 }
}
