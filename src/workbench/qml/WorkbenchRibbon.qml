import QtQuick
import SARibbon 3.0

// 工作台窗口的命令条：SARibbon 形态（单行模式）。
//
// 只在 AWB_ENABLE_RIBBON 的构建里登记进资源清单，由窗口的 Loader 按资源
// URL 装载；当前进程没注册 SARibbon 类型（单测引擎）时装载失败，窗口就地
// 换 WorkbenchToolbar.qml——同一批动作、同一套 win.* 入口，两份实现不许
// 各自长出不同的行为（「动作只有一处出口」的纪律：两份文件都只做转发，
// 逻辑全在窗口的动作层）。
//
// 单行模式的代价（见规划的事实清单）：proportion 失效（一律小按钮）、
// 面板标题隐藏、按钮文字要短（英文源串 ≤ 12 字符）。主次靠标签页归类
// 与按钮顺序表达。
//
// 主题是两套设计系统的唯一接触面：应用主题 → RibbonTheme 单向推
//（setAccentColor / setContentBgColor / setTextColor + currentTheme 取
// 深浅档），不做反向同步、不留第二套自动跟随（followAppTheme 关掉时
// ribbon 保留自己的调色板）。
Item {
    id: commandBar

    /// 宿主页面（MarkdownWorkbenchPage）：动作与状态的唯一来源。
    property var win: null

    implicitHeight: ribbonBar.implicitHeight

    // 样式档位（tools.json 持久化）与 SARibbon 枚举的互译。
    function styleEnumFor(name) {
        if (name === "two-row")
            return Ribbon.RibbonStyleCompactTwoRow
        if (name === "three-row")
            return Ribbon.RibbonStyleCompactThreeRow
        return Ribbon.RibbonStyleCompactSingleRow
    }

    // 主题单向桥：应用主题变化时推一次，不逐帧推。
    function applyBridgeTheme() {
        if (!tools.ribbonFollowAppTheme)
            return
        RibbonTheme.currentTheme = theme.variant === "dark"
                ? Ribbon.RibbonThemeOffice2021Dark
                : Ribbon.RibbonThemeOffice2021Blue
        RibbonTheme.setAccentColor(theme.accent)
        RibbonTheme.setContentBgColor(theme.surfaceBg)
        RibbonTheme.setTextColor(theme.textPrimary)
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
        // 页面身份由左上角的应用按钮承担（窗口没有「页头」这个概念）。
        applicationLabel: win
                          ? (win.preset === "document"
                             ? (markdown.fileTitle.length > 0
                                ? markdown.fileTitle : qsTr("File"))
                             : qsTr("Draft"))
                          : qsTr("Draft")
        onCurrentIndexChanged: tools.ribbonLastTabIndex = currentIndex

        // 标题行右侧的高频小按钮：Copy 是主交付动作。
        RibbonQuickAccessBar {
            RibbonToolButton {
                text: qsTr("Copy")
                iconSource: "qrc:/icons/copy.svg"
                onClicked: if (win) win.copyPrompt()
            }
        }

        // --- 标签页 1 · 开始：编辑时最常用的动作，默认选中 ---------------
        RibbonCategory {
            title: qsTr("Start")

            RibbonPanel {
                RibbonToolButton {
                    text: qsTr("Copy Prompt")
                    iconSource: "qrc:/icons/copy.svg"
                    toolTip: qsTr("Put the whole prompt on the clipboard")
                    onClicked: if (win) win.copyPrompt()
                }
                RibbonToolButton {
                    text: qsTr("Paste")
                    iconSource: "qrc:/icons/paste.svg"
                    enabled: win && win.activeEditor
                             && win.activeEditor.canPaste
                    onClicked: if (win) win.editorAction("paste")
                }
                RibbonToolButton {
                    text: qsTr("Cut")
                    iconSource: "qrc:/icons/cut.svg"
                    enabled: win && win.activeEditor
                             && win.activeEditor.selectedText.length > 0
                    onClicked: if (win) win.editorAction("cut")
                }
                RibbonToolButton {
                    text: qsTr("Copy Text")
                    iconSource: "qrc:/icons/copy.svg"
                    enabled: win && win.activeEditor
                             && win.activeEditor.selectedText.length > 0
                    onClicked: if (win) win.editorAction("copy")
                }
            }

            RibbonPanel {
                RibbonToolButton {
                    text: qsTr("Undo")
                    iconSource: "qrc:/icons/undo.svg"
                    enabled: win && win.activeEditor
                             && win.activeEditor.canUndo
                    onClicked: if (win) win.editorAction("undo")
                }
                RibbonToolButton {
                    text: qsTr("Redo")
                    iconSource: "qrc:/icons/redo.svg"
                    enabled: win && win.activeEditor
                             && win.activeEditor.canRedo
                    onClicked: if (win) win.editorAction("redo")
                }
            }

            RibbonPanel {
                RibbonToolButton {
                    text: qsTr("Bold")
                    iconSource: "qrc:/icons/bold.svg"
                    checkable: true
                    // checkable 的真实状态源：MarkdownEdit 的光标查询
                    //（没有查询侧的动作不许设 checkable——否则按钮骗人）。
                    checked: win && win.activeEditor
                             && MarkdownEdit.isBoldAt(win.activeEditor)
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("bold")
                }
                RibbonToolButton {
                    text: qsTr("Italic")
                    iconSource: "qrc:/icons/bold.svg"
                    checkable: true
                    checked: win && win.activeEditor
                             && MarkdownEdit.isItalicAt(win.activeEditor)
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("italic")
                }
                RibbonToolButton {
                    text: qsTr("Strike")
                    iconSource: "qrc:/icons/bold.svg"
                    checkable: true
                    checked: win && win.activeEditor
                             && MarkdownEdit.isStrikeAt(win.activeEditor)
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("strike")
                }
                RibbonToolButton {
                    text: qsTr("Code")
                    iconSource: "qrc:/icons/code.svg"
                    checkable: true
                    checked: win && win.activeEditor
                             && MarkdownEdit.isCodeAt(win.activeEditor)
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("code")
                }
                RibbonToolButton {
                    text: qsTr("Link")
                    iconSource: "qrc:/icons/web.svg"
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("link")
                }
            }

            RibbonPanel {
                RibbonToolButton {
                    text: qsTr("Heading")
                    iconSource: "qrc:/icons/bold.svg"
                    popupMode: Ribbon.MenuButtonPopup
                    // 三级 + 文本判定：同级再点 = 去标题（菜单里独立条目）。
                    menuItems: [
                        RibbonMenuItem {
                            text: qsTr("Heading 1")
                            onTriggered: if (win) win.applyFormat("heading1")
                        },
                        RibbonMenuItem {
                            text: qsTr("Heading 2")
                            onTriggered: if (win) win.applyFormat("heading2")
                        },
                        RibbonMenuItem {
                            text: qsTr("Heading 3")
                            onTriggered: if (win) win.applyFormat("heading3")
                        },
                        RibbonMenuItem {
                            text: qsTr("Remove heading")
                            onTriggered: if (win) win.applyFormat("heading0")
                        }
                    ]
                }
                RibbonToolButton {
                    text: qsTr("Bullets")
                    iconSource: "qrc:/icons/bullet-list.svg"
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("bullet")
                }
                RibbonToolButton {
                    text: qsTr("Numbered")
                    iconSource: "qrc:/icons/bullet-list.svg"
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("numbered")
                }
                RibbonToolButton {
                    text: qsTr("Task List")
                    iconSource: "qrc:/icons/bullet-list.svg"
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("task")
                }
                RibbonToolButton {
                    text: qsTr("Quote")
                    iconSource: "qrc:/icons/bold.svg"
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("quote")
                }
                RibbonToolButton {
                    text: qsTr("Divider")
                    iconSource: "qrc:/icons/bold.svg"
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("divider")
                }
            }

            RibbonPanel {
                RibbonToolButton {
                    // 围栏代码块 + 语言：菜单给常见语言与「无语言」。
                    text: qsTr("Fence")
                    iconSource: "qrc:/icons/code.svg"
                    popupMode: Ribbon.MenuButtonPopup
                    menuItems: [
                        RibbonMenuItem {
                            text: qsTr("No language")
                            onTriggered: if (win) win.applyFence("")
                        },
                        RibbonMenuItem {
                            text: "C++"
                            onTriggered: if (win) win.applyFence("cpp")
                        },
                        RibbonMenuItem {
                            text: "Python"
                            onTriggered: if (win) win.applyFence("python")
                        },
                        RibbonMenuItem {
                            text: "JavaScript"
                            onTriggered: if (win) win.applyFence("javascript")
                        },
                        RibbonMenuItem {
                            text: "QML"
                            onTriggered: if (win) win.applyFence("qml")
                        },
                        RibbonMenuItem {
                            text: "Bash"
                            onTriggered: if (win) win.applyFence("bash")
                        }
                    ]
                }
                RibbonToolButton {
                    text: qsTr("Table")
                    iconSource: "qrc:/icons/code.svg"
                    enabled: win && win.activeEditor
                    onClicked: if (win) win.applyFormat("table")
                }
            }

            RibbonPanel {
                RibbonToolButton {
                    text: qsTr("Find")
                    iconSource: "qrc:/icons/search.svg"
                    // 文档模式查预览渲染区（既有搜索条），提示词模式查
                    // 草稿源码（B2 的编辑区查找条）。
                    enabled: win
                    onClicked: {
                        if (!win)
                            return
                        if (win.preset === "document")
                            win.openSearch()
                        else
                            win.openDraftFind()
                    }
                }
                RibbonToolButton {
                    text: qsTr("Save")
                    iconSource: "qrc:/icons/download.svg"
                    enabled: win && win.preset === "document"
                             && markdown.canEdit
                    onClicked: if (win) win.saveDocument()
                }
                RibbonToolButton {
                    // B3 清空草稿：确认 + 可撤销（规划 03 §四）。
                    text: qsTr("Clear")
                    iconSource: "qrc:/icons/close.svg"
                    enabled: win && win.preset === "prompt"
                             && tools.draft.length > 0
                    onClicked: if (win) win.confirmClearDraft()
                }
            }

            // 草稿管理（M4）：放「开始」页而不是提示词上下文页——
            // E4（从文档新建草稿）在文档预设下才有意义，开始页两种
            // 预设都在场。
            RibbonPanel {
                panelTitle: qsTr("Drafts")

                RibbonToolButton {
                    text: qsTr("New Draft")
                    iconSource: "qrc:/icons/plus.svg"
                    enabled: win
                    onClicked: if (win) win.newDraft()
                }
                RibbonToolButton {
                    // E4：当前文档的文本原样成稿（单向：草稿永不写回文件）。
                    text: qsTr("From Document")
                    iconSource: "qrc:/icons/file.svg"
                    enabled: win && win.preset === "document"
                             && markdown.filePath.length > 0
                    onClicked: if (win) win.newDraftFromDocument()
                }
                RibbonToolButton {
                    text: qsTr("Rename")
                    iconSource: "qrc:/icons/bold.svg"
                    enabled: win && win.preset === "prompt"
                             && tools.activeDraftId.length > 0
                    onClicked: if (win) win.renameCurrentDraft()
                }
                RibbonToolButton {
                    text: qsTr("Duplicate")
                    iconSource: "qrc:/icons/copy.svg"
                    enabled: win && win.preset === "prompt"
                             && tools.activeDraftId.length > 0
                    onClicked: if (win) win.duplicateCurrentDraft()
                }
                RibbonToolButton {
                    text: qsTr("Delete")
                    iconSource: "qrc:/icons/close.svg"
                    enabled: win && win.preset === "prompt"
                             && tools.activeDraftId.length > 0
                    onClicked: if (win) win.deleteCurrentDraft()
                }
            }
        }

        // --- 标签页 2 · 提示词：模板与收藏（上下文页，仅提示词预设） ---------
        // 决策 D-2：文档模式（双击 .md）下这个 tab 不出现——模板与收藏是
        // 草稿的工具，别人的文档不往里插。挂 RibbonContextCategory。
        //
        // contextColor 不能用 theme.accent：SARibbon 的标题行底色就是
        // RibbonTheme.accent（主题桥把应用 accent 推了进去），上下文页的
        // 色带与 tab 文字都按 contextColor 画——同色等于全部隐形，看起来
        // 就是「context 页和普通页混叠在一起」（2026-10 用户报告）。绿色
        // 在深浅两套主题下都与 accent 蓝可区分，也是 Office 上下文页的
        // 惯例用色。
        RibbonContextCategory {
            id: promptContext
            contextTitle: qsTr("Prompt tools")
            contextColor: theme.success
            active: win ? win.preset === "prompt" : false

            RibbonCategory {
                title: qsTr("Prompt")

                RibbonPanel {
                    RibbonToolButton {
                        // 左栏模板库就是选择器（openTemplatePicker）：
                        // SARibbon 的 gallery/menuItems 是静态
                        // listProperty 绑不了模型，不在 Ribbon 里
                        // 造第二份列表。
                        text: qsTr("Insert Template")
                        iconSource: "qrc:/icons/file.svg"
                        toolTip: qsTr("Browse the template library")
                        onClicked: if (win) win.openTemplatePicker()
                    }
                    RibbonToolButton {
                        text: qsTr("Save as Template")
                        iconSource: "qrc:/icons/code.svg"
                        enabled: win && win.preset === "prompt"
                        onClicked: if (win) win.saveCurrentAsTemplate()
                    }
                }

                RibbonPanel {
                    RibbonToolButton {
                        text: qsTr("Save Favorite")
                        iconSource: "qrc:/icons/copy.svg"
                        enabled: win && win.preset === "prompt"
                        onClicked: if (win) win.saveCurrentToFavorites()
                    }
                }

                RibbonPanel {
                    // B9 体检入口：触发 + 规则开关（清单在右栏体检段）。
                    RibbonToolButton {
                        text: qsTr("Check Prompt")
                        iconSource: "qrc:/icons/search.svg"
                        enabled: win && win.preset === "prompt"
                        onClicked: if (win) win.runDraftChecks()
                    }
                }
            }
        }

        // --- 标签页 3 · 视图：三态、面板与 ribbon 自己的开关 ---------------
        RibbonCategory {
            title: qsTr("View")

            RibbonPanel {
                RibbonToolButton {
                    text: qsTr("Preview")
                    iconSource: "qrc:/icons/web.svg"
                    checkable: true
                    checked: win && win.preset === "document"
                             && markdown.mode === "preview"
                    enabled: win && win.preset === "document"
                    onClicked: markdown.setMode("preview")
                }
                RibbonToolButton {
                    text: qsTr("Split")
                    iconSource: "qrc:/icons/menu.svg"
                    checkable: true
                    checked: win && win.preset === "document"
                             && markdown.mode === "split"
                    enabled: win && win.preset === "document"
                             && markdown.splitAvailable
                    onClicked: markdown.setMode("split")
                }
                RibbonToolButton {
                    text: qsTr("Source")
                    iconSource: "qrc:/icons/code.svg"
                    checkable: true
                    checked: win && win.preset === "document"
                             && markdown.mode === "source"
                    enabled: win && win.preset === "document"
                             && markdown.splitAvailable
                    onClicked: markdown.setMode("source")
                }
            }

            RibbonPanel {
                RibbonToolButton {
                    text: qsTr("Outline")
                    iconSource: "qrc:/icons/bullet-list.svg"
                    checkable: true
                    checked: markdown.showOutline
                    enabled: win && win.preset === "document"
                    onClicked: markdown.setShowOutline(!markdown.showOutline)
                }
                RibbonToolButton {
                    // 「完整工具」即预设切换：全开 = 提示词模式，收起 =
                    // 文档模式（没有可看文档时不可用）。
                    text: qsTr("Full Tools")
                    checkable: true
                    checked: win && win.preset === "prompt"
                    enabled: win && (win.preset === "prompt"
                                     || (markdown.available
                                         && markdown.fileTitle.length > 0))
                    onClicked: if (win) win.toggleFullTools()
                }
            }

            RibbonPanel {
                RibbonToolButton {
                    text: qsTr("Collapse")
                    checkable: true
                    checked: win && win.ribbonCollapsed
                    onClicked: if (win) win.toggleRibbonCollapsed()
                }
                RibbonToolButton {
                    text: qsTr("Style")
                    iconSource: "qrc:/icons/menu.svg"
                    popupMode: Ribbon.MenuButtonPopup
                    menuItems: [
                        RibbonMenuItem {
                            text: qsTr("Single row")
                            checkable: true
                            checked: tools.ribbonStyle === "single"
                            onTriggered: tools.ribbonStyle = "single"
                        },
                        RibbonMenuItem {
                            text: qsTr("Two rows")
                            checkable: true
                            checked: tools.ribbonStyle === "two-row"
                            onTriggered: tools.ribbonStyle = "two-row"
                        },
                        RibbonMenuItem {
                            text: qsTr("Three rows")
                            checkable: true
                            checked: tools.ribbonStyle === "three-row"
                            onTriggered: tools.ribbonStyle = "three-row"
                        }
                    ]
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
            }
        }
    }
}
