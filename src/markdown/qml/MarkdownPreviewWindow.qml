import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtWebEngine
import AgentWorkbench
import AgentWorkbench.App

// Markdown 预览窗口：一份文档的渲染面 + 源码编辑器 + 大纲 / 搜索 / 导出。
//
// 由 MainWindow.qml 的 Loader 按需创建，关掉即销毁——WebEngineView 是渲染
// 进程与合成层的入口，常驻会让每一次启动都付这份钱，而预览是低频动作。
//
// 这里只有「窗口」的知识：布局、快捷键、怎么问用户。文档状态、命令队列、
// 视图偏好全在 `markdown` 那个单例（MarkdownPreviewController）里，页面侧的
// 渲染在 awbmd://runtime/app/shell.html 里。三条线之间的全部接触就是：
//   推命令  pump() → view.runJavaScript("AWB.dispatch(…)")
//   收消息  awbmd://runtime/event?k=… → 控制器信号 → 这里的 Connections
//   改视图  markdown.setMode(…) / setFontSizePx(…) 等 WRITE 入口
//
// 与 Web 页的表面（WebEngineSurface.qml）有意不共用组件：那个文件把标签状态
// 机、profile 隔离、token 脱敏、白屏检测、旧引擎 polyfill 整套绑在 web 域上，
// 而这里渲染的是本地文档，没有 cookie 也没有崩溃重载。重叠的只有 console
// 转发一小段，且两处的日志口径不同（这里没有 token 要抹）。
// 与主窗同型用 ApplicationWindow，而不是裸 Window：根部那行 font.family 需要
// ApplicationWindow 才有（QQuickWindow 没有 font 属性，写上去是整份文件加载
// 失败），而字体要覆盖菜单与对话框，就得挂在窗口级而非某个内容 Item 上。
ApplicationWindow {
    id: previewWindow

    // --- 窗口自己的状态（活不过一次开窗） ---------------------------------
    // 页面加载完成且 AWB 可用：命令泵的唯一闸门。在这之前发出去的 dispatch
    // 会撞上 ReferenceError，而那条错误在 QML 侧是无声的。
    property bool pageReady: false
    // 搜索条状态。命中数由页面经信标报回（findResult），这边只镜像。
    property bool searchOpen: false
    property bool searchForward: true
    property bool caseSensitive: false
    property int findTotal: 0
    property int findActive: 0
    // 两栏布局里编辑区占的比例。窗口内状态，不进设置：拖一下不该落盘。
    property real editorShare: 0.45
    // 关窗批准位：有未保存改动时 onClosing 会挡一道，只有经
    // approveClose() 才放行。
    property bool closeApproved: false
    // 在飞的落盘请求（"export" / "browser"），补渲染轮询结束后交给 requestMarkup
    property string pendingLanding: ""
    property int landingPolls: 0
    // 渲染失败的 toast 只报第一次：一份坏 mermaid 图能让页面报几十条
    property bool renderNoticeShown: false
    // 把磁盘那份写回编辑器时的护栏：否则这次写入会被算成用户的编辑，
    // 于是「重载」当场又变成「脏」。
    property bool applyingExternal: false

    width: 1180
    height: 820
    minimumWidth: 720
    minimumHeight: 460
    visible: true
    color: theme.windowBg
    // 文档名在前：任务栏与窗口切换器里认的是文件名，不是「Markdown preview」。
    // 分隔符单列一处，别和下面那行 qsTr 挤在一起——i18n 门禁按物理行检查
    // qsTr 行的 ASCII，em dash 写在同一行就会被当成非英文源串。
    readonly property string titleDash: " — "
    title: (markdown.fileTitle.length > 0
            ? markdown.fileTitle + previewWindow.titleDash : "")
           + qsTr("Markdown preview")
    // 全局字体与主窗一致（Controls2 的字体继承链的根在窗口上）
    font.family: theme.family.length > 0 ? theme.family : undefined

    // --- 动作 --------------------------------------------------------------
    function saveDocument() {
        const result = markdown.saveNow()
        if (!result.ok)
            workbench.notify("error", qsTr("Could not save"), result.error)
        // 成功那条 toast 在 onSaved 里发：存盘的路径不止这一条（菜单、快捷键），
        // 而「存下了」这件事只由控制器的 saved 信号定义。
        return result.ok
    }

    // 命令泵：控制器排队的命令逐条交给页面。
    //
    // 顺序由队列保证（同类命令只留最新一条），而 runJavaScript 的调用顺序
    // 就是页面里的执行顺序，所以不必等回调再发下一条。唯一的例外是
    // serialize：它的返回值本身就是导出件，必须从回调里取。
    function pump() {
        if (!previewWindow.pageReady)
            return
        while (markdown.hasPendingCommand()) {
            const json = markdown.takeCommand()
            // 命令名从 JSON 里解出来，而不是在字符串里找子串：命令体是
            // MarkdownRuntime 紧凑序列化的产物， spacing 一变这里就会静默
            // 不再认出 serialize，于是导出永远不落地、也没有任何报错。
            let cmd = ""
            try {
                cmd = JSON.parse(json).cmd
            } catch (e) {
                console.error("markdown: undecodable command from the queue")
                continue
            }
            view.runJavaScript("AWB.dispatch(" + json + ")",
                               function (reply) {
                if (cmd !== "serialize")
                    return
                markdown.reportMarkup(typeof reply === "string" ? reply : "")
            })
        }
    }

    // 导出 / 外部打开前的补渲染：屏外块与懒画图表都是异步的，直接序列化会得到
    // 一堆占位符。轮询页面自己的 AWB.idle() 而不是猜一个固定延时——图表多的
    // 文档里 mermaid 的耗时能差一个数量级，猜哪个数都会错。
    function requestLanding(purpose) {
        previewWindow.pendingLanding = purpose
        previewWindow.landingPolls = 0
        markdown.requestFullRender()
        idlePoll.restart()
    }

    // 数一次轮询并回报当前次数：到预算就照样序列化，宁可不完美也不永远不落地。
    function spentIdlePoll() {
        previewWindow.landingPolls += 1
        return previewWindow.landingPolls
    }

    // 选区在 Chromium 里，只能把文本取回来交给剪贴板（QML 不碰系统能力，
    // 一律经 workbench）。
    function copySelection() {
        view.runJavaScript(
            "window.getSelection ? String(window.getSelection()) : ''",
            function (text) {
            const picked = typeof text === "string" ? text : ""
            if (picked.length === 0) {
                workbench.notify("info", qsTr("Nothing selected"),
                                 qsTr("Select some text in the preview first."))
                return
            }
            workbench.copyText(picked)
        })
    }

    function copySource() {
        if (markdown.editorText().length === 0) {
            workbench.notify("info", qsTr("Nothing to copy"),
                             qsTr("The document is empty."))
            return
        }
        workbench.copyText(markdown.editorText())
    }

    // --- 搜索 --------------------------------------------------------------
    // 空关键词也要发出去：页面在 find() 开头就把上一次的高亮拆掉，不发就留着一屏
    // 黄底。
    function runSearch(restart) {
        markdown.requestFind(searchField.text, previewWindow.searchForward,
                             previewWindow.caseSensitive, restart)
        if (searchField.text.length === 0) {
            previewWindow.findTotal = 0
            previewWindow.findActive = 0
        }
    }

    function closeSearch() {
        previewWindow.searchOpen = false
        searchField.text = ""
    }

    function approveClose() {
        // 只放行，报到交给 onClosing：两条关闭路径（点 × 与这里的对话框）都
        // 从那里过，状态放一次就够。
        closeApproved = true
        previewWindow.close()
    }

    // 开窗即向控制器报到：从此它才往外排命令（窗口不在时命令一律不排，
    // 见 MarkdownPreviewController 的 m_windowOpen 判据）。
    Component.onCompleted: {
        markdown.notifyWindowOpened()
        if (!markdown.canEdit && markdown.mode === "source")
            markdown.setMode("preview")
    }

    // 没有关闭事件也要把状态位放下来：主窗退出、Loader 被直接关掉都会跳过
    // onClosing。控制器里的 m_windowOpen 有幂等守卫，与 onClosing 各报一次不冲突。
    Component.onDestruction: markdown.notifyWindowClosed()

    // --- 关闭语义 ----------------------------------------------------------
    onClosing: function (close) {
        if (previewWindow.closeApproved || !markdown.dirty) {
            previewWindow.closeApproved = false
            // 关窗必须报到：QML 的 Window 关掉只是藏起来，对象还活着。不报的话
            // `markdown.active` 永远为真，主窗那个 Loader 的 active 不会翻转，
            // 第二次右键预览就只剩一次 raise() 打在一扇已经隐藏的窗上。
            // 顺序要紧：先让控制器把文档与状态放下，窗口随后才销毁；反过来会让
            // notifyWindowClosed() 之后还有编辑器的销毁回调在写文本。
            markdown.notifyWindowClosed()
            return
        }
        // 有未保存的改动时不静默丢弃：这扇窗的存盘键是 Ctrl+S，用户完全可能
        // 改完直接点 ×。
        close.accepted = false
        unsavedDialog.open()
    }

    // --- 快捷键 ------------------------------------------------------------
    // context 用 ApplicationShortcut + enabled 绑本窗激活：Chromium 会吃掉普通
    // 窗口作用域的按键（Web 页的快捷键为什么必须这么写，见 WebTabsPage），而
    // 限定 active 又保证不会在用户回到主窗时抢下 Ctrl+S / Ctrl+F。
    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        enabled: previewWindow.active && markdown.canEdit
        onActivated: previewWindow.saveDocument()
    }
    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.ApplicationShortcut
        enabled: previewWindow.active && !previewWindow.searchOpen
        onActivated: {
            previewWindow.searchOpen = true
            searchField.forceActiveFocus()
        }
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: previewWindow.active && previewWindow.searchOpen
        onActivated: previewWindow.closeSearch()
    }
    // F5 是「重读磁盘」而不是「重载页面」：内容本来就在本地，重载视图只会把
    // 已经挂好的 DOM 丢掉重来一遍。
    Shortcut {
        sequence: "F5"
        context: Qt.ApplicationShortcut
        enabled: previewWindow.active
        onActivated: {
            if (markdown.dirty)
                diskChangedDialog.open()
            else
                markdown.reloadFromDisk()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- 工具行 ---------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingM
            Layout.rightMargin: theme.spacingM
            Layout.topMargin: theme.spacingS
            Layout.bottomMargin: theme.spacingS
            spacing: theme.spacingS

            // 三态是「当前在哪」，所以用 variant 表达选中，不用 checkable：
            // 后者的视觉在深浅主题下都不够读得出来。
            Repeater {
                model: [
                    { key: "preview", label: qsTr("Preview") },
                    { key: "split", label: qsTr("Split") },
                    { key: "source", label: qsTr("Source") }
                ]
                delegate: AButton {
                    required property var modelData
                    required property int index

                    text: modelData.label
                    Layout.preferredWidth: 76
                    variant: markdown.mode === modelData.key ? "primary" : "ghost"
                    // 只读文档没有源码可编：Split / Source 直接禁用，而不是
                    // 让人切到一个只能看的编辑框。
                    enabled: modelData.key === "preview" || markdown.splitAvailable
                    onClicked: markdown.setMode(modelData.key)
                }
            }

            // 分隔条用确定高度，不用 Layout.fillHeight：RowLayout 里的跨轴
            // fill 会让这一行的 implicitHeight 跟着自己的实测高度走，而外层
            // ColumnLayout 正是按 implicitHeight 给非 fill 的行分配——于是
            // 工具栏把自己撑成整扇窗（实测 765/820），正文那一栏只剩 15 px。
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: theme.spacingXs
                Layout.rightMargin: theme.spacingXs
                implicitWidth: 1
                implicitHeight: 24
                color: theme.separator
            }

            AIconButton {
                iconSource: "qrc:/icons/bullet-list.svg"
                tooltip: qsTr("Show outline")
                active: markdown.showOutline
                onClicked: markdown.setShowOutline(!markdown.showOutline)
            }
            AIconButton {
                iconSource: "qrc:/icons/search.svg"
                tooltip: qsTr("Search in document")
                active: previewWindow.searchOpen
                onClicked: {
                    if (previewWindow.searchOpen) {
                        previewWindow.closeSearch()
                        return
                    }
                    previewWindow.searchOpen = true
                    searchField.forceActiveFocus()
                }
            }

            AComboBox {
                id: fontCombo
                Layout.preferredWidth: 132
                // 标签与值同序：0 = 跟随主题的 fontSizeBody。
                property var values: [0, 12, 13, 14, 16, 18, 20, 24]
                model: [qsTr("Theme size"), "12 px", "13 px", "14 px",
                        "16 px", "18 px", "20 px", "24 px"]
                currentIndex: values.indexOf(markdown.fontSizePx)
                onActivated: function (index) {
                    markdown.setFontSizePx(values[index])
                }
            }

            AComboBox {
                id: widthCombo
                Layout.preferredWidth: 132
                // 0 = 内置基准。末项不是像素而是「尽量满栏」：页面侧
                // --awb-doc-width 给一个超过视口的值即等于满栏，比让正文溢出
                // 窗口诚实。
                property var values: [0, 640, 760, 900, 1200, 4000]
                model: [qsTr("Default width"), "640 px", "760 px", "900 px",
                        "1200 px", qsTr("Full width")]
                currentIndex: values.indexOf(markdown.contentWidthPx)
                onActivated: function (index) {
                    markdown.setContentWidthPx(values[index])
                }
            }

            Item {
                Layout.fillWidth: true
            }

            AIconButton {
                iconSource: "qrc:/icons/menu.svg"
                tooltip: qsTr("More actions")
                onClicked: actionsMenu.popup()
            }

            AMenu {
                id: actionsMenu

                AMenuItem {
                    icon.source: "qrc:/icons/copy.svg"
                    text: qsTr("Copy selection")
                    onTriggered: previewWindow.copySelection()
                }
                AMenuItem {
                    icon.source: "qrc:/icons/code.svg"
                    text: qsTr("Copy Markdown source")
                    onTriggered: previewWindow.copySource()
                }
                AMenuSeparator {}
                AMenuItem {
                    icon.source: "qrc:/icons/external-link.svg"
                    text: qsTr("Open in browser")
                    onTriggered: previewWindow.requestLanding("browser")
                }
                AMenuItem {
                    icon.source: "qrc:/icons/download.svg"
                    text: qsTr("Export HTML next to the document")
                    // 导出件里的图片是相对引用，落在文档旁边才显示得出来。
                    onTriggered: {
                        markdown.setExportDirectory("")
                        previewWindow.requestLanding("export")
                    }
                }
                AMenuItem {
                    icon.source: "qrc:/icons/folder.svg"
                    text: qsTr("Export HTML to a folder...")
                    onTriggered: {
                        const folder = ui.pickFolder(
                                    qsTr("Choose a folder for the exported HTML"))
                        if (folder.length === 0)
                            return
                        markdown.setExportDirectory(folder)
                        previewWindow.requestLanding("export")
                    }
                }
                AMenuSeparator {}
                AMenuItem {
                    icon.source: "qrc:/icons/refresh.svg"
                    text: qsTr("Reload from disk")
                    enabled: !markdown.dirty
                    onTriggered: markdown.reloadFromDisk()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: theme.separator
        }

        // --- 搜索条（Ctrl+F 展开） -----------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingM
            Layout.rightMargin: theme.spacingM
            Layout.topMargin: theme.spacingS
            Layout.bottomMargin: theme.spacingS
            visible: previewWindow.searchOpen
            spacing: theme.spacingS

            ASearchField {
                id: searchField
                Layout.preferredWidth: 280
                placeholderText: qsTr("Find in document")
                onTextChanged: searchDebounce.restart()
                onAccepted: {
                    previewWindow.searchForward = true
                    previewWindow.runSearch(true)
                }
            }

            // 逐键搜索在 2 MB 文档上就是每键扫一遍全篇 DOM：停 200 ms 再问。
            Timer {
                id: searchDebounce
                interval: 200
                onTriggered: previewWindow.runSearch(true)
            }

            AButton {
                text: qsTr("Aa")
                Layout.preferredWidth: 44
                variant: previewWindow.caseSensitive ? "primary" : "ghost"
                ToolTip.delay: 300
                ToolTip.timeout: 10000
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Match case")
                onClicked: {
                    previewWindow.caseSensitive = !previewWindow.caseSensitive
                    previewWindow.runSearch(true)
                }
            }

            AButton {
                text: qsTr("Previous")
                variant: "ghost"
                onClicked: {
                    previewWindow.searchForward = false
                    previewWindow.runSearch(false)
                }
            }
            AButton {
                text: qsTr("Next")
                variant: "ghost"
                onClicked: {
                    previewWindow.searchForward = true
                    previewWindow.runSearch(false)
                }
            }

            Label {
                text: searchField.text.length === 0
                      ? ""
                      : (previewWindow.findTotal === 0
                         ? qsTr("No matches")
                         : qsTr("%1 of %2").arg(previewWindow.findActive)
                                               .arg(previewWindow.findTotal))
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
            }

            Item {
                Layout.fillWidth: true
            }

            AIconButton {
                iconSource: "qrc:/icons/close.svg"
                tooltip: qsTr("Close search")
                onClicked: previewWindow.closeSearch()
            }
        }

        // --- 主体：大纲 | 编辑器 | 分隔条 | 预览 ---------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 大纲：数据是控制器算好的（level / text / anchor / blockIndex），
            // 这里只画。点一条走 scrollToAnchor——页面会把那一块的解析插队到
            // 分片队列前面，跳到还没解析的块不该等剩余分片跑完。
            Rectangle {
                id: outlinePanel
                Layout.fillHeight: true
                Layout.preferredWidth: 232
                visible: markdown.showOutline
                color: theme.surfaceBg

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.spacingS
                    spacing: theme.spacingXs

                    Label {
                        text: qsTr("Outline")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                        font.bold: true
                    }

                    ListView {
                        id: outlineList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: markdown.outline

                        ScrollBar.vertical: AScrollBar {}

                        delegate: Item {
                            id: outlineRow

                            required property var modelData
                            required property int index

                            width: outlineList.width
                            height: 26

                            Rectangle {
                                anchors.fill: parent
                                radius: theme.radiusControl
                                color: theme.surfaceHoverBg
                                visible: outlineHover.hovered
                            }

                            HoverHandler {
                                id: outlineHover
                            }

                            TapHandler {
                                onSingleTapped: markdown.scrollToAnchor(
                                    outlineRow.modelData.anchor,
                                    outlineRow.modelData.blockIndex)
                            }

                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                // 缩进按标题级别：level 1 不缩进。
                                x: theme.spacingS
                                   + Math.max(0, outlineRow.modelData.level - 1)
                                     * theme.spacingM
                                width: parent.width - x - theme.spacingS
                                text: outlineRow.modelData.text
                                color: theme.textPrimary
                                font.pixelSize: theme.fontSizeBody
                                elide: Text.ElideRight
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: outlineList.count === 0
                            text: qsTr("No headings")
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                        }
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: theme.separator
                }
            }

            // 编辑区与预览区：手写两栏 + 可拖分隔条。不用 SplitView 是因为三态
            // 里有两态要把一栏挤成 0 宽，而 SplitView 对 visible 的处理跨版本不
            // 一致——这里的可见性正是模式的全部含义。
            Item {
                id: docArea
                Layout.fillWidth: true
                Layout.fillHeight: true

                readonly property bool showsEditor: markdown.mode !== "preview"
                readonly property bool showsView: markdown.mode !== "source"
                readonly property bool showsBoth: showsEditor && showsView
                // 分隔条的位置：单栏时被推到边界，于是另一栏自然 0 宽。
                property real splitX: showsBoth
                                      ? Math.round(width * previewWindow.editorShare)
                                      : (showsEditor ? width : 0)

                // --- 编辑区（源码） ---
                Flickable {
                    id: editorScroll
                    x: 0
                    y: 0
                    width: docArea.splitX
                    height: parent.height
                    visible: docArea.showsEditor && width > 0
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick

                    ScrollBar.vertical: AScrollBar {}

                    // 与 ToolsPage 的提示词编辑区同一套做法：TextArea 自己不会
                    // 滚动，TextArea.flickable 附加属性接管增高与光标可见。
                    TextArea.flickable: ATextArea {
                        id: editorArea

                        // 只在初始化与「磁盘那份进来了」时写 text：常驻双向绑定
                        // 会与用户输入互相打架。
                        Component.onCompleted: text = markdown.editorText()
                        placeholderText: qsTr("Nothing to edit")
                        readOnly: !markdown.canEdit
                        // 源码用等宽：表格与代码块的对齐依赖它。
                        font.family: theme.monoFamily.length > 0
                                     ? theme.monoFamily : theme.family
                        // 停手再交出去：一次敲键要把整篇重切块并算差异，2 MB
                        // 文档上每键一次就是把 CPU 花在马上会被改掉的中间态。
                        onTextChanged: {
                            if (previewWindow.applyingExternal)
                                return
                            editorSyncTimer.restart()
                        }
                    }
                }

                Timer {
                    id: editorSyncTimer
                    interval: 200
                    onTriggered: markdown.setEditorText(editorArea.text)
                }

                // --- 预览区（Chromium） ---
                WebEngineView {
                    id: view
                    // objectName 是 tst_markdownpreviewui 的定位锚点（同 ToolsPage
                    // 的 promptEditor）：测试要从窗口对象图里找到这块视图驱动页面。
                    objectName: "markdownPreviewView"
                    x: docArea.splitX + (docArea.showsBoth ? handle.width : 0)
                    y: 0
                    width: parent.width - x
                    height: parent.height
                    visible: docArea.showsView && width > 0

                    url: markdown.pageUrl()
                    profile: MarkdownPreview.previewProfile()

                    // 页面自己就是渲染器：脚本必须开。写明而不是靠默认值，因为
                    // 「脚本被禁」的表现是正文一片空白，而 C++ 侧一切照常。
                    settings.javascriptEnabled: true

                    // 预览页没有「切走就冻结」的语义：它要么在屏上，要么整栏被
                    // 源码模式挤掉。Frozen 会让分片管线与图表观察器一起停摆，
                    // 切回来时用户看到半篇文档。
                    lifecycleState: WebEngineView.LifecycleState.Active

                    // 链接一律不由本视图导航：它加载的是随包的 shell.html，跟着
                    // 链接走等于把渲染面换成别人的页面。站内锚点同样不支持——
                    // 标题不生成 slug id，理由见 markdown-config.js。
                    //
                    // 拦导航用 navigationRequested。请求对象上只有 accept() /
                    // reject() 两个 Q_INVOKABLE（Qt 6.7 的
                    // QWebEngineNavigationRequest）：既没有 Widgets 那边的
                    // linkClicked 信号，也没有 ignore()——前者写成信号处理器是
                    // 整份文件加载失败（界面只表现为右键没反应），后者写成调用
                    // 是运行时的 TypeError，而异常会让请求按默认值放行，导航照
                    // 样发生，所以这类缺陷在界面上根本看不出来。
                    //
                    // url 要按字符串比：这个类型没注册成 QML 值类型，QML 递过来
                    // 的 url 是字符串，`request.url.scheme` 一律 undefined——拿
                    // 它判前缀会把我们自己的 shell.html 也当成外链拒掉，视图永远
                    // 空白（`url` 属性赋值同样走这条信号，所以判据必须放行 awbmd）。
                    //
                    // 判据是白名单：只有 http/https/mailto 值得交给外部浏览器，
                    // 其余（file:、javascript:、data: 与一切未知 scheme）一律拒掉
                    // 且不转发——本地文档里写什么链接，都不该由这扇窗替用户执行。
                    onNavigationRequested: function (request) {
                        const target = String(request.url)
                        if (target.indexOf("awbmd:") === 0) {
                            request.accept()
                            return
                        }
                        request.reject()
                        if (target.indexOf("http:") === 0
                                || target.indexOf("https:") === 0
                                || target.indexOf("mailto:") === 0)
                            workbench.openExternalUrl(target)
                    }

                    onLoadingChanged: function (info) {
                        if (info.status === WebEngineView.LoadSucceededStatus) {
                            previewWindow.pageReady = true
                            previewWindow.pump()
                            return
                        }
                        if (info.status === WebEngineView.LoadStartedStatus) {
                            previewWindow.pageReady = false
                            return
                        }
                        if (info.status === WebEngineView.LoadFailedStatus) {
                            console.error("markdown: preview page failed to load",
                                          String(info.url), "code:", info.errorCode)
                            workbench.notify("error", qsTr("Preview failed"),
                                             qsTr("The preview page could not be loaded."))
                        }
                    }

                    // JS 控制台：连接此信号后引擎不再走默认的 [js] 日志路由
                    // （检测到有接收者即 return），转发由这里负责。没有 token 要
                    // 抹（本地文档，URL 里不带凭证）；Info 级不转发，与 Web 页
                    // 同一口径——默认路由的 js 日志分类缺省就是 Warning。
                    onJavaScriptConsoleMessage: function (level, message,
                                                         lineNumber, sourceID) {
                        if (level === WebEngineView.InfoMessageLevel)
                            return
                        console.warn("[markdown js]", String(sourceID) + ":"
                                     + lineNumber, message)
                    }

                    // 渲染进程死了不自动重载：重载会再死一次，崩溃循环比手动
                    // 重开这一窗更糟。
                    onRenderProcessTerminated: function (status, exitCode) {
                        console.error("markdown: render process terminated",
                                      status, exitCode)
                        workbench.notify("error", qsTr("Preview crashed"),
                                         qsTr("The renderer was terminated. Reopen the file to try again."))
                    }
                }

                // --- 分隔条：视觉 4px，命中 12px（同 ToolsPage 的手柄口径） ---
                Rectangle {
                    id: handle
                    x: docArea.splitX
                    y: 0
                    width: docArea.showsBoth ? theme.spacingXs : 0
                    height: parent.height
                    visible: width > 0
                    color: splitDrag.containsMouse ? theme.borderStrong
                                                   : theme.separator

                    MouseArea {
                        id: splitDrag
                        width: 12
                        height: parent.height
                        x: (parent.width - width) / 2
                        enabled: docArea.showsBoth
                        cursorShape: Qt.SplitHCursor
                        // 用绝对坐标算比例：相对增量要在 docArea 坐标系里折，
                        // 而 x / width 本来就是这里唯一需要的两个数。
                        onPositionChanged: function (mouse) {
                            if (!pressed || !docArea.showsBoth)
                                return
                            const at = mapToItem(docArea, mouse.x, 0)
                            previewWindow.editorShare = Math.max(
                                        0.15, Math.min(0.75, at.x / docArea.width))
                        }
                    }
                }
            }
        }

        // --- 状态行 ---------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: statusBar.implicitHeight + theme.spacingXs * 2
            color: theme.chromeBg

            RowLayout {
                id: statusBar
                anchors.fill: parent
                anchors.leftMargin: theme.spacingM
                anchors.rightMargin: theme.spacingM
                anchors.topMargin: theme.spacingXs
                anchors.bottomMargin: theme.spacingXs
                spacing: theme.spacingM

                Label {
                    id: statusPath
                    Layout.maximumWidth: 460
                    text: markdown.filePath
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                    font.family: theme.monoFamily
                    elide: Text.ElideMiddle
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                    ToolTip.visible: statusPathHover.hovered
                    ToolTip.text: markdown.filePath

                    HoverHandler {
                        id: statusPathHover
                    }
                }

                // 状态不只靠颜色：脏与只读都写成字，不看色也读得出来。
                Label {
                    visible: markdown.dirty
                    text: qsTr("Unsaved changes")
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeCaption
                }

                Label {
                    visible: markdown.readOnly && markdown.readOnlyReason.length > 0
                    text: markdown.readOnlyReason
                    color: theme.warning
                    font.pixelSize: theme.fontSizeCaption
                    elide: Text.ElideRight
                    Layout.maximumWidth: 380
                }

                Item {
                    Layout.fillWidth: true
                }

                Label {
                    visible: markdown.byteSize > 0
                    text: qsTr("%1 KB").arg(Math.round(markdown.byteSize / 1024))
                    color: theme.textMuted
                    font.pixelSize: theme.fontSizeCaption
                }
                Label {
                    id: statusVersion
                    visible: markdown.rendererVersion.length > 0
                    text: markdown.rendererVersion
                    color: theme.textDisabled
                    font.pixelSize: theme.fontSizeCaption
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                    ToolTip.visible: versionHover.hovered
                    ToolTip.text: markdown.rendererVersion

                    HoverHandler {
                        id: versionHover
                    }
                }
            }
        }
    }

    // --- 与控制器之间的全部信号 --------------------------------------------
    Connections {
        target: markdown

        // 窗口已经活着时再次右键预览：控制器只会再发一次 windowRequested 信号，
        // 主窗那边的 Loader 状态不变，把窗口抬起来这件事只有这里能做。
        function onWindowRequested() {
            previewWindow.raise()
            previewWindow.requestActivate()
        }

        // 队列里进了新命令（编辑、换主题、搜索…）
        function onCommandQueued() {
            previewWindow.pump()
        }

        // 整篇换了（重载、接受外部改动）：编辑器文本跟着换，但要绕开 debounce，
        // 否则这次写入会被算成用户的编辑、当场又变脏。
        function onDocumentChanged() {
            const current = markdown.editorText()
            if (editorArea.text !== current) {
                previewWindow.applyingExternal = true
                editorArea.text = current
                previewWindow.applyingExternal = false
            }
            // 换了一篇文档 = 页面重挂 DOM：旧的命中序号与「报过一次」的闸门都
            // 不再成立，搜索条开着就立刻按新内容重跑一次。
            previewWindow.renderNoticeShown = false
            if (previewWindow.searchOpen)
                previewWindow.runSearch(true)
            else {
                previewWindow.findTotal = 0
                previewWindow.findActive = 0
            }
        }

        function onSaved() {
            workbench.notify("success", qsTr("Saved"), markdown.filePath)
        }

        function onChangedOnDiskChanged() {
            if (!markdown.changedOnDisk)
                return
            if (!markdown.dirty) {
                // 没有可丢的东西就直接跟上磁盘那份；留着提示只会让用户去处理
                // 一个不需要决定的决定。
                markdown.reloadFromDisk()
                return
            }
            diskChangedDialog.open()
        }

        function onFindResult(total, active) {
            previewWindow.findTotal = total
            previewWindow.findActive = active
        }

        function onRenderFailed(message) {
            console.warn("markdown: page reported", message)
            if (previewWindow.renderNoticeShown)
                return
            previewWindow.renderNoticeShown = true
            workbench.notify("error", qsTr("Something did not render"),
                             qsTr("A part of the document failed to render. "
                                  + "See the log for details."))
        }

        function onMarkupWritten(path, purpose) {
            if (purpose === "browser") {
                workbench.openExternalUrl(Url.fromLocalFile(path))
                return
            }
            workbench.notify("success", qsTr("Exported"), path)
            ui.revealFile(path)
        }

        function onMarkupWriteFailed(purpose, message) {
            workbench.notify("error", qsTr("Could not write the file"), message)
        }
    }

    // 外部改动的窗口激活期复查：QFileSystemWatcher 在 Windows 上会静默丢掉
    // 「删除后重建」的文件，光靠监视器有漏报，回到窗口时再问一次。
    onActiveChanged: {
        if (active)
            markdown.refreshDiskState()
    }

    // --- 补渲染轮询（导出 / 外部打开的前置） --------------------------------
    // 判据一律取「还在忙吗」：AWB 没定义、回读形状不对、pending 非零，全都是
    // 「还没闲下来」。把「读不到状态」当成闲，会在页面没加载完时导出一份空文件。
    Timer {
        id: idlePoll
        interval: 250
        repeat: true
        onTriggered: {
            view.runJavaScript("(typeof AWB === 'undefined' ? null : AWB.idle())",
                               function (state) {
                const busy = !state
                        || state.pendingBlocks === undefined
                        || Number(state.pendingBlocks)
                           + Number(state.pendingDiagrams) > 0
                // 40 次 × 250 ms = 10 s 上限。
                if (busy && previewWindow.spentIdlePoll() < 40)
                    return
                idlePoll.stop()
                markdown.requestMarkup(previewWindow.pendingLanding)
            })
        }
    }

    // --- 三个问用户的地方 ---------------------------------------------------
    AConfirmDialog {
        id: diskChangedDialog
        width: 460
        titleText: qsTr("File changed on disk")
        message: qsTr("The file was changed outside this window while you have unsaved changes.")
        confirmText: qsTr("Reload from disk")
        cancelText: qsTr("Keep my changes")

        onConfirmed: {
            const result = markdown.reloadFromDisk()
            if (!result.ok)
                workbench.notify("error", qsTr("Could not reload"), result.error)
        }
        onCancelled: markdown.dismissDiskNotice()
    }

    // 「保存 / 放弃 / 取消」三态：AConfirmDialog 只有两个按钮，所以直接基于
    // ADialog 写（与主窗的退出确认同形，见 designs.md §3）。
    ADialog {
        id: unsavedDialog
        width: 440
        closePolicy: Popup.NoAutoClose
        titleText: qsTr("Unsaved changes")

        ColumnLayout {
            width: unsavedDialog.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Save the changes to %1 before closing?")
                          .arg(markdown.fileTitle)
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                AButton {
                    Layout.fillWidth: true
                    variant: "primary"
                    text: qsTr("Save and close")
                    onClicked: {
                        unsavedDialog.close()
                        if (previewWindow.saveDocument())
                            previewWindow.approveClose()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Discard changes")
                    onClicked: {
                        unsavedDialog.close()
                        previewWindow.approveClose()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: unsavedDialog.close()
                }
            }
        }
    }
}
