import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// Markdown 工作台页面：提示词撰写与文档预览的合体，住在主窗工作区里
//（Agent Tools 分组下的「提示词编辑」条目）。曾经是独立窗口，2026-10
// 按原始设计收回到主窗容器内——工作台是 Agent Tools 的一个高级功能，
// 不是一扇独立的应用窗。
//
// 两种「预设」是同一套代码的两份界面配置，不是两个应用：
//   提示词模式（prompt）——左侧工作区文件树开、草稿编辑区为主体。
//   文档模式（document）——侧栏收起，中间区渲染磁盘上那份 .md。
// 切预设的入口是 tab 条（草稿 tab ⇄ 文件 tab）；页面创建时按控制器
// 里的活跃会话推断起点（活跃的是文件会话 = 文档模式，否则提示词模式）。
//
// 2026-10 重组：左右业务侧栏退役（模板与收藏移进命令条「提示词」页的
// gallery，体检与历史改为非模态悬浮窗），编辑区占满中栏；查找入口统一
// 为命令条上的查找框（Ctrl+F 直达）。这里只有「页面」的知识：布局、
// 快捷键、怎么问用户。文档状态与命令队列在 `markdown` 单例
//（MarkdownPreviewController）里，草稿与工作区在 `tools` 单例
//（ToolsFacade）里，渲染面在 MarkdownPreviewPane.qml（按资源 URL 惰性
// 装载——本页面要在没有 WebEngine 的构建里照常加载）。
//
// 本页注册为 keepAlive：编辑器的撤销栈、滚动位置与分屏比例搬不进 C++，
// 切页销毁会让写作现场清零。因此页面常驻，快捷键一律门控 pageCurrent。
Item {
    id: workbenchWindow

    // --- 预设 --------------------------------------------------------------
    // 当前预设：prompt | document。文档模式要求控制器里有一份能渲染的
    // 文档，拿不到时 applyPreset 会退回提示词模式。切换入口：tab 点击
    // 与生命周期开头的推断。
    property string preset: "prompt"

    // 本页是不是当前页（keepAlive 页的非激活期要闭嘴：快捷键、轮询）。
    readonly property bool pageCurrent: nav.currentPageId === "tools"

    // 两套预设的界面状态各自独立、互不污染（页面存活期内）：
    // 提示词模式左侧开；文档模式左侧收起（窄边可展开）。
    property bool promptLeftOpen: true
    property bool documentLeftOpen: false
    // 文档模式的 Ribbon 折叠位（提示词模式的折叠位持久化在
    // tools.ribbonCollapsed 里）。
    property bool documentRibbonCollapsed: true

    readonly property bool leftOpen: preset === "prompt"
                                    ? promptLeftOpen : documentLeftOpen
    readonly property bool ribbonCollapsed: preset === "prompt"
                                            ? tools.ribbonCollapsed
                                            : documentRibbonCollapsed

    // --- 页面自己的状态 ----------------------------------------------------
    // 查找状态：当前关键词（命令条查找框的镜像——命中判定由页面统一做）。
    property string findNeedle: ""
    // 查找的命中统计（「N/M」，命令条与状态行消费）：正文编辑后由
    // findStatsTimer 防抖重算，动作结束（findNext/replace*）立即重算。
    property int findMatchCount: 0
    property int findMatchIndex: 0
    // 两栏布局里编辑区占的比例。页面内状态，不进设置：拖一下不该落盘。
    property real editorShare: 0.45

    // 编辑区字号（Ctrl+滚轮缩放，草稿区与文件源码区共用一份）：0 = 跟随
    // 主题 fontSizeBody。同为页面内状态、不落盘——重启回到主题默认；这是
    // 用户驱动的临时阅读缩放（与预览的字号设置同一性质），不属于界面固定
    // 字号阶梯（designs.md §2.1 约束的是设计期取值）。
    property int editorFontPx: 0
    readonly property int editorFontResolved: editorFontPx > 0
                                              ? editorFontPx
                                              : theme.fontSizeBody

    // Ctrl+滚轮的字号步进：每 120 单位 angleDelta（一格滚轮）±1 px，
    // 夹在 8–40——再小看不清行间结构，再大单行容不下代码块。
    function zoomEditorFont(steps) {
        const next = editorFontResolved + steps
        if (next < 8 || next > 40)
            return
        editorFontPx = next
    }

    // Tab 键的编辑器动作（VSCode 语义）：多行选区给每行行首插缩进单位
    //（选区随之扩展）；无选区/单行选区保持原生——插入一个缩进单位（单行
    // 选区被替换）。缩进单位来自设置（空格 × 每级宽度，或字面 \t）。只读
    // 文档不接（MarkdownEdit 直接走光标，绕过 readOnly）。
    function handleEditorTab(editor) {
        if (editor.readOnly)
            return
        MarkdownEdit.handleTab(editor,
                               workbench.markdownEditorIndentWithSpaces,
                               workbench.markdownEditorIndentWidth)
    }

    // Shift+Tab：Tab 的反向动作，给覆盖到的每行行首去掉一个缩进单位
    //（无选区时是光标所在行）。只读文档同样不接。
    function handleEditorBacktab(editor) {
        if (editor.readOnly)
            return
        MarkdownEdit.handleBacktab(editor,
                                   workbench.markdownEditorIndentWithSpaces,
                                   workbench.markdownEditorIndentWidth)
    }

    // --- 输入手感（A 批；开关在设置 → Markdown，读经 workbench） --------
    // 草稿区与文件源码区挂同一组处理器，焦点在谁身上动作就落在谁身上。

    // 编辑器聚焦时的格式化快捷键（A1/A2）：只认 Ctrl+B/I/K/E 四个组合，
    // 返回 applyFormat 的 kind；其余按键返回空串。修饰键必须**恰好**是
    // Ctrl——行级动作占着 Ctrl+Shift+K（删行），带 Shift 的组合不属于这
    // 里。Ctrl+B 的归属由此定：编辑器聚焦时它被 Keys.onShortcutOverride
    // 拦下做加粗，焦点不在编辑器时仍是全局的侧栏折叠（MainWindow 的
    // Shortcut 不动）。
    function formatShortcutKind(event) {
        if (event.modifiers !== Qt.ControlModifier)
            return ""
        switch (event.key) {
        case Qt.Key_B:
            return "bold"
        case Qt.Key_I:
            return "italic"
        case Qt.Key_K:
            return "link"
        case Qt.Key_E:
            return "code"
        }
        return ""
    }

    function editorFormatShortcutsEnabled(editor) {
        return workbench.markdownEditorFormatShortcuts && pageCurrent
                && editor && !editor.readOnly
    }

    // Keys.onShortcutOverride：接管了快捷键，窗口级 Shortcut（全局的
    // Ctrl+B 侧栏折叠）就拿不到这次按键；不接管则一切照旧。
    function handleEditorShortcutOverride(editor, event) {
        if (editorFormatShortcutsEnabled(editor)
                && formatShortcutKind(event).length > 0)
            event.accepted = true
    }

    // Keys.onPressed 的合并接线：先给格式化快捷键机会，再做输入配对；
    // 都不接时放行编辑器默认行为。
    function handleEditorKeyPressed(editor, event) {
        // 图片粘贴：纯图片剪贴板时编辑器原生 paste() 不可用，先问
        // workbench；它没接管才放行默认粘贴。
        if (!editor.readOnly && isPasteShortcut(event)) {
            if (pasteInto(editor)) {
                event.accepted = true
                return
            }
        }
        if (editorFormatShortcutsEnabled(editor)) {
            const kind = formatShortcutKind(event)
            if (kind.length > 0) {
                applyFormat(kind)
                event.accepted = true
                return
            }
        }
        // 输入配对（A6）：无修饰键的单字符输入才接；接管时吞掉默认插入。
        // 输入法组字走 InputMethod 事件、不经过这里，中文输入不受影响。
        if (workbench.markdownEditorAutoPair && !editor.readOnly
                && event.modifiers === Qt.NoModifier
                && event.text.length === 1
                && MarkdownEdit.autoType(editor, event.text))
            event.accepted = true
    }

    // 粘贴快捷键：显式认 Ctrl+V 与 Shift+Insert，不依赖 KeyEvent.matches
    // 在各 Qt 版本的枚举映射（macOS 的 Command 默认映射为 Control）。
    function isPasteShortcut(event) {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        const meta = (event.modifiers & Qt.MetaModifier) !== 0
        if (ctrl && !shift && !alt && !meta && event.key === Qt.Key_V)
            return true
        return ctrl && shift && !alt && !meta && event.key === Qt.Key_Insert
    }

    // 粘贴入口：返回 true 表示这次粘贴已被图片导入接管（成功或失败都算），
    // false 才走编辑器原生粘贴。
    function pasteInto(editor) {
        if (!editor || editor.readOnly)
            return false
        const result = workbench.importClipboardImage()
        if (!result.handled)
            return false
        editor.forceActiveFocus()
        if (!result.ok) {
            workbench.notify("error", qsTr("Could not insert image"),
                             result.error)
            return true
        }
        MarkdownEdit.insertMarkdown(editor, -1, result.markdown)
        return true
    }

    // 把导入结果插到落点：单张保持内联，多张每行一张；多张落在一行中间
    // 时前后补换行，让它成为独立块，整段仍是一次插入、一步撤销。
    function insertImportedImages(editor, position, content) {
        if (!content || content.length === 0)
            return false
        let text = content
        if (content.indexOf("\n") >= 0) {
            const before = position > 0
                    ? editor.text.charAt(position - 1) : "\n"
            const after = position < editor.text.length
                    ? editor.text.charAt(position) : "\n"
            if (before !== "\n")
                text = "\n" + text
            if (after !== "\n")
                text = text + "\n"
        }
        return MarkdownEdit.insertMarkdown(editor, position, text)
    }

    // 拖放悬停：外部文件或应用内文件树行才显示可放置光标。
    function handleEditorDragEntered(drag) {
        if (drag.hasUrls
                || (drag.source && drag.source.isFileReferenceDrag))
            drag.accept(Qt.CopyAction)
    }

    // 拖放落点：外部文件地址走 URL 导入；应用内文件树用 delegate 的绝对
    // 路径（path 是 required property）。都不是图片时，文件树行保持原来
    // 的反引号文件引用。
    function handleEditorDrop(editor, drop) {
        if (!editor || editor.readOnly)
            return
        const position = editor.positionAt(drop.x, drop.y)
        editor.forceActiveFocus()

        let result = null
        if (drop.hasUrls)
            result = workbench.importImageUrls(drop.urls)
        else if (drop.source && drop.source.isFileReferenceDrag
                 && drop.source.path)
            result = workbench.importImageFiles([drop.source.path])

        if (result !== null && result.handled) {
            if (!result.ok) {
                workbench.notify("error", qsTr("Could not insert image"),
                                 result.error)
            } else {
                insertImportedImages(editor, position, result.markdown)
            }
            drop.acceptProposedAction()
            return
        }

        if (drop.source && drop.source.isFileReferenceDrag) {
            editor.insert(position, drop.text)
            drop.acceptProposedAction()
        }
    }

    // 智能回车（A3）：列表/引用/任务项上回车延续前缀，空项回车退出；
    // 没有可延续的结构时放行默认换行。只读文档不接。
    function handleEditorReturn(editor, event) {
        if (workbench.markdownEditorSmartEnter && !editor.readOnly)
            event.accepted = MarkdownEdit.handleEnter(editor)
    }

    // --- 行级编辑动作（B 批次）：编辑器聚焦时的组合键路由 -------------------
    // 只处理行操作相关的修饰键组合，匹配到就执行并吞掉按键；没匹配到的
    // （普通输入、其它 Ctrl 组合）原样放行。Alt/Ctrl 组合只会在编辑器聚
    // 焦时到达这里，与页面级 Shortcut（Ctrl+F/Ctrl+H/Ctrl+G/Ctrl+S）不
    // 打架。
    function handleEditorKey(editor, event) {
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const key = event.key

        // Alt+↑/↓ 移动行、Shift+Alt+↓ 复制行、Ctrl+Shift+K 删行：纯编
        // 辑动作，只读文档不接（MarkdownEdit 直接走文档光标）。
        // Ctrl+L 选整行、Ctrl+D 选词不改文档，只读也放行。
        if (ctrl && shift && key === Qt.Key_K) {
            if (editor.readOnly)
                return false
            MarkdownEdit.deleteLine(editor)
        } else if (ctrl && !shift && key === Qt.Key_L) {
            MarkdownEdit.selectLine(editor)
        } else if (ctrl && !shift && key === Qt.Key_D) {
            MarkdownEdit.selectWord(editor)
        } else {
            if (alt && editor.readOnly)
                return false
            if (alt && !shift && key === Qt.Key_Up) {
                MarkdownEdit.moveLineUp(editor)
            } else if (alt && !shift && key === Qt.Key_Down) {
                MarkdownEdit.moveLineDown(editor)
            } else if (alt && shift && key === Qt.Key_Down) {
                MarkdownEdit.duplicateLine(editor)
            } else {
                // 没匹配到的组合原样放行（普通输入也走这里）。
                return false
            }
        }
        return true
    }

    // 在飞的落盘请求（"export" / "browser"），补渲染轮询结束后交给 requestMarkup
    property string pendingLanding: ""
    property int landingPolls: 0
    // 渲染失败的 toast 只报第一次：一份坏 mermaid 图能让页面报几十条
    property bool renderNoticeShown: false
    // 把磁盘那份写回编辑器时的护栏：否则这次写入会被算成用户的编辑，
    // 于是「重载」当场又变成「脏」。
    property bool applyingExternal: false

    // 命令泵的闸门：预览面装载完成且页面就绪。
    readonly property bool previewReady: previewPaneLoader.item
                                         && previewPaneLoader.item.pageReady

    // 当前激活的编辑器：提示词模式是草稿区；文档模式是源码区（只读文档
    // 没有可编辑目标，格式动作自然禁用）。
    readonly property var activeEditor: preset === "prompt"
                                        ? promptEditor
                                        : (markdown.canEdit ? editorArea : null)

    // --- 模板与收藏（动作层） -------------------------------------------
    // 「动作只有一处出口」：Ribbon 的 gallery、降级工具栏的菜单都只
    // 转发到这里。插入目标永远是草稿编辑器（文件 tab 是别人的文档，
    // 不往里插模板）。

    // 正在走变量表单流程的模板（{id, title}）；null = 无流程在飞。
    property var pendingTemplate: null

    // 体检结果（悬浮窗消费；null = 还没跑过——窗显示引导文案）。
    property var draftCheckResults: null

    // 两个非模态悬浮窗的开关（体检 / 历史）。它们不挡其它操作：可
    // 一直停留，也可关闭；命令条的按钮 checkable 态绑定在这里。
    property bool checksVisible: false
    property bool historyVisible: false

    // C4 预算条（草稿状态行的实时估算；-1 = 无草稿）。
    readonly property int draftTokens: preset === "prompt"
                                       && tools.draft
            ? tools.estimateTokens(tools.draft) : -1
    readonly property int draftBudgetLevel: draftTokens >= 0
            ? tools.budgetLevel(draftTokens) : 0

    // E6 文档版统计（同一估算函数）：文本取文件编辑器——它与预览共用
    // 同一份文档（session 的 MarkdownDocument），只在源码/分屏可见，
    // 不可见时数值冻结在最后一份，够用且零额外通道。无文件会话 = -1。
    readonly property int fileTabTokens: preset === "document"
                                         && editorArea
                                         && editorArea.text
            ? tools.estimateTokens(editorArea.text) : -1

    // 把文本插到草稿光标处（模板渲染与收藏插入共用的落点）。
    function insertTextAtCursor(text) {
        // 提示词预设与草稿激活不是同一件事（文档预设切回来草稿未激活
        // 时编辑器还是隐藏的）：切预设并确保草稿会话在场。
        applyPreset("prompt")
        promptEditor.forceActiveFocus()
        promptEditor.insert(promptEditor.cursorPosition, text)
    }

    // 完整的模板插入流程：无变量直插，有变量弹表单。
    function insertTemplate(id) {
        if (preset !== "prompt")
            applyPreset("prompt")
        const variables = tools.templateVariables(id)
        if (variables.length === 0) {
            // 无变量模板：选区仍参与渲染（内置 selection 变量），直接落。
            const text = tools.renderTemplate(id, {},
                                              promptEditor.selectedText)
            insertTextAtCursor(text)
            return
        }
        pendingTemplate = { id: id, title: templateTitleById(id) }
        variableForm.open()
        // 第一行拿焦点：模型绑 pendingTemplate（设值即建行），表单打开
        // 就能打字；行没建出来就静默（Esc/Cancel 都还可用）。
        const first = variableRepeater.itemAt(0)
        if (first)
            first.forceActiveFocus()
    }

    // 变量表单确认后的落地（表单调回；values 是 QVariantMap）。
    function completeTemplateInsert(values) {
        if (!pendingTemplate)
            return
        const text = tools.renderTemplate(
                    pendingTemplate.id, values,
                    promptEditor.selectedText)
        pendingTemplate = null
        insertTextAtCursor(text)
    }

    // 取消表单：流程清空，什么都不插。
    function cancelTemplateInsert() {
        pendingTemplate = null
        promptEditor.forceActiveFocus()
    }

    // 把当前草稿存为模板（弹小框改标题/分类/描述，预填标题 = 首行）。
    function saveCurrentAsTemplate() {
        templateTitleField.text = saveTemplateDialog.draftTitleGuess()
        saveTemplateDialog.open()
        templateTitleField.forceActiveFocus()
        templateTitleField.selectAll()
    }

    // 把当前草稿全文存为收藏（标题自动取首行，门面侧兜底）。
    function saveCurrentToFavorites() {
        if (tools.draft.length === 0) {
            workbench.notify("info", qsTr("Nothing to save"),
                             qsTr("The editor is empty."))
            return
        }
        tools.addFavorite("", promptEditor.text)
        workbench.notify("success", qsTr("Saved to favorites"),
                         qsTr("Insert it from the favorites panel."))
    }

    // 把一条收藏的正文插到草稿光标处（收藏 gallery 的动作出口：标题即 id，
    // 门面按标题找正文）。找不到（已被删）就什么都不做。
    function insertFavorite(title) {
        const all = tools.favorites
        for (let i = 0; i < all.length; ++i) {
            if (all[i].title === title) {
                insertTextAtCursor(all[i].body)
                return
            }
        }
    }

    // 模板 id → 显示标题（左栏与表单共用；空 id 给空串）。
    function templateTitleById(id) {
        const all = tools.templates
        for (let i = 0; i < all.length; ++i) {
            if (all[i].id === id)
                return all[i].title
        }
        return ""
    }

    // 草稿首行（存为模板对话框的标题预填来源）。
    function draftFirstLine() {
        return promptEditor.text.split("\n")[0] || ""
    }

    // 草稿全文（存为模板的正文来源——与渲染镜像同源，取编辑器的实时文本）。
    function draftFullText() {
        return promptEditor.text
    }

    // --- 草稿管理（多草稿的动作层，tab 条与右键菜单转发的就是这一批） ----
    // 新建并立即开始写它：编辑器与渲染会话都跟着 activeDraftIdChanged 走。
    function newDraft() {
        const result = tools.createDraft("", "", tools.currentWorkspace)
        if (!result.ok) {
            workbench.notify("error", qsTr("Could not create the draft"),
                             result.error)
            return
        }
        applyPreset("prompt")
    }

    // 重命名当前草稿（改名不改 id——会话恢复按 id 不受影响）。
    function renameCurrentDraft() {
        if (tools.activeDraftId.length === 0)
            return
        renameDraftField.text = draftTitleById(tools.activeDraftId)
        renameDraftDialog.open()
    }

    function doRenameDraft() {
        // QML JS 字符串只有 .trim()（QString 的 .trimmed() 在这里不存在，
        // 用错是静默 TypeError——绑与动作全断）。
        const title = renameDraftField.text.trim()
        if (title.length === 0)
            return
        const result = tools.renameDraft(tools.activeDraftId, title)
        if (result.ok)
            markdown.setDraftTitle(tools.activeDraftId, title)
        renameDraftDialog.close()
    }

    // 删除当前草稿：非空先确认（判据；空草稿直接删）。删除后控制器
    // 会话由 closeDraftSession 收掉，激活位顺延。
    function deleteCurrentDraft() {
        const id = tools.activeDraftId
        if (id.length === 0)
            return
        if (tools.draft.length > 0) {
            deleteDraftDialog.open()
            return
        }
        doDeleteDraft()
    }

    function doDeleteDraft() {
        const id = tools.activeDraftId
        markdown.closeDraftSession(id)
        const result = tools.removeDraft(id)
        if (!result.ok) {
            workbench.notify("error", qsTr("Could not delete the draft"),
                             result.error)
            return
        }
        // 门面把激活位顺延到下一条草稿；渲染会话跟上。
        activateActiveDraftSession()
        historyPopup.rebuild()
    }

    // 关掉当前草稿的 tab（关视图不删草稿——与文件 tab 同一语义）。
    function closeCurrentDraftTab() {
        if (tools.activeDraftId.length === 0)
            return
        markdown.closeDraftSession(tools.activeDraftId)
    }

    // 切换草稿（tab 点击与快照回滚后的兜底共用）：切走前存一版。
    function switchDraftTo(id) {
        if (id === tools.activeDraftId || id.length === 0)
            return
        if (promptEditor && tools.activeDraftId.length > 0)
            tools.addSnapshot(tools.activeDraftId,
                              promptEditor.text, "switch")
        tools.switchDraft(id)
    }

    // 快照：手动存一版当前草稿。
    function saveDraftVersion() {
        if (tools.activeDraftId.length === 0 || !promptEditor)
            return
        tools.addSnapshot(tools.activeDraftId, promptEditor.text, "manual")
        historyPopup.rebuild()
        workbench.notify("success", qsTr("Version saved"),
                         qsTr("It is in the draft's history list."))
    }

    // 回滚到某版快照：破坏性替换（当前版先自动存一版 rollback，门面
    // 侧做），确认对话框在这里。
    function rollbackDraftVersion(createdAt) {
        rollbackTargetCreatedAt = createdAt
        rollbackConfirm.open()
    }

    property double rollbackTargetCreatedAt: 0

    function doRollback() {
        const result = tools.rollbackToSnapshot(tools.activeDraftId,
                                                rollbackTargetCreatedAt)
        if (result.ok && promptEditor) {
            promptEditor.text = result.body
            draftSyncTimer.restart()
        }
        rollbackConfirm.close()
        historyPopup.rebuild()
    }

    // --- 预设应用（幂等：onCompleted 与意图写入各走一次也安全） ------------
    // 预设与活跃会话耦合：提示词预设激活当前草稿的会话（预览面渲染它），
    // 文档预设激活上次的文件会话（没有文件 tab 就退回提示词）。lastFileDocId
    // 记住「上一个活跃的文件会话」：prompt ⇄ document 来回切时回到看的那份，
    // 而不是总跳到最后打开的。
    property string lastFileDocId: ""

    // 文件 tab 子集（草稿 tab 混排后，「能不能切文档预设」只看文件）。
    function fileTabs() {
        const files = []
        for (let i = 0; i < markdown.tabs.length; ++i) {
            if (!markdown.tabs[i].draft)
                files.push(markdown.tabs[i])
        }
        return files
    }

    readonly property var dirtyFileTabs: {
        const dirty = []
        for (let i = 0; i < markdown.tabs.length; ++i) {
            if (markdown.tabs[i].dirty)
                dirty.push(markdown.tabs[i])
        }
        return dirty
    }

    // 草稿 id → 显示标题（tab 与会话激活共用）。
    function draftTitleById(id) {
        const all = tools.drafts
        for (let i = 0; i < all.length; ++i) {
            if (all[i].id === id)
                return all[i].title
        }
        return ""
    }

    // 激活当前草稿的渲染会话（幂等）：正文与标题从 tools 取——门面是
    // 草稿数据的唯一来源，窗口只做接线。
    function activateActiveDraftSession() {
        const id = tools.activeDraftId
        if (id.length === 0)
            return
        // 资源根 = 草稿绑定的工作区（或数据目录下的草稿资源目录），
        // 图片导入与预览用同一个结论。
        markdown.activateSession(
            markdown.openDraftSession(id, draftTitleById(id), tools.draft,
                                      workbench.draftAssetRoot(id)))
    }

    function applyPreset(requested) {
        const files = fileTabs()
        const documentUsable = markdown.available && files.length > 0
        if (requested === "document" && documentUsable) {
            preset = "document"
            if (markdown.draftActive || markdown.activeDocId.length === 0) {
                const target = lastFileDocId.length > 0
                        && fileTabs().some(t => t.docId === lastFileDocId)
                        ? lastFileDocId
                        : files[files.length - 1].docId
                markdown.activateSession(target)
            }
        }
        else {
            preset = "prompt"
            // 草稿会话承载提示词形态的渲染（虚拟文档）；已激活时是幂等的。
            activateActiveDraftSession()
        }
    }

    // --- 动作层（Ribbon 与降级工具栏转发的就是这一批） ---------------------
    function copyPrompt() {
        if (tools.draft.length === 0) {
            workbench.notify("info", qsTr("Nothing to copy"),
                             qsTr("The editor is empty."))
            return
        }
        // copy 是快照触发点（规划 06 §1.3）：交出去的版本进历史——
        // 「上次复制的那版」是回滚时最常找的一版。
        if (tools.activeDraftId.length > 0)
            tools.addSnapshot(tools.activeDraftId, tools.draft, "copy")
        workbench.copyText(tools.draft)
        workbench.notify("success", qsTr("Copied"),
                         qsTr("The prompt is on the clipboard."))
    }

    // 选目录走 ui.pickFolder（C++ 的原生 IFileOpenDialog，Qt 5/Qt 6 同一实
    // 现与同一行为；QML FolderDialog 是 Qt 6 QuickDialogs2 独有）。
    function addWorkspaceViaDialog() {
        const path = ui.pickFolder(qsTr("Choose a workspace folder"))
        if (path.length === 0)
            return
        const result = tools.addWorkspace(path)
        if (!result.ok)
            workbench.notify("error", qsTr("Could not add the workspace"),
                             result.error)
    }

    // 双击文件树的一行：打开这个文件。Markdown 走预览域开成新 tab（同
    // 路径去重、「开满了」的报错都在那边，失败由它发 toast），其余文件
    // 交给系统默认打开器——文件树的双击语义与系统文件管理器一致；无
    // 预览能力的构建里 .md 也落到系统打开器（工作台里没有能开它的地
    // 方）。往草稿插引用的入口是拖放与右键菜单，不再是双击。
    function openFileFromTree(path) {
        if (workbench.canPreviewMarkdown(path))
            workbench.previewMarkdown(path)
        else
            workbench.openLocalFileExternal(path)
    }

    // 格式动作与剪贴板动作都作用在当前编辑器上；焦点先归还，输入与
    // 选区写入才落在它身上（与 MarkdownContextMenu.run 同一条纪律）。
    // B1 补全后的全集（kind → MarkdownEdit 动作）。
    function applyFormat(kind) {
        const editor = activeEditor
        if (!editor)
            return
        editor.forceActiveFocus()
        if (kind === "bold")
            MarkdownEdit.applyBold(editor)
        else if (kind === "italic")
            MarkdownEdit.applyItalic(editor)
        else if (kind === "strike")
            MarkdownEdit.applyStrike(editor)
        else if (kind === "underline")
            MarkdownEdit.applyUnderline(editor)
        else if (kind === "code")
            MarkdownEdit.applyCode(editor)
        else if (kind === "bullet")
            MarkdownEdit.applyBullet(editor)
        else if (kind === "numbered")
            MarkdownEdit.applyNumbered(editor)
        else if (kind === "quote")
            MarkdownEdit.applyQuote(editor)
        else if (kind === "link")
            MarkdownEdit.applyLink(editor)
        else if (kind === "task")
            MarkdownEdit.applyTask(editor)
        else if (kind === "divider")
            MarkdownEdit.insertDivider(editor)
        else if (kind === "table")
            MarkdownEdit.insertTable(editor, 3)
        else if (kind === "heading1")
            MarkdownEdit.applyHeading(editor, 1)
        else if (kind === "heading2")
            MarkdownEdit.applyHeading(editor, 2)
        else if (kind === "heading3")
            MarkdownEdit.applyHeading(editor, 3)
        else if (kind === "heading0")
            MarkdownEdit.applyHeading(editor, 0)
        else if (kind === "headingUp")
            MarkdownEdit.promoteHeading(editor)
        else if (kind === "headingDown")
            MarkdownEdit.demoteHeading(editor)
    }

    // 围栏代码块 + 语言（Ribbon 的 Fence 菜单转发到这里）。
    function applyFence(language) {
        const editor = activeEditor
        if (!editor)
            return
        editor.forceActiveFocus()
        MarkdownEdit.applyFence(editor, language)
    }

    // --- 统一查找/替换（命令条查找框与替换框的唯一动作出口） -------------
    // 编辑器侧的一次「查找/下一个」（草稿区与源码区共用）：关键词变了就
    // 从头找，没变从选区尾部找；命中即选中。返回 {start, length, wrapped}。
    function findInEditor(editor, needle, changed) {
        const from = changed || editor.selectedText.length === 0
                ? 0 : editor.selectionEnd
        const hit = MarkdownEdit.find(
                    editor, needle, from,
                    tools.findCaseSensitive, tools.findWholeWords)
        if (hit.start >= 0)
            editor.select(hit.start, hit.start + hit.length)
        return hit
    }

    // 一次「查找/下一个」：文档模式查预览渲染区（分片 DOM 的查找），提
    // 示词模式查草稿源码。文档模式的替换走源码区，所以这里顺路给源码区
    // 挂上命中高亮（替换动作对齐选区要用）。
    function findNext(needle) {
        if (needle === null || needle === undefined)
            return
        const changed = needle !== findNeedle
        if (preset === "document" && markdown.available) {
            findNeedle = needle
            // restart：关键词变了从头，同一个词继续向下。
            markdown.requestFind(needle, true,
                                 tools.findCaseSensitive, changed)
            if (markdown.canEdit) {
                MarkdownEdit.setSearchHighlight(
                            editorArea, needle, tools.findCaseSensitive,
                            tools.findWholeWords)
            }
            refreshFindStats()
            return
        }
        if (needle.length === 0 || !promptEditor)
            return
        findNeedle = needle
        MarkdownEdit.setSearchHighlight(
                    promptEditor, needle, tools.findCaseSensitive,
                    tools.findWholeWords)
        const hit = findInEditor(promptEditor, needle, changed)
        if (hit.start < 0) {
            refreshFindStats()
            workbench.notify("info", qsTr("Find"),
                             qsTr("No more matches."))
            return
        }
        if (hit.wrapped)
            workbench.notify("info", qsTr("Find"),
                             qsTr("Searched from the start."))
        refreshFindStats()
    }

    // 取消查找：清关键词、把焦点还给编辑器（查找框留在命令条上，随用随
    // 输；「可取消」清的是搜索现场——包括命中的背景高亮与计数）。
    function cancelFind() {
        findNeedle = ""
        findMatchCount = 0
        findMatchIndex = 0
        if (promptEditor)
            MarkdownEdit.setSearchHighlight(promptEditor, "",
                                            tools.findCaseSensitive,
                                            tools.findWholeWords)
        if (editorArea)
            MarkdownEdit.setSearchHighlight(editorArea, "",
                                            tools.findCaseSensitive,
                                            tools.findWholeWords)
        if (promptEditor)
            promptEditor.forceActiveFocus()
    }

    // 查找统计（「N/M」）：分母 = 编辑器全文命中总数，分子 = 选区起点起
    // 的第一个命中序号。编辑器不在（文档模式只读）或关键词为空时归零；
    // 正文变化经 300 ms 防抖重算（逐键全篇扫描没有意义）。
    function refreshFindStats() {
        const editor = activeEditor
        if (findNeedle.length === 0 || !editor) {
            findMatchCount = 0
            findMatchIndex = 0
            return
        }
        findMatchCount = MarkdownEdit.findCount(
                    editor, findNeedle, tools.findCaseSensitive,
                    tools.findWholeWords)
        const from = editor.selectedText.length > 0
                ? editor.selectionStart : editor.cursorPosition
        findMatchIndex = MarkdownEdit.findIndexOf(
                    editor, findNeedle, from, tools.findCaseSensitive,
                    tools.findWholeWords)
    }

    // 选区是否正是 needle 的这次命中（替换动作的前置判据）。
    function selectionMatchesNeedle(editor, needle) {
        if (!editor || editor.selectedText.length !== needle.length)
            return false
        if (tools.findCaseSensitive)
            return editor.selectedText === needle
        return editor.selectedText.toLowerCase() === needle.toLowerCase()
    }

    // 替换当前命中并找下一个（替换框回车 / Replace 按钮）。选区不是这
    // 次命中（关键词刚换、还没找过）时先在编辑器里找一条对齐；全文没有
    // 命中时什么都不动。替换永远作用于源码文本（渲染区的 DOM 无从写
    // 回），文档模式下与 Ctrl+F 的分流是刻意的不对称。
    function replaceNext(replacement, needle) {
        if (needle === null || needle === undefined
                || needle.length === 0)
            return
        const editor = activeEditor
        if (!editor || editor.readOnly)
            return
        editor.forceActiveFocus()
        if (needle !== findNeedle)
            findNeedle = needle
        MarkdownEdit.setSearchHighlight(
                    editor, needle, tools.findCaseSensitive,
                    tools.findWholeWords)
        if (!selectionMatchesNeedle(editor, needle)) {
            const hit = findInEditor(editor, needle, true)
            if (hit.start < 0 || !selectionMatchesNeedle(editor, needle))
                return
        }
        const hit = MarkdownEdit.replaceAndFindNext(
                    editor, replacement, needle,
                    tools.findCaseSensitive, tools.findWholeWords)
        if (hit.start >= 0)
            editor.select(hit.start, hit.start + hit.length)
        refreshFindStats()
    }

    // 全部替换（Replace All 按钮）：一次撤销步全部回退，toast 报次数。
    function replaceEverywhere(replacement, needle) {
        if (needle === null || needle === undefined
                || needle.length === 0)
            return
        const editor = activeEditor
        if (!editor || editor.readOnly)
            return
        editor.forceActiveFocus()
        findNeedle = needle
        const count = MarkdownEdit.replaceAll(
                    editor, needle, replacement,
                    tools.findCaseSensitive, tools.findWholeWords)
        workbench.notify("success", qsTr("Replace"),
                         qsTr("Replaced %n occurrence(s).", "", count))
        refreshFindStats()
    }

    // 跳转到行（Ctrl+G）：行号 1 起，光标落到该行行首。提示词模式落草稿
    // 编辑器；文档模式落文件源码区（预览态没有源码面，提示切回源码）。
    // 只读文档同样可导航——跳转只动光标。
    function gotoLineNumber(line) {
        const target = preset === "prompt"
                ? promptEditor
                : (docArea.showsEditor && !markdown.draftActive
                   ? editorArea : null)
        if (!target) {
            workbench.notify("info", qsTr("Go to line"),
                             qsTr("Switch to the source view first."))
            return
        }
        const lines = target.text.split("\n")
        const clamped = Math.max(1, Math.min(line, lines.length))
        let offset = 0
        for (let i = 0; i < clamped - 1; ++i)
            offset += lines[i].length + 1
        target.cursorPosition = offset
        target.forceActiveFocus()
    }

    // 跳转对话框的确认：解析行号并跳转（校验器已挡掉非数字）。
    function doGotoLine() {
        const line = parseInt(gotoLineField.text)
        gotoLineDialog.close()
        if (!line || line < 1)
            return
        gotoLineNumber(line)
    }

    // --- 体检与历史（非模态悬浮窗的开关） ---------------------------------
    // 开体检窗顺路跑一次（没跑过时窗里只有引导文案，开窗即出结果）。
    function toggleChecksWindow() {
        checksVisible = !checksVisible
        if (checksVisible && draftCheckResults === null)
            draftCheckResults = tools.runChecks(tools.draft)
    }

    function toggleHistoryWindow() {
        historyVisible = !historyVisible
    }

    // 跑一次体检并把结果存进页面状态（悬浮窗消费）。文本来自草稿
    // （体检是提示词质量工具，文件 tab 上没有意义）。
    function runDraftChecks() {
        draftCheckResults = tools.runChecks(tools.draft)
    }

    // 选中草稿编辑器的一段区间并聚焦（体检命中行的跳转落点）。
    function selectDraftRange(start, length) {
        if (!promptEditor)
            return
        promptEditor.select(start, start + length)
        promptEditor.forceActiveFocus()
    }

    function editorAction(action) {
        const editor = activeEditor
        if (!editor)
            return
        editor.forceActiveFocus()
        if (action === "undo")
            editor.undo()
        else if (action === "redo")
            editor.redo()
        else if (action === "cut")
            editor.cut()
        else if (action === "copy")
            editor.copy()
        else if (action === "paste")
            pasteInto(editor)
    }

    function saveDocument() {
        const result = markdown.saveNow()
        if (!result.ok)
            workbench.notify("error", qsTr("Could not save"), result.error)
        // 成功那条 toast 在 onSaved 里发：存盘的路径不止这一条（菜单、快捷键），
        // 而「存下了」这件事只由控制器的 saved 信号定义。
        return result.ok
    }

    function toggleRibbonCollapsed() {
        setRibbonCollapsed(!ribbonCollapsed)
    }

    // Ribbon 的折叠位写回（用户在 ribbon 上直接展开/折叠时由它的
    // onMinimumModeChanged 调到这里）。
    function setRibbonCollapsed(collapsed) {
        if (preset === "prompt")
            tools.ribbonCollapsed = collapsed
        else
            documentRibbonCollapsed = collapsed
    }

    // 切命令条的标签页（tab 序 = 开始 0 / 提示词 1 / 视图 2）。测试与
    // 页面内跳转共用（gallery 的格子只在其 tab 被实例化时存在）。
    function setRibbonTab(index) {
        const bar = commandBarLoader.item
                   ? commandBarLoader.item : null
        if (bar && bar.setTabIndex)
            bar.setTabIndex(index)
    }

    // 命令泵：控制器排队的命令逐条交给页面。顺序由队列保证（同类命令只
    // 留最新一条），而 runJavaScript 的调用顺序就是页面里的执行顺序。
    // 唯一的例外是 serialize：它的返回值本身就是导出件，必须从回调里取。
    function pump() {
        if (!previewReady)
            return
        while (markdown.hasPendingCommand()) {
            const json = markdown.takeCommand()
            // 命令名从 JSON 里解出来，而不是在字符串里找子串：命令体是
            // MarkdownRuntime 紧凑序列化的产物，spacing 一变这里就会静默
            // 不再认出 serialize，于是导出永远不落地、也没有任何报错。
            let cmd = ""
            try {
                cmd = JSON.parse(json).cmd
            } catch (e) {
                console.error("markdown: undecodable command from the queue")
                continue
            }
            previewPaneLoader.item.dispatch(
                        json,
                        cmd !== "serialize" ? null : function (reply) {
                            markdown.reportMarkup(
                                        typeof reply === "string" ? reply : "")
                        })
        }
    }

    // 导出 / 外部打开前的补渲染：屏外块与懒画图表都是异步的，直接序列化
    // 会得到一堆占位符。轮询页面自己的 AWB.idle() 而不是猜一个固定延时。
    function requestLanding(purpose) {
        pendingLanding = purpose
        landingPolls = 0
        markdown.requestFullRender()
        idlePoll.restart()
    }

    // 数一次轮询并回报当前次数：到预算就照样序列化，宁可不完美也不永远不落地。
    function spentIdlePoll() {
        landingPolls += 1
        return landingPolls
    }

    // 选区在 Chromium 里，只能把文本取回来交给剪贴板（QML 不碰系统能力，
    // 一律经 workbench）。
    function copySelection() {
        previewPaneLoader.item.evalJs(
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

    // 会话落盘：把页面现场（预设/开合）交给 workbench 合成（tab 组与激活位
    // 从控制器取）。结构变化立即的「立即」是防抖 800 ms——脏标记翻转也会
    // 触发 tabsChanged，逐键落盘没必要。
    function scheduleSessionSave() {
        sessionSaveTimer.restart()
    }

    function flushSession() {
        sessionSaveTimer.stop()
        workbench.saveMarkdownSession({
            "preset": preset,
            "promptLeftOpen": promptLeftOpen,
            "documentLeftOpen": documentLeftOpen,
            "documentRibbonCollapsed": documentRibbonCollapsed
        })
    }

    onPresetChanged: scheduleSessionSave()
    onPromptLeftOpenChanged: scheduleSessionSave()
    onDocumentLeftOpenChanged: scheduleSessionSave()
    onDocumentRibbonCollapsedChanged: scheduleSessionSave()

    Timer {
        id: sessionSaveTimer
        interval: 800
        onTriggered: workbenchWindow.flushSession()
    }

    // 建页即向控制器报到：从此它才往外排命令（页面不在时命令一律不排，
    // 页面常驻到应用退出为止）。一条草稿都没有（全新安装）时先建一条
    // ——提示词形态需要一个可写的主体。
    // 控制器里已有会话（argv 双击 .md / 单实例打开后按需建页）时**不**做
    // 会话恢复、不动活跃位：活跃会话就是用户要看的，预设据此落位。tab
    // 组恢复只在控制器全空（本轮应用的第一次建页）时做。
    Component.onCompleted: {
        markdown.notifyWindowOpened()
        if (tools.activeDraftId.length === 0)
            tools.createDraft("", "", tools.currentWorkspace)
        var restored = null
        if (markdown.tabs.length === 0)
            restored = workbench.restoreMarkdownSession()
        if (restored && restored.restored) {
            promptLeftOpen = restored.promptLeftOpen
            documentLeftOpen = restored.documentLeftOpen
            documentRibbonCollapsed = restored.documentRibbonCollapsed
        }
        // 预设起点按控制器现状推断：活跃的是文件会话（恢复现场重开的、
        // 或预览请求刚装载的）= 文档模式；否则提示词模式（草稿会话由此
        // 激活）。restoreMarkdownSession 已把记录的激活位落在控制器里，
        // 这里的推断与它天然一致。
        applyPreset(!markdown.draftActive
                    && markdown.activeDocId.length > 0 ? "document"
                                                       : "prompt")
        if (!markdown.canEdit && markdown.mode === "source")
            markdown.setMode("preview")
        // 崩溃恢复（D6）：有未恢复的副本就问一次（新对话框，不挡正常
        // 路径——没有副本时什么都不弹）。门面没有副本时给的是**空
        // map**（QML 里是 {}，不是 null）：判「有没有 draftId 键」，
        // 判 null 会每次都弹。
        const recovery = tools.pendingRecovery()
        if (recovery && recovery.draftId !== undefined)
            recoveryDialog.open()
    }



    // --- 快捷键 ------------------------------------------------------------
    // context 用 ApplicationShortcut + enabled 绑 pageCurrent（本页是当前
    // 页）：Chromium 会吃掉普通作用域的按键，而本页常驻（keepAlive），
    // 不门控就会在用户切到别的页面后继续抢 Ctrl+S / Ctrl+F。
    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        enabled: pageCurrent && preset === "document"
                 && markdown.canEdit
        onActivated: workbenchWindow.saveDocument()
    }
    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.ApplicationShortcut
        // 焦点直达命令条查找框（两种预设同一个去向：findNext 内部按
        // 预设分流到草稿源码或预览渲染区）。
        enabled: pageCurrent && commandBarLoader.item
        onActivated: commandBarLoader.item.focusFind()
    }
    // 替换入口：焦点直达命令条替换框（动作与查找共用一套关键词/偏好）。
    Shortcut {
        sequence: "Ctrl+H"
        context: Qt.ApplicationShortcut
        enabled: pageCurrent && commandBarLoader.item
                 && commandBarLoader.item.focusReplace
        onActivated: commandBarLoader.item.focusReplace()
    }
    // 跳转到行（1 起；提示词模式落草稿，文档模式落源码区）。
    Shortcut {
        sequence: "Ctrl+G"
        context: Qt.ApplicationShortcut
        enabled: pageCurrent
        onActivated: gotoLineDialog.open()
    }
    // F5 是「重读磁盘」而不是「重载页面」：内容本来就在本地，重载视图只会
    // 把已经挂好的 DOM 丢掉重来一遍。
    Shortcut {
        sequence: "F5"
        context: Qt.ApplicationShortcut
        enabled: pageCurrent && preset === "document"
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

        // --- 顶部命令条：Ribbon 构建里是 RibbonBar，其余走工具栏降级 ----
        // Ribbon 的 QML 只在 AWB_ENABLE_RIBBON 的构建里登记进资源清单；
        // 未登记（开关关闭）或当前进程没注册 SARibbon 类型（单测引擎）时
        // Loader 报错，就地换装载降级工具栏——能力按编译内容裁剪，与
        // 预览面的装载同一条路线。
        Loader {
            id: commandBarLoader

            Layout.fillWidth: true
            // Ribbon 折叠态的 category 预览要盖住下方内容：预览越过命令条
            // 底边绘制，而 z 只在兄弟之间比较，宿主 Loader 必须跟 ribbon
            // 一起抬升（两份命令条皮都暴露 categoryPreviewUp 同一签名）。
            z: item && item.categoryPreviewUp ? 20 : 0
            source: "qrc:/qt/qml/AgentWorkbench/workbench/WorkbenchRibbon.qml"

            onStatusChanged: {
                if (status === Loader.Ready) {
                    item.win = workbenchWindow
                    return
                }
                if (status === Loader.Error) {
                    console.warn("workbench: ribbon unavailable, "
                                 + "falling back to the toolbar")
                    source = "qrc:/qt/qml/AgentWorkbench/workbench/WorkbenchToolbar.qml"
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: theme.separator
        }

        // --- 主体：左侧属性区 | 中间文档区 | 右侧属性区 -------------------
        RowLayout {
            id: workbenchRow

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // --- 左侧：工作区文件树（宽度可拖） ---------------------------
            // 面板本体在 WorkspaceTreePanel 里（树、工作区切换、空态、窄边
            // 还原按钮、内部宽度）；这里只接线：开合落到两种预设各自的状态
            //（它们要落盘），预览能力由 workbench 决定。
            // 宽度经右缘分隔条拖拽（与编辑/预览分栏同一交互）；拖到下限
            // 以下、以及段头上的收起键，都把整个左栏收成窄边——窄边上只留
            // 展开按钮，它是收起态唯一的还原入口。提示词预设默认开，文档
            // 预设默认收起。
            WorkspaceTreePanel {
                id: leftPanel

                expanded: leftOpen
                canPreview: function(path) {
                    return workbench.canPreviewMarkdown(path)
                }

                onExpandRequested: {
                    if (preset === "prompt")
                        promptLeftOpen = true
                    else
                        documentLeftOpen = true
                }
                onCollapseRequested: {
                    if (preset === "prompt")
                        promptLeftOpen = false
                    else
                        documentLeftOpen = false
                }
                onAddWorkspaceRequested: workbenchWindow.addWorkspaceViaDialog()
                onFileActivated: function(path) {
                    workbenchWindow.openFileFromTree(path)
                }
                onReferenceRequested: function(reference) {
                    workbenchWindow.insertTextAtCursor(reference)
                }
                onCopyRequested: function(text) {
                    workbench.copyText(text)
                }
                onPreviewRequested: function(path) {
                    workbench.previewMarkdown(path)
                }
                onNotifyRequested: function(level, title, body) {
                    workbench.notify(level, title, body)
                }
            }

            // --- 左栏宽度手柄：视觉 4px，命中 12px -------------------------
            // 与编辑/预览分栏的分隔条共用 ASplitHandle：同一交互只有一处
            // 实现，两条分隔条各自的「位置代表什么」留在下面两个处理器里。
            // 拖动在 180–520 之间改宽度；拖到 140 以下（带 40px 滞回，避免
            // 贴边抖动）收起左栏——展开入口留在窄边上。收起态手柄退场
            //（窄边的展开按钮接手）。
            ASplitHandle {
                id: leftPanelHandle
                objectName: "leftPanelHandle"

                Layout.fillHeight: true
                Layout.preferredWidth: hitWidth
                visible: leftOpen

                visualWidth: 4
                barRadius: 2
                // 坐标换算到承载左栏的那一行：宽度按指针在这一行里的
                // 绝对位置算。
                host: workbenchRow

                // 抓取点在行内的偏移（按下时记录）。宽度一律按指针的绝对
                // 位置算（与 docArea 分隔条同一条纪律），且必须减这个
                // **恒定**的按下偏移：手柄会随面板宽度跟着挪位置，逐事件
                // 读局部坐标的话它含着本轮拖动的位移，两项相减恒等于面板
                // 当前宽度，拖动就成了无操作（早前版本减 leftPanel.width
                // 更是把任何拖动都算成负宽度、直接收起——2026-10 用户报告
                // 「一拖就收起」的根因）。
                property real grabOffset: 0

                onDragStarted: function(x, y) {
                    leftPanelHandle.grabOffset = x - leftPanel.width
                }
                onDragged: function(x, y) {
                    const width = x - leftPanelHandle.grabOffset
                    if (width < 140) {
                        if (leftOpen) {
                            if (preset === "prompt")
                                promptLeftOpen = false
                            else
                                documentLeftOpen = false
                        }
                        return
                    }
                    leftPanel.panelWidth = Math.max(180, Math.min(520, width))
                }
            }

            // --- 中间：文档区 ----------------------------------------------
            // 文件 tab 条 + 三态 + 状态条。tab 只装文件会话（草稿是提示词
            // 形态的隐式主体，不占 tab；多草稿并入 tab 条是 M4 的事）。
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // tab 条：一条贴顶的窄条。文件 tab 与草稿 tab 混排（草稿
                // 在前，M4）：都可点（激活 + 切到各自预设）、都可关（关视图
                // 不删东西）；脏标记以圆点呈现，不只靠颜色；草稿标题斜体
                // 加草稿徽标，与文件 tab 一眼可分。
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 34
                    color: theme.chromeBg

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: theme.spacingS
                        anchors.rightMargin: theme.spacingS
                        spacing: theme.spacingS

                        Repeater {
                            model: markdown.tabs

                            delegate: Item {
                                id: fileTab
                                // 测试锚点（tst_ribbonui 经真实点击驱动
                                // TapHandler）：tab 会随会话增减，名字只标
                                // 类别不标序号。
                                objectName: "fileTab"
                                required property var modelData
                                required property int index

                                // 根用 Item + 视觉背景放内层：delegate 的
                                // required property 与同名视觉属性的遮蔽坑
                                //（AGENTS.md QML 契约）。
                                readonly property bool isActive:
                                    fileTab.modelData.active

                                // 作用域显式传入：点 tab 要调页面动作层的
                                // applyPreset/switchDraftTo，而编译 QML 的
                                // delegate 信号处理器里直接写文档根 id
                                //（workbenchWindow）会 ReferenceError——上下
                                // 文件作用域的名字（markdown 等）经作用域链
                                // 照常可达，id 却解析不到。挂成 delegate 根
                                // 的属性后，处理器经对象作用域链取它，解释
                                // 与编译两条路都成立。
                                property var scope: workbenchWindow

                                implicitWidth: tabRow.implicitWidth + theme.spacingS * 2
                                implicitHeight: 34

                                Rectangle {
                                    anchors.fill: parent
                                    radius: theme.radiusControl
                                    color: fileTab.isActive
                                          ? theme.surfaceBg : "transparent"
                                    border.width: fileTab.isActive ? 1 : 0
                                    border.color: theme.borderSubtle
                                }

                                RowLayout {
                                    id: tabRow
                                    anchors.centerIn: parent
                                    spacing: theme.spacingXs

                                    // 脏标记：一个 6 px 的圆点，标题旁。
                                    Rectangle {
                                        visible: fileTab.modelData.dirty
                                        implicitWidth: 6
                                        implicitHeight: 6
                                        radius: 3
                                        color: theme.warning
                                    }

                                    // 草稿徽标：小圆点（强调色）——文件
                                    // tab 只有脏点，草稿 tab 恒有草稿点。
                                    Rectangle {
                                        visible: fileTab.modelData.draft
                                        implicitWidth: 6
                                        implicitHeight: 6
                                        radius: 3
                                        color: theme.accent
                                    }

                                    Label {
                                        text: fileTab.modelData.title
                                        color: fileTab.isActive
                                              ? theme.textPrimary
                                              : theme.textSecondary
                                        font.pixelSize: theme.fontSizeCaption
                                        font.italic: fileTab.modelData.draft
                                        elide: Text.ElideMiddle
                                    }

                                    AIconButton {
                                        iconSource: "qrc:/icons/close.svg"
                                        tooltip: qsTr("Close this tab")
                                        onClicked: {
                                            if (fileTab.modelData.draft)
                                                markdown.closeDraftSession(
                                                    fileTab.modelData.docId)
                                            else
                                                markdown.closeSession(
                                                    fileTab.modelData.docId)
                                        }
                                    }
                                }

                                // 点 tab：激活该会话并切到各自预设——草稿
                                // tab 的编辑对象也要跟过去（门面侧换当前
                                // 草稿）。点关闭按钮不该走到这里——
                                // AIconButton 自己吃点击。
                                // 页面引用一律在触碰模型之前先取进局部
                                // 变量：activateSession/switchDraftTo 会同步
                                // 发 tabsChanged，tab 条的 Repeater 随之重建，
                                // 本 delegate 的作用域当场失效——此后再解析
                                // scope 就抛 ReferenceError，预设切换静默死
                                // 在 handler 半路（2026-10 用户报告的
                                // 「点文件 tab 左栏整个收起」）。局部变量活在
                                // JS 栈上，不受 delegate 销毁影响。
                                TapHandler {
                                    onSingleTapped: {
                                        const page = scope
                                        const tab = fileTab.modelData
                                        if (tab.draft) {
                                            page.switchDraftTo(tab.docId)
                                            page.applyPreset("prompt")
                                        }
                                        else {
                                            markdown.activateSession(tab.docId)
                                            page.applyPreset("document")
                                        }
                                    }
                                }

                                // 右键：草稿 tab 给「重命名/删除草稿」（命令
                                // 条不再放这两个动作），文件 tab 只给关闭。
                                // 只吃右键，左键留给上面的 TapHandler；坐标
                                // 换算到页面根（菜单的坐标系），页面引用经
                                // scope（delegate 处理器里裸根 id 会
                                // ReferenceError，TapHandler 同一条纪律）。
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onClicked: function(mouse) {
                                        const page = scope
                                        const tab = fileTab.modelData
                                        const at = mapToItem(page,
                                                             mouse.x, mouse.y)
                                        tabContextMenu.tabDocId = tab.docId
                                        tabContextMenu.draftTab
                                                = tab.draft ? tab.docId : ""
                                        tabContextMenu.popup(at.x, at.y)
                                    }
                                }
                            }
                        }

                        // tab 条只回答「在哪份文档」：视图选项住在命令条
                        //（Ribbon「视图」页 / 降级工具栏），不混进 tab 条。

                        // 右端两个固定件：新建草稿（+）与文档模式的更多
                        // 动作（保存/导出/重载——命令条不含文档模式的保存
                        // 专属动作，收进这里）。
                        Item {
                            Layout.fillWidth: true
                        }

                        AIconButton {
                            objectName: "newDraftTabButton"
                            iconSource: "qrc:/icons/plus.svg"
                            tooltip: qsTr("New draft")
                            onClicked: workbenchWindow.newDraft()
                        }

                        AIconButton {
                            visible: preset === "document"
                            iconSource: "qrc:/icons/menu.svg"
                            tooltip: qsTr("More actions")
                            onClicked: actionsMenu.popup()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: theme.separator
                }

                // --- 文档主体 ----------------------------------------------
                // 一套两栏结构服务两种预设：编辑侧是草稿编辑器（草稿会话
                // 激活）或文件源码区（文件会话激活），预览侧渲染活跃会话
                // ——渲染管线吃文本不吃文件，草稿与文档同一条。草稿区与
                // 两栏结构是兄弟：位置对齐到编辑槽位（splitX），预览面与
                // 分隔条在 docArea 里。不用 SplitView 是因为三态里有两态
                // 要把一栏挤成 0 宽，而 SplitView 对 visible 的处理跨版本
                // 不一致。
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // 草稿编辑区（提示词形态的源码面；objectName 是
                    // tst_toolsui 的定位锚点）。
                    Flickable {
                        id: promptScroll
                        objectName: "promptScroll"

                        x: 0
                        y: 0
                        width: docArea.splitX
                        height: parent.height
                        visible: docArea.showsEditor && markdown.draftActive
                                 && width > 0
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        // 折行显示，宽度即视口宽度，横向永不溢出。
                        flickableDirection: Flickable.VerticalFlick

                        ScrollBar.vertical: AScrollBar {
                            objectName: "promptScrollBar"
                        }

                        TextArea.flickable: ATextArea {
                            id: promptEditor
                            objectName: "promptEditor"

                            // Ctrl+滚轮缩放的字号（0 = 跟随主题）。
                            font.pixelSize: workbenchWindow.editorFontResolved

                            placeholderText: qsTr("Write your prompt here. Nothing is sent from this window.")
                            // Tab：多行选区整段缩进（VSCode 语义），单行/
                            // 无选区保持原生插入；Keys 附加属性先于编辑器
                            // 自身的按键处理，处理后不再走默认的「替换选区」。
                            Keys.onTabPressed:
                                workbenchWindow.handleEditorTab(promptEditor)
                            // Shift+Tab：同一批行的反向动作（退一个缩进
                            // 单位，无选区时是光标所在行）。
                            Keys.onBacktabPressed:
                                workbenchWindow.handleEditorBacktab(promptEditor)
                            // 输入手感：格式化快捷键先于窗口级 Shortcut
                            //（Keys.onShortcutOverride），回车延续列表，
                            // 单字符输入自动配对标记；全部可在设置关闭。
                            // 行级动作组合键（Alt+↑/↓ 移动、Shift+Alt+↓
                            // 复制、Ctrl+Shift+K 删行、Ctrl+L 选行、Ctrl+D
                            // 选词）先路由；没匹配到的再走输入手感这层。
                            Keys.onShortcutOverride: function(event) {
                                workbenchWindow.handleEditorShortcutOverride(
                                            promptEditor, event)
                            }
                            Keys.onPressed: function(event) {
                                if (workbenchWindow.handleEditorKey(
                                        promptEditor, event)) {
                                    event.accepted = true
                                    return
                                }
                                workbenchWindow.handleEditorKeyPressed(
                                            promptEditor, event)
                            }
                            Keys.onReturnPressed: function(event) {
                                workbenchWindow.handleEditorReturn(
                                            promptEditor, event)
                            }
                            Keys.onEnterPressed: function(event) {
                                workbenchWindow.handleEditorReturn(
                                            promptEditor, event)
                            }
                            // 只在初始化时从门面取草稿：常驻双向绑定会与
                            // 用户输入互相打架。
                            Component.onCompleted: {
                                text = tools.draft
                                // markdown 语法高亮在此时挂到 textDocument 上；
                                // 主题切换由 MarkdownEdit 自己跟随。
                                MarkdownEdit.attach(promptEditor)
                            }
                            // 双写：tools.draft 走门面自己的 500 ms 防抖落
                            // tools.json（写的是**当前**草稿）；草稿会话走
                            // 200 ms 防抖——一次敲键要整篇重切块，逐键喂
                            // 是把 CPU 花在马上会被改掉的中间态上。
                            onTextChanged: {
                                tools.draft = text
                                draftSyncTimer.restart()
                                findStatsTimer.restart()
                            }

                            // 拖放目标：外部图片落成 ![alt](路径)；文件树
                            // 行按同一套规则处理，非图片保持反引号引用。
                            DropArea {
                                anchors.fill: parent

                                onEntered: function(drag) {
                                    workbenchWindow.handleEditorDragEntered(
                                                drag)
                                }
                                onDropped: function(drop) {
                                    workbenchWindow.handleEditorDrop(
                                                promptEditor, drop)
                                }
                            }

                            // 右键弹 markdown 异型菜单（顶部格式化工具栏 +
                            // 复制/粘贴）。TextEdit 只吃左/中键，右键不触碰
                            // 编辑器选区；MouseArea 只接受右键，鼠标处给
                            // I-beam 光标。
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                cursorShape: Qt.IBeamCursor
                                onClicked: function(mouse) {
                                    editorMenu.editor = promptEditor
                                    editorMenu.popup(mouse.x, mouse.y)
                                }
                            }

                            MarkdownContextMenu {
                                id: editorMenu
                                objectName: "editorMenu"
                                onPasteRequested: workbenchWindow.pasteInto(
                                                      editorMenu.editor)
                            }
                        }
                    }

                    // 草稿会话的增量喂入（与 editorSyncTimer 同一条纪律）。
                    // 恢复副本也在这里顺路写（2 s 停顿节流由门面侧做）。
                    Timer {
                        id: draftSyncTimer
                        interval: 200
                        onTriggered: {
                            if (tools.activeDraftId.length === 0)
                                return
                            markdown.setDraftText(tools.activeDraftId,
                                                  promptEditor.text)
                            tools.writeRecoverySnapshot(
                                        tools.activeDraftId,
                                        promptEditor.text)
                        }
                    }

                    // 查找统计的防抖重算：正文编辑后 300 ms 再全篇扫描
                    //（逐键扫没有意义——中间态马上会被改掉）。
                    Timer {
                        id: findStatsTimer
                        interval: 300
                        onTriggered: workbenchWindow.refreshFindStats()
                    }

                    // 两栏主体（编辑槽位 + 预览面 + 分隔条）。
                    Item {
                        id: docArea
                        anchors.fill: parent

                    readonly property bool showsEditor: !markdown.available
                                                       || markdown.mode !== "preview"
                    readonly property bool showsView: markdown.available
                                                     && markdown.mode !== "source"
                        readonly property bool showsBoth: showsEditor && showsView
                        // 分隔条的位置：单栏时被推到边界，于是另一栏自然
                        // 0 宽。
                        property real splitX: showsBoth
                                              ? Math.round(width * workbenchWindow.editorShare)
                                              : (showsEditor ? width : 0)

                        // --- 编辑侧（文件源码区） ---
                        Flickable {
                            id: editorScroll
                            x: 0
                            y: 0
                            width: docArea.splitX
                            height: parent.height
                            visible: docArea.showsEditor
                                     && !markdown.draftActive && width > 0
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            flickableDirection: Flickable.VerticalFlick

                            ScrollBar.vertical: AScrollBar {}

                            // 与草稿区同一套做法：TextArea 自己不会滚动，
                            // TextArea.flickable 附加属性接管增高与光标可见。
                            TextArea.flickable: ATextArea {
                                id: editorArea

                                // Ctrl+滚轮缩放的字号（0 = 跟随主题）。
                                font.pixelSize: workbenchWindow.editorFontResolved

                                // Tab：与草稿区同一动作（只读由
                                // handleEditorTab 门控）。
                                Keys.onTabPressed:
                                    workbenchWindow.handleEditorTab(editorArea)
                                // Shift+Tab：同一批行的反向动作。
                                Keys.onBacktabPressed:
                                    workbenchWindow.handleEditorBacktab(editorArea)
                                // 输入手感：与草稿区同一组处理器（快捷键
                                // /回车/配对都按各自的设置门控）；行级动作
                                // 组合键先路由，与草稿区同一批。
                                Keys.onShortcutOverride: function(event) {
                                    workbenchWindow.handleEditorShortcutOverride(
                                                editorArea, event)
                                }
                                Keys.onPressed: function(event) {
                                    if (workbenchWindow.handleEditorKey(
                                            editorArea, event)) {
                                        event.accepted = true
                                        return
                                    }
                                    workbenchWindow.handleEditorKeyPressed(
                                                editorArea, event)
                                }
                                Keys.onReturnPressed: function(event) {
                                    workbenchWindow.handleEditorReturn(
                                                editorArea, event)
                                }
                                Keys.onEnterPressed: function(event) {
                                    workbenchWindow.handleEditorReturn(
                                                editorArea, event)
                                }

                                // 只在初始化与「磁盘那份进来了」时写 text：
                                // 常驻双向绑定会与用户输入互相打架。
                                Component.onCompleted: {
                                    text = markdown.editorText()
                                    // 高亮与草稿区同一套（统一编辑器）：
                                    // 只读文档也高亮，读源码同样受益。
                                    MarkdownEdit.attach(editorArea)
                                }
                                placeholderText: qsTr("Nothing to edit")
                                readOnly: !markdown.canEdit
                                // 源码用等宽：表格与代码块的对齐依赖它。
                                font.family: theme.monoFamily.length > 0
                                             ? theme.monoFamily : theme.family
                                // 停手再交出去：一次敲键要把整篇重切块并算
                                // 差异，2 MB 文档上每键一次就是把 CPU 花在
                                // 马上会被改掉的中间态。
                                onTextChanged: {
                                    if (workbenchWindow.applyingExternal)
                                        return
                                    editorSyncTimer.restart()
                                    findStatsTimer.restart()
                                }

                                // 与草稿区同一条拖放规则；只读文档不接受。
                                DropArea {
                                    anchors.fill: parent
                                    enabled: !editorArea.readOnly
                                    onEntered: function(drag) {
                                        workbenchWindow.handleEditorDragEntered(
                                                    drag)
                                    }
                                    onDropped: function(drop) {
                                        workbenchWindow.handleEditorDrop(
                                                    editorArea, drop)
                                    }
                                }
                            }
                        }

                        Timer {
                            id: editorSyncTimer
                            interval: 200
                            onTriggered: markdown.setEditorText(editorArea.text)
                        }

                        // --- 预览区（Chromium 渲染面，按能力惰性装载） ---
                        Loader {
                            id: previewPaneLoader

                            x: docArea.splitX + (docArea.showsBoth ? handle.width : 0)
                            y: 0
                            width: parent.width - x
                            height: parent.height
                            visible: docArea.showsView && width > 0
                            // 预览面只在真有预览能力的构建里存在（资源
                            // 清单按同一条件登记），没有它文档模式整个
                            // 不成立——applyPreset 会退回提示词模式。
                            active: markdown.available
                            source: "qrc:/qt/qml/AgentWorkbench/markdown/MarkdownPreviewPane.qml"

                            onStatusChanged: {
                                if (status === Loader.Error) {
                                    console.error(
                                        "markdown: failed to load the preview pane",
                                        source)
                                    workbench.notify(
                                        "error",
                                        qsTr("Preview failed"),
                                        qsTr("The preview page could not be loaded."))
                                }
                            }
                        }

                        // --- 分隔条：视觉 4px，命中 12px -------------------
                        // 与左栏宽度手柄同一个 ASplitHandle；这里位置代表
                        // 比例，换算留在 onDragged 里。
                        ASplitHandle {
                            id: handle
                            x: docArea.splitX
                            y: 0
                            width: docArea.showsBoth ? theme.spacingXs : 0
                            height: parent.height
                            visible: width > 0

                            visualWidth: theme.spacingXs
                            host: docArea
                            interactive: docArea.showsBoth

                            // 用绝对坐标算比例：相对增量要在 docArea 坐标系
                            // 里折，而 x / width 本来就是这里唯一需要的两个
                            // 数。
                            onDragged: function(x, y) {
                                workbenchWindow.editorShare = Math.max(
                                            0.15,
                                            Math.min(0.75, x / docArea.width))
                            }
                        }

                        // --- 悬浮大纲：渲染窗自己的导航（2026-10 自右栏迁来） --
                        // 大纲是「正在读的这份文档」的导航，住在渲染窗右缘、
                        // 悬浮于内容之上：不占布局（塞进右栏会与体检/历史
                        // 挤扁彼此），也不改分栏比例。可收可调的那套交互与
                        // 三个几何状态都在 MarkdownOutlinePanel 里（内存态，
                        // 不落盘）。
                        // 纯源码态没有渲染窗，大纲随之退场（锚点跳转本来就
                        // 作用于渲染页）。面板自身可见性翻转是安全的（容器
                        // 级，见左栏三段的教训）。
                        MarkdownOutlinePanel {
                            id: outlineFloat
                            objectName: "outlineFloat"

                            entries: markdown.outline
                            visible: markdown.showOutline && docArea.showsView

                            onAnchorActivated: function(anchor, blockIndex) {
                                markdown.scrollToAnchor(anchor, blockIndex)
                            }
                        }
                    }

                    // --- Ctrl+滚轮缩放编辑区字号 --------------------------------
                    // 透明覆盖件，几何盖住编辑槽位（草稿区与文件源码区共用，
                    // 两者互斥、同位置）。只吃带 Ctrl 的滚轮：普通滚轮
                    // accepted=false 落回下面的编辑器滚动；不接受任何鼠标
                    // 按钮，点击/右键/拖放全部照旧穿到编辑器。
                    MouseArea {
                        x: 0
                        y: 0
                        width: docArea.splitX
                        height: parent.height
                        visible: promptScroll.visible || editorScroll.visible
                        acceptedButtons: Qt.NoButton
                        onWheel: function(wheel) {
                            wheel.accepted =
                                    (wheel.modifiers & Qt.ControlModifier) !== 0
                            if (wheel.accepted)
                                workbenchWindow.zoomEditorFont(
                                            Math.round(wheel.angleDelta.y / 120))
                        }
                    }
                }

                // --- 状态行 -------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusRow.implicitHeight + theme.spacingXs * 2
                    color: theme.chromeBg

                    RowLayout {
                        id: statusRow
                        anchors.fill: parent
                        anchors.leftMargin: theme.spacingM
                        anchors.rightMargin: theme.spacingM
                        anchors.topMargin: theme.spacingXs
                        anchors.bottomMargin: theme.spacingXs
                        spacing: theme.spacingM

                        // 提示词模式：字符与行数（草稿的体量感）。
                        Label {
                            visible: workbenchWindow.preset === "prompt"
                            text: qsTr("%1 chars").arg(tools.draft.length)
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                        }
                        Label {
                            visible: workbenchWindow.preset === "prompt"
                            text: qsTr("%1 lines").arg(
                                      tools.draft.length === 0
                                      ? 0 : tools.draft.split("\n").length)
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                        }
                        // 查找命中统计（「N/M」）：分子随当前选区走，分母
                        // 是全文命中数。只在查找进行中露面。
                        Label {
                            visible: workbenchWindow.findNeedle.length > 0
                                     && workbenchWindow.findMatchCount > 0
                            text: "%1/%2".arg(workbenchWindow.findMatchIndex)
                                          .arg(workbenchWindow.findMatchCount)
                            color: theme.textSecondary
                            font.pixelSize: theme.fontSizeCaption
                        }

                        // 光标位置（Ln/Col）：提示词模式跟草稿编辑器，文档
                        // 模式跟源码区（源码面不在场时隐藏）。cursorPosition
                        // 是 Q_PROPERTY，光标移动时绑定自动重算。
                        Label {
                            visible: workbenchWindow.preset === "prompt"
                                     || (docArea.showsEditor
                                         && !markdown.draftActive)
                            text: {
                                const editor = workbenchWindow.preset === "prompt"
                                        ? promptEditor
                                        : (markdown.draftActive
                                           ? null : editorArea)
                                if (!editor)
                                    return ""
                                const pos = editor.cursorPosition
                                const before = editor.text.substring(0, pos)
                                const line = before.split("\n").length
                                const col = pos - before.lastIndexOf("\n")
                                return qsTr("Ln %1, Col %2").arg(line).arg(col)
                            }
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                        }

                        // C4 预算条：估算 token 与分级色（只影响文字颜色
                        //——状态行里没有地方画进度条，颜色 + 文案即分级）。
                        Label {
                            objectName: "draftBudgetLabel"
                            visible: workbenchWindow.preset === "prompt"
                                      && workbenchWindow.draftTokens >= 0
                            // 「估算」必须出现在文案里：公式不承诺与任何
                            // 分词器一致（规划 06 §四.1 的硬要求）。
                            text: qsTr("~%1 tokens (estimated)")
                                      .arg(workbenchWindow.draftTokens)
                            color: workbenchWindow.draftBudgetLevel === 2
                                   ? theme.danger
                                   : (workbenchWindow.draftBudgetLevel === 1
                                      ? theme.warning : theme.textMuted)
                            font.pixelSize: theme.fontSizeCaption
                            ToolTip.delay: 300
                            ToolTip.timeout: 10000
                            ToolTip.visible: budgetHover.hovered
                            ToolTip.text: qsTr(
                                "A rough estimate of the context the agent "
                                + "will read, against a %1-token window.")
                                      .arg(tools.contextWindowTokens)

                            HoverHandler {
                                id: budgetHover
                            }
                        }

                        // 文档模式：路径 + 脏/只读 + 体量 + 渲染栈版本。
                        Label {
                            id: statusPath
                            visible: workbenchWindow.preset === "document"
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

                        // 状态不只靠颜色：脏与只读都写成字。
                        Label {
                            visible: workbenchWindow.preset === "document"
                                     && markdown.dirty
                            text: qsTr("Unsaved changes")
                            color: theme.textSecondary
                            font.pixelSize: theme.fontSizeCaption
                        }

                        Label {
                            visible: workbenchWindow.preset === "document"
                                     && markdown.readOnly
                                     && markdown.readOnlyReason.length > 0
                            text: markdown.readOnlyReason
                            color: theme.warning
                            font.pixelSize: theme.fontSizeCaption
                            elide: Text.ElideRight
                            Layout.maximumWidth: 380
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        // E6 文档版统计：同一估算函数的另一处显示（与草稿
                        // 预算条同源）。文本经门面的估算入口喂入——控制
                        // 器不整篇暴露 text，byteSize/KB 已有，token 估算
                        // 就近用 fileText 桥（编辑器持同一文档）。
                        Label {
                            visible: workbenchWindow.preset === "document"
                                     && markdown.byteSize > 0
                                     && workbenchWindow.fileTabTokens >= 0
                            text: qsTr("~%1 tokens (estimated)")
                                      .arg(workbenchWindow.fileTabTokens)
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                        }

                        Label {
                            visible: workbenchWindow.preset === "document"
                                     && markdown.byteSize > 0
                            text: qsTr("%1 KB").arg(
                                      Math.round(markdown.byteSize / 1024))
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                        }
                        Label {
                            id: statusVersion
                            visible: workbenchWindow.preset === "document"
                                     && markdown.rendererVersion.length > 0
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

                        // --- 视图切换的就近入口（状态行最右端的工具栏） -------
                        // 与命令条「视图」页同一批动作、同一个真相源（markdown
                        // 单例的 mode / showOutline），两处经绑定天然同步。
                        // 排布：源码、分栏、预览 ── 分隔线 ── 大纲。单选语义
                        // 与命令条一致：variant 由 mode 绑定、点击只写回
                        // mode，按钮没有自己的可翻转状态，永不会「全不选」。
                        // 无预览能力的构建整组退场（与命令条同一条能力门控）。
                        RowLayout {
                            id: statusViewSwitch
                            objectName: "statusViewSwitch"
                            visible: markdown.available
                            spacing: theme.spacingXs

                            AButton {
                                compact: true
                                text: qsTr("Source")
                                variant: markdown.mode === "source"
                                         ? "primary" : "ghost"
                                enabled: markdown.splitAvailable
                                onClicked: markdown.setMode("source")
                            }
                            AButton {
                                compact: true
                                text: qsTr("Split")
                                variant: markdown.mode === "split"
                                         ? "primary" : "ghost"
                                enabled: markdown.splitAvailable
                                onClicked: markdown.setMode("split")
                            }
                            AButton {
                                compact: true
                                text: qsTr("Preview")
                                variant: markdown.mode === "preview"
                                         ? "primary" : "ghost"
                                onClicked: markdown.setMode("preview")
                            }
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: 1
                                implicitHeight: 16
                                color: theme.separator
                            }
                            AButton {
                                compact: true
                                text: qsTr("Outline")
                                variant: markdown.showOutline
                                         ? "primary" : "ghost"
                                onClicked: markdown.setShowOutline(
                                               !markdown.showOutline)
                            }
                        }
                    }
                }
            }
        }
    }

    // --- 体检悬浮窗（非模态：不挡其它操作，可停留可关闭） ------------------
    // 从右侧栏迁来（2026-10）：体检结果占一整条右栏会把编辑区挤窄，迁成
    // 悬浮窗后它想停多久停多久、不用时关掉即全屏写作。空态分两种：没跑
    // 过给引导（开窗动作会顺路跑一次）；跑过且为空 = 真的没问题。
    PromptChecksPopup {
        id: checksPopup
        objectName: "checksPopup"

        results: workbenchWindow.draftCheckResults
        visible: workbenchWindow.checksVisible

        onCloseRequested: workbenchWindow.checksVisible = false
        onRerunRequested: workbenchWindow.runDraftChecks()
        onRangeRequested: function(start, length) {
            workbenchWindow.selectDraftRange(start, length)
        }
    }

    // --- 历史悬浮窗（非模态，与体检窗错位） --------------------------------
    // 草稿的快照清单与回滚入口（从右侧栏迁来）。点一版 = 回滚（先确认
    // ——回滚是破坏性替换；当前版会被自动存成 rollback 快照）。
    DraftHistoryPopup {
        id: historyPopup
        objectName: "historyPopup"

        sibling: checksPopup
        visible: workbenchWindow.historyVisible

        onCloseRequested: workbenchWindow.historyVisible = false
        onSaveVersionRequested: workbenchWindow.saveDraftVersion()
        onRollbackRequested: function(createdAt) {
            workbenchWindow.rollbackDraftVersion(createdAt)
        }
    }

    // --- tab 右键菜单（草稿 tab：重命名/删除；两种 tab 都可关闭） ---------
    // 草稿专属条目对文件 tab **禁用而不隐藏**（与文件树右键菜单同一条
    // 纪律，2026-10 用户报告：隐藏会在菜单里留出空白槽位，禁用让「动作
    // 存在、此处不可用」可见）。关闭对草稿与文件 tab 都生效。
    AMenu {
        id: tabContextMenu
        objectName: "tabContextMenu"

        // 右键落在草稿 tab 上时是草稿 id；空串 = 文件 tab。docId 两种
        // tab 都有（关闭动作按它走各自的会话关闭）。
        property string draftTab: ""
        property string tabDocId: ""

        AMenuItem {
            enabled: tabContextMenu.draftTab.length > 0
            icon.source: "qrc:/icons/bold.svg"
            text: qsTr("Rename")
            onTriggered: workbenchWindow.renameCurrentDraft()
        }
        AMenuItem {
            enabled: tabContextMenu.draftTab.length > 0
            icon.source: "qrc:/icons/close.svg"
            text: qsTr("Delete draft")
            onTriggered: workbenchWindow.deleteCurrentDraft()
        }
        AMenuSeparator {}
        AMenuItem {
            icon.source: "qrc:/icons/close.svg"
            text: qsTr("Close tab")
            onTriggered: {
                if (tabContextMenu.draftTab.length > 0)
                    markdown.closeDraftSession(tabContextMenu.draftTab)
                else if (tabContextMenu.tabDocId.length > 0)
                    markdown.closeSession(tabContextMenu.tabDocId)
            }
        }
    }

    // --- 与控制器之间的全部信号 --------------------------------
    Connections {
        target: markdown

        // 右键预览 / 双击 .md：控制器把文件会话装载好并发信号。页面
        // 已常驻在场，这里只切预设（文件会话此刻已激活）——把用户带到
        // 页面上是导航侧的事（WorkbenchContext::previewMarkdown 会一并
        // 切到本页）。
        function onWindowRequested() {
            workbenchWindow.applyPreset("document")
        }

        // 队列里进了新命令（编辑、换主题、搜索…）
        function onCommandQueued() {
            workbenchWindow.pump()
        }

        // 活跃会话换人：记住「上次的文件会话」（预设来回切时回到看的那份）。
        function onActiveSessionChanged() {
            if (!markdown.draftActive)
                workbenchWindow.lastFileDocId = markdown.activeDocId
            else if (markdown.activeDraftId !== tools.activeDraftId
                     && markdown.activeDraftId.length > 0) {
                // 会话侧先落到了另一条草稿（会话恢复、关 tab 顺位）：编辑
                // 器的编辑对象跟过去——门面侧 switch 落盘旧草稿并换正文。
                tools.switchDraft(markdown.activeDraftId)
            }
            // 激活位是会话文件的一部分，切换即排队落盘。
            workbenchWindow.scheduleSessionSave()
        }

        // tab 结构/脏标记/标题变化：统一走防抖落盘（脏标记的逐键翻转被
        // 800 ms 收敛成编辑间歇的一次写）。
        function onTabsChanged() {
            workbenchWindow.scheduleSessionSave()
        }

        // 整篇换了（换 tab、重载、接受外部改动、草稿改写）：文件源码区的
        // 文本跟着换，但要绕开 debounce，否则这次写入会被算成用户的编辑、
        // 当场又变脏。草稿激活时跳过——草稿正文只从草稿编辑器流向会话，
        // 不倒灌。
        function onDocumentChanged() {
            if (markdown.draftActive)
                return
            const current = markdown.editorText()
            if (editorArea.text !== current) {
                applyingExternal = true
                editorArea.text = current
                applyingExternal = false
            }
            // 换了一篇文档 = 页面重挂 DOM：渲染失败的「只报一次」闸门
            // 不再成立。
            renderNoticeShown = false
        }

        function onSaved() {
            workbench.notify("success", qsTr("Saved"), markdown.filePath)
        }

        function onChangedOnDiskChanged() {
            if (!markdown.changedOnDisk)
                return
            if (workbenchWindow.preset !== "document")
                return
            if (!markdown.dirty) {
                // 没有可丢的东西就直接跟上磁盘那份；留着提示只会让用户
                // 去处理一个不需要决定的决定。
                markdown.reloadFromDisk()
                return
            }
            diskChangedDialog.open()
        }

        function onRenderFailed(message) {
            console.warn("markdown: page reported", message)
            if (renderNoticeShown)
                return
            renderNoticeShown = true
            workbench.notify("error", qsTr("Something did not render"),
                             qsTr("A part of the document failed to render. "
                                  + "See the log for details."))
        }

        function onMarkupWritten(path, purpose) {
            if (purpose === "browser") {
                workbench.openLocalFileExternal(path)
                return
            }
            workbench.notify("success", qsTr("Exported"), path)
            ui.revealFile(path)
        }

        function onMarkupWriteFailed(purpose, message) {
            workbench.notify("error", qsTr("Could not write the file"), message)
        }
    }

    // --- 与 tools 门面之间的信号（M4：草稿集驱动编辑器与会话） --------------
    Connections {
        target: tools

        // 当前草稿换人（新建/切换/删除顺位）：编辑器换正文（undo 栈随
        // text 重置清零——不同草稿不该共享撤销历史），渲染会话跟过去，
        // 体检与历史面板跟着当前草稿重算。
        function onActiveDraftIdChanged() {
            // promptEditor 是文件级 id（QML 组件内全局解析），直接用；
            // 写成 workbenchWindow.promptEditor 是在窗口上找不存在的
            // 属性，静默 undefined——守卫一挡编辑器就永远不换正文。
            if (promptEditor) {
                promptEditor.text = tools.draft
                draftSyncTimer.stop()
            }
            workbenchWindow.activateActiveDraftSession()
            historyPopup.rebuild()
            workbenchWindow.draftCheckResults = null
        }

        // 清单变化（新建/删除/重命名/复制）：重命名的标题要同步进控制
        // 器（tab 与渲染标题），历史面板跟着当前草稿重算。
        function onDraftsChanged() {
            if (markdown.draftActive
                    && tools.activeDraftId.length > 0)
                markdown.setDraftTitle(tools.activeDraftId,
                                       workbenchWindow.draftTitleById(
                                           tools.activeDraftId))
            historyPopup.rebuild()
        }
    }

    // 外部改动的回页复查：QFileSystemWatcher 在 Windows 上会静默丢掉
    // 「删除后重建」的文件，光靠监视器有漏报，回到本页时再问一次。
    onPageCurrentChanged: {
        if (pageCurrent && preset === "document")
            markdown.refreshDiskState()
    }

    // --- 更多动作菜单（文档模式） ------------------------------------------
    // Save 收在这里：命令条不再放文档模式专属动作（Ctrl+F 之外还有 Ctrl+S
    // 直达），菜单是它唯一的指针入口。
    AMenu {
        id: actionsMenu

        AMenuItem {
            icon.source: "qrc:/icons/download.svg"
            text: qsTr("Save")
            enabled: markdown.canEdit && markdown.dirty
            onTriggered: workbenchWindow.saveDocument()
        }
        AMenuItem {
            icon.source: "qrc:/icons/copy.svg"
            text: qsTr("Copy selection")
            onTriggered: workbenchWindow.copySelection()
        }
        AMenuItem {
            icon.source: "qrc:/icons/code.svg"
            text: qsTr("Copy Markdown source")
            onTriggered: workbenchWindow.copySource()
        }
        AMenuSeparator {}
        AMenuItem {
            icon.source: "qrc:/icons/external-link.svg"
            text: qsTr("Open in browser")
            onTriggered: workbenchWindow.requestLanding("browser")
        }
        AMenuItem {
            icon.source: "qrc:/icons/download.svg"
            text: qsTr("Export HTML next to the document")
            // 导出件里的图片是相对引用，落在文档旁边才显示得出来。
            onTriggered: {
                markdown.setExportDirectory("")
                workbenchWindow.requestLanding("export")
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
                workbenchWindow.requestLanding("export")
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

    // --- 补渲染轮询（导出 / 外部打开的前置） --------------------------------
    // 判据一律取「还在忙吗」：AWB 没定义、回读形状不对、pending 非零，全都
    // 是「还没闲下来」。把「读不到状态」当成闲，会在页面没加载完时导出
    // 一份空文件。
    Timer {
        id: idlePoll
        interval: 250
        repeat: true
        onTriggered: {
            // 预览面不在（能力缺失或尚未装载）就没有「等它闲下来」可言：
            // 直接停表，别让下一次 tick 再撞一次空引用。
            if (!previewPaneLoader.item) {
                idlePoll.stop()
                return
            }
            previewPaneLoader.item.evalJs(
                        "(typeof AWB === 'undefined' ? null : AWB.idle())",
                        function (state) {
                            const busy = !state
                                    || state.pendingBlocks === undefined
                                    || Number(state.pendingBlocks)
                                       + Number(state.pendingDiagrams) > 0
                            // 40 次 × 250 ms = 10 s 上限。
                            if (busy && workbenchWindow.spentIdlePoll() < 40)
                                return
                            idlePoll.stop()
                            markdown.requestMarkup(pendingLanding)
                        })
        }
    }

    // --- 两个问用户的地方 ---------------------------------------------------
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

    // --- 模板插入的变量表单（M2） -------------------------------------------
    // 直接声明而非经 Loader：Dialog/Popup 从 Loader 的 sourceComponent
    // 实例化时，弹层的父子重定会在「实例化中」触发一次已释放子项的
    // 虚调用（实测崩溃，feeefeee 模式）；直接声明没有这条路径，而弹层
    // 本来就不参与布局——没 open() 时零渲染开销，「惰性」在这里没有
    // 实际收益。
    ADialog {
        id: variableForm
        objectName: "variableForm"

        // 窗口（动作层回调的持有者）。
        property var win: null

        width: 460
        // 标题绑 pendingTemplate（装载前已设，创建时即有值）。
        titleText: workbenchWindow.pendingTemplate
                   ? workbenchWindow.pendingTemplate.title : ""
        closePolicy: Popup.NoAutoClose

        // 变量行：名字 + 输入框（有默认值预填；留空 = 跳过）。
        // 逐行 Repeater 生成——数量级是个位数到十几个。模型绑
        // pendingTemplate（装载前已设）：delegate 在实例化中创建。
        ColumnLayout {
            width: variableForm.availableWidth
            spacing: theme.spacingS

            Label {
                Layout.fillWidth: true
                text: qsTr("Empty fields stay as {{placeholders}} in the text.")
                color: theme.textMuted
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.Wrap
            }

            Repeater {
                id: variableRepeater

                model: workbenchWindow.pendingTemplate
                       ? tools.templateVariables(
                             workbenchWindow.pendingTemplate.id)
                       : []

                delegate: RowLayout {
                    id: variableRow
                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    spacing: theme.spacingS

                    Label {
                        Layout.preferredWidth: 110
                        text: variableRow.modelData.name
                        color: theme.textSecondary
                        font.pixelSize: theme.fontSizeCaption
                        elide: Text.ElideMiddle
                    }

                    ATextField {
                        id: valueField

                        Layout.fillWidth: true
                        placeholderText:
                            variableRow.modelData.defaultValue
                        text: variableRow.modelData.defaultValue
                        // Enter = 确认（键盘流式：Tab 自然跳下一个，
                        // Esc 走 ADialog 的 reject——closePolicy 关掉
                        // 了 PressOutside，Esc 只由 Dialog 处理）。
                        onAccepted: variableForm.confirm()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Item {
                    Layout.fillWidth: true
                }
                AButton {
                    text: qsTr("Cancel")
                    onClicked: variableForm.reject()
                }
                AButton {
                    variant: "primary"
                    text: qsTr("Insert")
                    onClicked: variableForm.confirm()
                }
            }
        }

        onRejected: {
            cancelTemplateInsert()
        }

        // 确认：收表单值 → 窗口的 completeTemplateInsert。
        function confirm() {
            const values = {}
            for (let i = 0; i < variableRepeater.count; ++i) {
                const row = variableRepeater.itemAt(i)
                if (!row)
                    continue
                // 行里唯一的 ATextField。
                const fields = row.data
                for (let c = 0; c < fields.length; ++c) {
                    const child = fields[c]
                    if (child instanceof ATextField) {
                        values[row.modelData.name] = child.text
                        break
                    }
                }
            }
            close()
            completeTemplateInsert(values)
        }
    }

    // --- 存为模板对话框（M2，Loader 惰性装载） -------------------------------
    // --- 存为模板对话框（M2） -----------------------------------------------
    // 直接声明（同 variableForm 的理由：Dialog 从 Loader 实例化会在
    // 弹层重定父子时崩溃；弹层没 open() 零开销，不需要惰性）。
    ADialog {
        id: saveTemplateDialog
        objectName: "saveTemplateDialog"

        width: 460
        titleText: qsTr("Save as template")
        closePolicy: Popup.NoAutoClose

        ColumnLayout {
            width: saveTemplateDialog.availableWidth
            spacing: theme.spacingS

            // 标题（预填草稿首行；门面没有暴露「首行去记号」的推导，
            // 在这里做一份轻量的）。
            Label {
                    text: qsTr("Title")
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeCaption
                }
                ATextField {
                    id: templateTitleField
                    objectName: "templateTitleField"

                    Layout.fillWidth: true
                }

                Label {
                    text: qsTr("Category")
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeCaption
                }
                ATextField {
                    id: templateCategoryField
                    objectName: "templateCategoryField"

                    Layout.fillWidth: true
                    text: qsTr("My templates")
                }

                Label {
                    text: qsTr("Description")
                    color: theme.textSecondary
                    font.pixelSize: theme.fontSizeCaption
                }
                ATextField {
                    id: templateDescriptionField
                    objectName: "templateDescriptionField"

                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingS

                    Item {
                        Layout.fillWidth: true
                    }
                    AButton {
                        text: qsTr("Cancel")
                        onClicked: saveTemplateDialog.reject()
                    }
                    AButton {
                        variant: "primary"
                        text: qsTr("Save")
                        onClicked: saveTemplateDialog.accept()
                    }
                }
            }

            // 首行去 markdown 记号（与门面的 firstLineAsTitle 同一条规则；
            // 门面没暴露它，这份推导留在 UI 侧即可——两边口径一致）。
            function draftTitleGuess() {
                const first = workbenchWindow.draftFirstLine()
                let cleaned = ""
                for (let i = 0; i < first.length; ++i) {
                    const ch = first[i]
                    if (ch === "#" || ch === "-" || ch === "*"
                            || ch === ">" || ch === "`")
                        continue
                    cleaned += ch
                }
                cleaned = cleaned.trim()
                return cleaned.length > 0 ? cleaned
                        : qsTr("New template")
            }

            onAccepted: {
                const result = tools.saveAsTemplate(
                            templateTitleField.text,
                            workbenchWindow.draftFullText(),
                            templateCategoryField.text,
                            templateDescriptionField.text)
                if (result.ok) {
                    workbench.notify(
                                "success",
                                qsTr("Template saved"),
                                qsTr("It is in your template library and in the template folder."))
                }
                else {
                    workbench.notify("error", qsTr("Could not save the template"),
                                     result.error)
                }
            }

            onRejected: promptEditor.forceActiveFocus()
        }


    // 重命名草稿：改名不改 id（会话恢复按 id 不受影响）。
    ADialog {
        id: renameDraftDialog
        width: 420
        titleText: qsTr("Rename the draft")

        onOpened: renameDraftField.forceActiveFocus()

        ColumnLayout {
            width: renameDraftDialog.availableWidth
            spacing: theme.spacingM

            ATextField {
                id: renameDraftField
                objectName: "renameDraftField"
                Layout.fillWidth: true
                placeholderText: qsTr("Draft title")
                onAccepted: workbenchWindow.doRenameDraft()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                AButton {
                    Layout.fillWidth: true
                    variant: "primary"
                    text: qsTr("Rename")
                    onClicked: workbenchWindow.doRenameDraft()
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: renameDraftDialog.close()
                }
            }
        }
    }

    // 删除草稿的确认：非空草稿先问（连带快照与恢复副本一起删，不可撤销）。
    ADialog {
        id: deleteDraftDialog
        width: 420
        titleText: qsTr("Delete the draft")

        ColumnLayout {
            width: deleteDraftDialog.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Delete '%1' and its version history? This cannot be undone.")
                            .arg(workbenchWindow.draftTitleById(
                                     tools.activeDraftId))
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
                    text: qsTr("Delete")
                    onClicked: {
                        deleteDraftDialog.close()
                        workbenchWindow.doDeleteDraft()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: deleteDraftDialog.close()
                }
            }
        }
    }

    // 跳转到行（Ctrl+G）：小对话框输行号（1 起）。两种预设都可跳：提示
    // 词模式落草稿，文档模式落源码区；导航只动光标，只读文档同样可用。
    ADialog {
        id: gotoLineDialog
        objectName: "gotoLineDialog"
        width: 360
        titleText: qsTr("Go to line")

        onOpened: {
            gotoLineField.text = ""
            gotoLineField.forceActiveFocus()
        }

        ColumnLayout {
            width: gotoLineDialog.availableWidth
            spacing: theme.spacingM

            ATextField {
                id: gotoLineField
                objectName: "gotoLineField"

                Layout.fillWidth: true
                placeholderText: qsTr("Line number")
                // 只收正整数（校验器挡掉其余输入，确认时不必再猜解析）。
                validator: IntValidator { bottom: 1; top: 9999999 }
                onAccepted: workbenchWindow.doGotoLine()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.spacingS

                Item {
                    Layout.fillWidth: true
                }
                AButton {
                    text: qsTr("Cancel")
                    onClicked: gotoLineDialog.close()
                }
                AButton {
                    variant: "primary"
                    text: qsTr("Go")
                    onClicked: workbenchWindow.doGotoLine()
                }
            }
        }
    }

    // 回滚确认：破坏性替换。门面在回滚前把当前版存成 rollback 快照，
    // 所以两头都有退路——文案如实说。
    ADialog {
        id: rollbackConfirm
        objectName: "rollbackConfirm"
        width: 420
        titleText: qsTr("Roll back")

        ColumnLayout {
            width: rollbackConfirm.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Replace the draft with this version? The current text is saved to the history first.")
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
                    text: qsTr("Roll back")
                    onClicked: workbenchWindow.doRollback()
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: rollbackConfirm.close()
                }
            }
        }
    }

    // 崩溃恢复（D6）：启动时检测到未恢复的副本问一次。恢复 = 副本变成
    // 一条新草稿（标题标注来源）；丢弃 = 删副本不再问。
    ADialog {
        id: recoveryDialog
        width: 440
        closePolicy: Popup.NoAutoClose
        titleText: qsTr("Recovered draft")

        ColumnLayout {
            width: recoveryDialog.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("The app closed unexpectedly. Restore the draft text from that session as a new draft?")
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
                    text: qsTr("Restore")
                    onClicked: {
                        recoveryDialog.close()
                        const id = tools.recoverToNewDraft()
                        if (id.length > 0) {
                            workbenchWindow.applyPreset("prompt")
                            workbench.notify(
                                        "success",
                                        qsTr("Draft restored"),
                                        qsTr("It is open as a new draft tab."))
                        }
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Discard")
                    onClicked: {
                        recoveryDialog.close()
                        tools.discardRecovery()
                    }
                }
            }
        }
    }
}
