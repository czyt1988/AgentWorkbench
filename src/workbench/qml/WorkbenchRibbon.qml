import QtQuick
import SARibbon 3.0
import AgentWorkbench
import "RibbonIcons.js" as RibbonIcons

// 工作台窗口的命令条：SARibbon 形态。
//
// 只在 AWB_ENABLE_RIBBON 的构建里登记进资源清单，由窗口的 Loader 按资源
// URL 装载；当前进程没注册 SARibbon 类型（单测引擎）时装载失败，窗口就地
// 换 WorkbenchToolbar.qml——同一批动作、同一套 win.* 入口，两份实现不许
// 各自长出不同的行为（「动作只有一处出口」的纪律：两份文件都只做转发，
// 逻辑全在窗口的动作层）。
//
// 三个标签页（2026-10 重组）：
//   开始——编辑动作：剪贴板、格式与列表（十枚动作同一面板，宽松档带
//     文字、紧凑档 icon-only，见 startIconStyle()）、标题、插入、
//     复制提示词、查找（输入框常驻面板，回车查找/再回车下一个）；
//   提示词——模板与收藏（普通 category，不再是上下文页：模板库是常驻
//     工具，不随预设出现/消失）、体检与历史（点击开页面上的悬浮窗，
//     动作与浮窗都由页面持有）；
//   视图——Markdown 编辑器视图（预览/分栏/源码/字号/栏宽/大纲）与
//     ribbon 自身（样式/主题跟随/折叠）。
//
// MarkdownEdit 的动作入口（applyBold 等）在页面动作层（win.applyFormat），
// 本文件不直接引用它：格式按钮的 checkable 已随 2026-10 整改移除（光标
// 态查询进不了 QML 绑定，勾选态只会骗人），单例 import 也随之退役。
//
// 主题桥是两套设计系统的唯一接触面：应用主题 → RibbonTheme 单向推。
// tab 栏（标题行）背景跟应用 chrome 底色、面板底色跟 surface、文字跟
// textPrimary——SARibbon 的 accent 令牌就是标题行底色，把它推成
// theme.accent（亮蓝）会造成 tab 栏与软件主体割裂（2026-10 用户报告的
// 根因）。桥恒跟随应用主题：ribbon 的配色不存在「独立档」，视觉统一是
// 全局约束。
Item {
    id: commandBar

    /// 宿主页面（MarkdownWorkbenchPage）：动作与状态的唯一来源。
    property var win: null

    // 折叠态的 category 预览是否正悬着：预览越过命令条底边画到页面内容
    // 之上，而 z 只在兄弟之间生效——宿主 Loader 必须同时把自己抬起来，
    // 否则预览永远被下方内容盖住（嵌入契约见 SARibbon 的 RibbonBar 头
    // 注释；2026-10 用户报告「折叠后点 tab 没反应」的根因）。
    readonly property bool categoryPreviewUp: ribbonBar.minimumModeExpanded

    implicitHeight: ribbonBar.implicitHeight

    // 图标：data URL 现染（颜色跟主题文字令牌走，见 RibbonIcons.js）。
    function icon(name) {
        return RibbonIcons.iconUrl(name, theme.textPrimary)
    }

    // 「开始」页 icon-only 动作在两档样式下的形态：宽松（two-row）档的
    // 大按钮在 IconOnly 下会把图标撑满整个按钮高度，与「标题/插入」
    // 面板同格的图标比着看非常失衡，该档位带文字渲染（与那些面板同一
    // 形态）；紧凑单行档按钮本来就是小的横排，icon-only 省横向空间，
    // 维持原样。函数求值跟 tools.ribbonStyle（有 NOTIFY）走，切档即变。
    function startIconStyle() {
        return tools.ribbonStyle === "two-row"
                ? Ribbon.TextBesideIcon : Ribbon.IconOnly
    }

    // 样式档位（tools.json 持久化）与 SARibbon 枚举的互译。三行档已退役
    // （2026-10 精简为紧凑/宽松两档），读到旧值按单行收敛。
    function styleEnumFor(name) {
        if (name === "two-row")
            return Ribbon.RibbonStyleCompactTwoRow
        return Ribbon.RibbonStyleCompactSingleRow
    }

    // 主题单向桥：应用主题变化时推一次，不逐帧推。
    function applyBridgeTheme() {
        RibbonTheme.currentTheme = theme.variant === "dark"
                ? Ribbon.RibbonThemeOffice2021Dark
                : Ribbon.RibbonThemeOffice2021Blue
        // accent 在 SARibbon 里是「标题行/tab 栏底色」：推应用 chrome 色
        // 才能兑现「tab 栏与主体背景一致」；面板底色与文字各归其位。
        RibbonTheme.setAccentColor(theme.chromeBg)
        RibbonTheme.setContentBgColor(theme.surfaceBg)
        RibbonTheme.setTextColor(theme.textPrimary)
    }

    // Ctrl+F 的落点：焦点进查找框并全选（连续查找的肌肉记忆）。
    function focusFind() {
        findField.forceActiveFocus()
        findField.selectAll()
    }

    // 切标签页（tab 序 = 开始 0 / 提示词 1 / 视图 2）：页面动作层转发
    // （gallery 的格子只在其 tab 被实例化时存在，跨页跳转经这里）。
    function setTabIndex(index) {
        ribbonBar.currentIndex = index
    }

    Component.onCompleted: {
        applyBridgeTheme()
        // 上次停留的标签页恢复要在组件建好之后：currentIndex 直接绑
        // tools.ribbonLastTabIndex 会与「写回」形成循环，改为两段式。
        ribbonBar.currentIndex = tools.ribbonLastTabIndex
    }

    Connections {
        target: theme
        function onChanged() {
            commandBar.applyBridgeTheme()
        }
    }

    RibbonBar {
        id: ribbonBar
        width: parent.width

        // 单行模式：tab 在标题行上、面板标题隐藏、图标在文字左侧。
        ribbonStyle: commandBar.styleEnumFor(tools.ribbonStyle)
        // 折叠位跟窗口走（提示词模式持久化在 tools.ribbonCollapsed，
        // 文档模式是窗口内存态）；用户在 ribbon 上直接展开/折叠时由
        // onMinimumModeChanged 写回，值收敛后不会再触发。
        minimumMode: win ? win.ribbonCollapsed : false
        onMinimumModeChanged: {
            if (win && ribbonBar.minimumMode !== win.ribbonCollapsed)
                win.setRibbonCollapsed(ribbonBar.minimumMode)
        }
        // 应用按钮（左上角「草稿/文件名」）已移除：它没有任何动作（无
        // applicationMenuActions、无 backstage），「在哪份文档」由页面
        // tab 条回答，这里不重复。applicationLabel 留空即整个按钮退场
        // （SARibbon 按空标签算零宽矩形）。
        onCurrentIndexChanged: tools.ribbonLastTabIndex = currentIndex

        // --- 标签页 1 · 开始：编辑动作 -----------------------------------
        RibbonCategory {
            title: qsTr("Start")

            // 剪贴板与撤销：高频动作用图标（紧凑档 icon-only、宽松档
            // 带文字，见 startIconStyle()）。
            RibbonPanel {
                panelTitle: qsTr("Clipboard")

                RibbonToolButton {
                    text: qsTr("Paste")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("paste")
                    toolTip: qsTr("Paste into the editor")
                    enabled: win && win.activeEditor
                             && win.activeEditor.canPaste
                    onClicked: if (win) win.editorAction("paste")
                }
                RibbonToolButton {
                    text: qsTr("Cut")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("cut")
                    toolTip: qsTr("Cut the selection")
                    enabled: win && win.activeEditor
                             && win.activeEditor.selectedText.length > 0
                    onClicked: if (win) win.editorAction("cut")
                }
                RibbonToolButton {
                    text: qsTr("Undo")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("undo")
                    toolTip: qsTr("Undo")
                    enabled: win && win.activeEditor
                             && win.activeEditor.canUndo
                    onClicked: if (win) win.editorAction("undo")
                }
                RibbonToolButton {
                    text: qsTr("Redo")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("redo")
                    toolTip: qsTr("Redo")
                    enabled: win && win.activeEditor
                             && win.activeEditor.canRedo
                    onClicked: if (win) win.editorAction("redo")
                }
            }

            // 行内格式与列表（任务字面结构的 Panel 2：十枚动作同一面板，
            // 形态随档位——startIconStyle()）。格式按钮一律不做 checkable：选中态要跟光标处的
            // 文本走，而「光标位置」不进 QML 绑定（MarkdownEdit 的查询是
            // 函数调用，绑定不会在光标移动时重求值），假勾选态只会骗人
            //（2026-10 用户报告：勾选态既不跟光标也不跟选区）。
            RibbonPanel {
                panelTitle: qsTr("Format")

                RibbonToolButton {
                    text: qsTr("Bold")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("bold")
                    toolTip: qsTr("Bold (**)")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("bold")
                }
                RibbonToolButton {
                    text: qsTr("Italic")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("italic")
                    toolTip: qsTr("Italic (*)")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("italic")
                }
                RibbonToolButton {
                    text: qsTr("Underline")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("underline")
                    toolTip: qsTr("Underline (<u></u>)")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("underline")
                }
                RibbonToolButton {
                    text: qsTr("Strikethrough")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("strike")
                    toolTip: qsTr("Strikethrough (~~)")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("strike")
                }
                RibbonToolButton {
                    text: qsTr("Link")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("link")
                    toolTip: qsTr("Insert a link")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("link")
                }
                RibbonToolButton {
                    text: qsTr("Bullets")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("bullet")
                    toolTip: qsTr("Bullet list")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("bullet")
                }
                RibbonToolButton {
                    text: qsTr("Numbered")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("numbered")
                    toolTip: qsTr("Numbered list")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("numbered")
                }
                RibbonToolButton {
                    text: qsTr("Task List")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("task")
                    toolTip: qsTr("Task list (- [ ])")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("task")
                }
                RibbonToolButton {
                    text: qsTr("Quote")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("quote")
                    toolTip: qsTr("Block quote (>)")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("quote")
                }
                RibbonToolButton {
                    text: qsTr("Divider")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("divider")
                    toolTip: qsTr("Insert a horizontal divider")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("divider")
                }
            }

            // 标题：单独面板（不与列表混排）；菜单给三级与去除。
            RibbonPanel {
                panelTitle: qsTr("Heading")

                RibbonToolButton {
                    text: qsTr("Heading")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("heading")
                    toolTip: qsTr("Set the heading level")
                    popupMode: Ribbon.MenuButtonPopup
                    // 三级 + 文本判定：同级再点 = 去标题（菜单里独立条目）。
                    menuActions: [
                        RibbonAction {
                            text: qsTr("Heading 1")
                            onTriggered: if (win) win.applyFormat("heading1")
                        },
                        RibbonAction {
                            text: qsTr("Heading 2")
                            onTriggered: if (win) win.applyFormat("heading2")
                        },
                        RibbonAction {
                            text: qsTr("Heading 3")
                            onTriggered: if (win) win.applyFormat("heading3")
                        },
                        RibbonAction {
                            text: qsTr("Remove heading")
                            onTriggered: if (win) win.applyFormat("heading0")
                        }
                    ]
                }
            }

            // 插入：icon + text。
            RibbonPanel {
                panelTitle: qsTr("Insert")

                RibbonToolButton {
                    text: qsTr("Inline Code")
                    iconSource: commandBar.icon("inlineCode")
                    toolTip: qsTr("Wrap the selection in backticks")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("code")
                }
                RibbonToolButton {
                    // 围栏代码块 + 语言：菜单给常见语言与「无语言」。
                    text: qsTr("Code Block")
                    iconSource: commandBar.icon("codeBlock")
                    toolTip: qsTr("Insert a fenced code block")
                    popupMode: Ribbon.MenuButtonPopup
                    menuActions: [
                        RibbonAction {
                            text: qsTr("No language")
                            onTriggered: if (win) win.applyFence("")
                        },
                        RibbonAction {
                            text: "C++"
                            onTriggered: if (win) win.applyFence("cpp")
                        },
                        RibbonAction {
                            text: "Python"
                            onTriggered: if (win) win.applyFence("python")
                        },
                        RibbonAction {
                            text: "JavaScript"
                            onTriggered: if (win) win.applyFence("javascript")
                        },
                        RibbonAction {
                            text: "QML"
                            onTriggered: if (win) win.applyFence("qml")
                        },
                        RibbonAction {
                            text: "Bash"
                            onTriggered: if (win) win.applyFence("bash")
                        }
                    ]
                }
                RibbonToolButton {
                    text: qsTr("Table")
                    iconSource: commandBar.icon("table")
                    toolTip: qsTr("Insert a table skeleton")
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("table")
                }
            }

            // 主交付动作：icon + text。
            RibbonPanel {
                panelTitle: qsTr("Deliver")

                RibbonToolButton {
                    text: qsTr("Copy Prompt")
                    iconSource: commandBar.icon("copy")
                    toolTip: qsTr("Put the whole prompt on the clipboard")
                    onClicked: if (win) win.copyPrompt()
                }
            }

            // 查找：输入框常驻面板（回车查找、再回车下一个；动作与分流
            // 都在页面动作层）。「可取消」= 清空并把焦点还给编辑器。
            // SARibbon 的右侧按钮组只接受工具按钮、嵌不了输入控件，
            // 查找框落在「开始」页的收尾面板（Ctrl+F 仍然直达）。
            RibbonPanel {
                panelTitle: qsTr("Find")

                RibbonControlContainer {
                    enableShowIcon: false
                    enableShowTitle: false

                    control: ATextField {
                        id: findField
                        objectName: "ribbonFindField"

                        width: 170
                        placeholderText: qsTr("Find in prompt")
                        // 回车即动作：首个回车从头查找、后续回车下一个
                        // （「是否首个」由页面按命中状态判断）。
                        onAccepted: if (win) win.findNext(findField.text)
                        Keys.onEscapePressed: if (win) win.cancelFind()
                    }
                }
                RibbonToolButton {
                    text: qsTr("Cancel")
                    toolButtonStyle: commandBar.startIconStyle()
                    iconSource: commandBar.icon("close")
                    toolTip: qsTr("Clear the search")
                    enabled: findField.text.length > 0
                    onClicked: if (win) win.cancelFind()
                }
            }
        }

        // --- 标签页 2 · 提示词：模板、收藏、体检与历史 ------------------
        // 普通 category（2026-10 从上下文页转正）：模板与收藏是提示词写作
        // 的常驻工具，不随文档预设出现/消失。
        RibbonCategory {
            title: qsTr("Prompt")

            // 模板库 gallery：SARibbon 的真 gallery（全高网格 + 分组翻页 +
            // 展开视口）。模板是用户数据，走动态填充（gallery.clear() →
            // addItem() 重建，SARibbon 3.0 的动态数据入口）：tools.templates
            // 每次变化重灌，格子 userData 携带模板 id，激活经 triggered 回跳。
            RibbonPanel {
                panelTitle: qsTr("Templates")

                RibbonGallery {
                    id: templateGallery
                    objectName: "templateGallery"

                    property var templates: tools.templates
                    onTemplatesChanged: rebuild()
                    Component.onCompleted: rebuild()

                    function rebuild() {
                        clear()
                        // 按 category 聚组（首见序，空串归默认组）。
                        const order = []
                        const byName = {}
                        for (let i = 0; i < templates.length; ++i) {
                            const t = templates[i]
                            const name = t.category && t.category.length > 0
                                    ? t.category
                                    : qsTr("My templates")
                            if (byName[name] === undefined) {
                                byName[name] = []
                                order.push(name)
                            }
                            byName[name].push(t)
                        }
                        for (let g = 0; g < order.length; ++g) {
                            const group = addGroup(order[g])
                            const rows = byName[order[g]]
                            for (let r = 0; r < rows.length; ++r) {
                                const item = group.addItem(
                                        rows[r].title, "", rows[r].title,
                                        rows[r].id)
                                item.selectable = false
                            }
                        }
                        currentGroupIndex = 0
                    }

                    onTriggered: function (item, index) {
                        if (win)
                            win.insertTemplate(item.userData)
                    }
                }
            }

            // 收藏：同一 gallery 形态（成稿的陈列）。收藏没有分类，单组。
            RibbonPanel {
                panelTitle: qsTr("Favorites")

                RibbonGallery {
                    id: favoriteGallery
                    objectName: "favoriteGallery"

                    property var favorites: tools.favorites
                    onFavoritesChanged: rebuild()
                    Component.onCompleted: rebuild()

                    function rebuild() {
                        clear()
                        const group = addGroup(qsTr("Favorites"))
                        for (let i = 0; i < favorites.length; ++i) {
                            const entry = favorites[i]
                            const item = group.addItem(entry.title, "",
                                                       entry.title,
                                                       entry.title)
                            item.selectable = false
                        }
                        currentGroupIndex = 0
                    }

                    onTriggered: function (item, index) {
                        // 收藏的 id 就是标题（addFavorite 用标题定位）。
                        if (win)
                            win.insertFavorite(item.userData)
                    }
                }
            }

            // 存档动作：icon + text。
            RibbonPanel {
                panelTitle: qsTr("Save")

                RibbonToolButton {
                    text: qsTr("Save as Template")
                    iconSource: commandBar.icon("template")
                    toolTip: qsTr("Turn the draft into a reusable template")
                    enabled: win && win.preset === "prompt"
                             && tools.draft.length > 0
                    onClicked: if (win) win.saveCurrentAsTemplate()
                }
                RibbonToolButton {
                    text: qsTr("Save Favorite")
                    iconSource: commandBar.icon("favorite")
                    toolTip: qsTr("Save the draft to the favorites")
                    enabled: win && win.preset === "prompt"
                             && tools.draft.length > 0
                    onClicked: if (win) win.saveCurrentToFavorites()
                }
            }

            // 体检与历史：点击打开页面上的非模态悬浮窗（可停留、可关闭）。
            RibbonPanel {
                panelTitle: qsTr("Review")

                RibbonToolButton {
                    text: qsTr("Check Prompt")
                    iconSource: commandBar.icon("check")
                    checkable: true
                    checked: win && win.checksVisible
                    toolTip: qsTr("Run prompt checks in a floating panel")
                    onClicked: if (win) win.toggleChecksWindow()
                }
                RibbonToolButton {
                    text: qsTr("History")
                    iconSource: commandBar.icon("history")
                    checkable: true
                    checked: win && win.historyVisible
                    toolTip: qsTr("Browse and roll back draft versions")
                    onClicked: if (win) win.toggleHistoryWindow()
                }
            }
        }

        // --- 标签页 3 · 视图：编辑器视图与 ribbon 自身 -------------------
        // Markdown 编辑器视图（预览/分栏/源码/字号/栏宽/大纲）与软件界面
        // 视图（样式/主题跟随/折叠）两类；tab 条只回答「在哪份文档」，
        // 「怎么看」的选项住在这里。
        RibbonCategory {
            title: qsTr("View")

            RibbonPanel {
                panelTitle: qsTr("Editor")

                RibbonToolButton {
                    text: qsTr("Preview")
                    iconSource: commandBar.icon("preview")
                    checkable: true
                    checked: markdown.mode === "preview"
                    enabled: markdown.available
                    onClicked: markdown.setMode("preview")
                }
                RibbonToolButton {
                    text: qsTr("Split")
                    iconSource: commandBar.icon("split")
                    checkable: true
                    checked: markdown.mode === "split"
                    // 只读文档没有源码可编：Split/Source 直接禁用，而不是
                    // 让人切到一个只能看的编辑框。
                    enabled: markdown.available
                             && markdown.splitAvailable
                    onClicked: markdown.setMode("split")
                }
                RibbonToolButton {
                    text: qsTr("Source")
                    iconSource: commandBar.icon("source")
                    checkable: true
                    checked: markdown.mode === "source"
                    enabled: markdown.available
                             && markdown.splitAvailable
                    onClicked: markdown.setMode("source")
                }

                // 预览字号：下拉显示当前值（0 = 跟随主题的 fontSizeBody）。
                RibbonControlContainer {
                    text: qsTr("Font Size")

                    control: AComboBox {
                        objectName: "fontSizeCombo"

                        model: [qsTr("Theme"), "12 px", "13 px", "14 px",
                                "16 px", "18 px", "20 px", "24 px"]
                        currentIndex: {
                            const px = [0, 12, 13, 14, 16, 18, 20, 24]
                            const at = px.indexOf(markdown.fontSizePx)
                            return at >= 0 ? at : 0
                        }
                        onActivated: function(index) {
                            const px = [0, 12, 13, 14, 16, 18, 20, 24]
                            markdown.setFontSizePx(px[index])
                        }
                    }
                }

                // 正文列默认宽度：下拉显示当前值（末项是「尽量满栏」）。
                RibbonControlContainer {
                    text: qsTr("Width")

                    control: AComboBox {
                        objectName: "widthCombo"

                        model: [qsTr("Default"), "640 px", "760 px",
                                "900 px", "1200 px", qsTr("Full")]
                        currentIndex: {
                            const px = [0, 640, 760, 900, 1200, 4000]
                            const at = px.indexOf(markdown.contentWidthPx)
                            return at >= 0 ? at : 0
                        }
                        onActivated: function(index) {
                            const px = [0, 640, 760, 900, 1200, 4000]
                            markdown.setContentWidthPx(px[index])
                        }
                    }
                }

                RibbonSeparator {}

                // 大纲：两种预设都在场（草稿也是一份有标题的正文）；
                // 纯源码态没有渲染窗，页面按 showsView 决定面板显隐。
                RibbonToolButton {
                    text: qsTr("Outline")
                    iconSource: commandBar.icon("outline")
                    checkable: true
                    checked: markdown.showOutline
                    enabled: markdown.available
                    onClicked: markdown.setShowOutline(!markdown.showOutline)
                }
            }

            // ribbon 自身：样式（下拉显示当前档）、主题跟随与折叠。
            RibbonPanel {
                panelTitle: qsTr("Ribbon")

                RibbonControlContainer {
                    text: qsTr("Style")

                    control: AComboBox {
                        objectName: "styleCombo"

                        // 紧凑 = 单行（默认）；宽松 = 两行。三行档已退役。
                        model: [qsTr("Compact"), qsTr("Loose")]
                        currentIndex: tools.ribbonStyle === "two-row" ? 1 : 0
                        onActivated: function(index) {
                            tools.ribbonStyle = index === 1
                                    ? "two-row" : "single"
                        }
                    }
                }

                RibbonToolButton {
                    // 主题跟随关掉时 ribbon 保留自己的调色板（不反向同步）。
                    text: qsTr("Follow Theme")
                    checkable: true
                    checked: tools.ribbonFollowAppTheme
                    onToggled: function (checked) {
                        tools.ribbonFollowAppTheme = checked
                        if (checked)
                            commandBar.applyBridgeTheme()
                    }
                }
                RibbonToolButton {
                    text: qsTr("Collapse")
                    checkable: true
                    checked: win && win.ribbonCollapsed
                    onClicked: if (win) win.toggleRibbonCollapsed()
                }
            }
        }
    }
}
