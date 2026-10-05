import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgentWorkbench
import AgentWorkbench.App

// Markdown 工作台窗口：提示词撰写与文档预览合体后的那扇独立窗。
//
// 两种「预设」是同一套代码的两份界面配置，不是两个应用：
//   提示词模式（prompt）——左侧工作区文件树全开、草稿编辑区为主体，
//     Ribbon 展开；这是从提示词页（原 ToolsPage）整体迁进来的形态。
//   文档模式（document）——侧栏收起、Ribbon 折叠，中间区渲染磁盘上
//     那份 .md（预览窗 MarkdownPreviewWindow 演化来的形态）。
// 用户视角里没有「模式」概念，只有「工具多一点少一点」；切预设的入口
// 是视图区的「完整工具」开关。
//
// 这里只有「窗口」的知识：布局、快捷键、怎么问用户。文档状态与命令队列
// 在 `markdown` 单例（MarkdownPreviewController）里，草稿与工作区在
// `tools` 单例（ToolsFacade）里，渲染面在 MarkdownPreviewPane.qml（按
// 资源 URL 惰性装载——本窗口要在没有 WebEngine 的构建里照常加载）。
//
// 与 Web 页的表面有意不共用组件，理由见 MarkdownPreviewPane.qml 的头注释。
// 与主窗同型用 ApplicationWindow（根部那行 font.family 需要 ApplicationWindow
// 才有），字体挂在窗口级才覆盖菜单与对话框。
ApplicationWindow {
    id: workbenchWindow

    // --- 开窗意图与预设 -----------------------------------------------------
    // 主窗的 Loader 在 onLoaded 里写这个属性：previewMarkdown() 的流程给
    // "document"，openMarkdownWorkbench() 给 "prompt"。onCompleted 与
    // onInitialPresetChanged 都走 applyPreset（幂等），装载完成前的默认
    // 值与装载后的写入发生在同一轮事件里，窗口上屏前就定形。
    property string initialPreset: "prompt"
    // 当前预设：prompt | document。文档模式要求控制器里有一份能渲染的
    // 文档，拿不到时 applyPreset 会退回提示词模式。
    property string preset: "prompt"

    // 两套预设的界面状态各自独立、互不污染（窗口存活期内）：
    // 提示词模式两侧全开；文档模式两侧收起（窄边可展开）。
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

    // --- 窗口自己的状态（活不过一次开窗） ---------------------------------
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

    // 命令泵的闸门：预览面装载完成且页面就绪。
    readonly property bool previewReady: previewPaneLoader.item
                                         && previewPaneLoader.item.pageReady

    // 当前激活的编辑器：提示词模式是草稿区；文档模式是源码区（只读文档
    // 没有可编辑目标，格式动作自然禁用）。
    readonly property var activeEditor: preset === "prompt"
                                        ? promptEditor
                                        : (markdown.canEdit ? editorArea : null)

    width: 1180
    height: 820
    minimumWidth: 720
    minimumHeight: 460
    visible: true
    color: theme.windowBg
    // 文档名在前：任务栏与窗口切换器里认的是文件名。分隔符单列一处，
    // 别和 qsTr 挤在同一物理行——i18n 门禁按行检查 ASCII。
    readonly property string titleDash: " — "
    title: (preset === "document" && markdown.fileTitle.length > 0
            ? markdown.fileTitle + workbenchWindow.titleDash : "")
           + qsTr("Markdown workbench")
    // 全局字体与主窗一致（Controls2 的字体继承链的根在窗口上）
    font.family: theme.family.length > 0 ? theme.family : undefined

    // --- 模板与收藏（M2 动作层） -------------------------------------------
    // 「动作只有一处出口」：Ribbon、降级工具栏、左栏条目都只转发到这里。
    // 插入目标永远是草稿编辑器（文件 tab 是别人的文档，不往里插模板）。

    // 正在走变量表单流程的模板（{id, title}）；null = 无流程在飞。
    property var pendingTemplate: null

    // 左栏段的折叠态（窗口内存：会话文件存预设级开合，段级是浏览态）。
    property bool templateSectionExpanded: true
    property bool favoriteSectionExpanded: false

    // B9 体检结果（右栏段消费；null = 还没跑过——段显示引导文案）。
    property var draftCheckResults: null

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

    // 「选择器」入口（Ribbon 的 Insert Template 转发到这里）：展开左栏
    // 的模板段并聚焦检索框——左栏模板库就是选择器，不再造第二个列表。
    function openTemplatePicker() {
        templateSectionExpanded = true
        if (!leftOpen) {
            if (preset === "prompt")
                promptLeftOpen = true
            else
                documentLeftOpen = true
        }
        templateSearch.forceActiveFocus()
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

    // --- 预设应用（幂等：onCompleted 与意图写入各走一次也安全） ------------
    // 预设与活跃会话耦合：提示词预设激活草稿会话（预览面渲染草稿），文档
    // 预设激活上次的文件会话（没有文件 tab 就退回提示词）。lastFileDocId
    // 记住「上一个活跃的文件会话」：prompt ⇄ document 来回切时回到看的那份，
    // 而不是总跳到最后打开的。
    property string lastFileDocId: ""

    readonly property var dirtyFileTabs: {
        const dirty = []
        for (let i = 0; i < markdown.tabs.length; ++i) {
            if (markdown.tabs[i].dirty)
                dirty.push(markdown.tabs[i])
        }
        return dirty
    }

    function applyPreset(requested) {
        const documentUsable = markdown.available
                               && markdown.tabs.length > 0
        if (requested === "document" && documentUsable) {
            preset = "document"
            if (markdown.draftActive || markdown.activeDocId.length === 0) {
                const target = lastFileDocId.length > 0
                        && markdown.tabs.length > 0
                        ? lastFileDocId
                        : markdown.tabs[markdown.tabs.length - 1].docId
                markdown.activateSession(target)
            }
        }
        else {
            preset = "prompt"
            // 草稿会话承载提示词形态的渲染（虚拟文档）；已激活时是幂等的。
            markdown.activateSession(markdown.openDraftSession())
        }
    }

    onInitialPresetChanged: applyPreset(initialPreset)

    // --- 动作层（Ribbon 与降级工具栏转发的就是这一批） ---------------------
    function copyPrompt() {
        if (tools.draft.length === 0) {
            workbench.notify("info", qsTr("Nothing to copy"),
                             qsTr("The editor is empty."))
            return
        }
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
    }

    // 围栏代码块 + 语言（Ribbon 的 Fence 菜单转发到这里）。
    function applyFence(language) {
        const editor = activeEditor
        if (!editor)
            return
        editor.forceActiveFocus()
        MarkdownEdit.applyFence(editor, language)
    }

    // --- B2 草稿查找条 -----------------------------------------------------

    function openDraftFind() {
        draftFindBar.visible = true
        draftFindField.forceActiveFocus()
        draftFindField.selectAll()
    }

    function closeDraftFind() {
        draftFindBar.visible = false
        if (promptEditor)
            promptEditor.forceActiveFocus()
    }

    // 一次「查找下一个」：命中即选中（TextArea 的 select 经光标位置）。
    // 回卷给一条 toast（不打断——「已从头开始」是提示不是警告）。
    function draftFindNext() {
        const needle = draftFindField.text
        if (needle.length === 0 || !promptEditor)
            return
        const hit = MarkdownEdit.find(
                    promptEditor, needle, promptEditor.selectionEnd,
                    tools.findCaseSensitive, tools.findWholeWords)
        if (hit.start < 0) {
            draftFindTotal = 0
            return
        }
        if (hit.wrapped)
            workbench.notify("info", qsTr("Find"),
                             qsTr("Searched from the start."))
        promptEditor.select(hit.start, hit.start + hit.length)
        draftFindTotal = 1
    }

    // 替换当前命中：MarkdownEdit.replaceAndFindNext 在文档侧完成替换
    // 与定位，然后把结果选区写回编辑器。
    function draftReplaceCurrent() {
        const needle = draftFindField.text
        if (needle.length === 0 || !promptEditor
                || promptEditor.selectedText.length === 0)
            return
        const next = MarkdownEdit.replaceAndFindNext(
                    promptEditor, draftReplaceField.text, needle,
                    tools.findCaseSensitive, tools.findWholeWords)
        if (next.start >= 0)
            promptEditor.select(next.start, next.start + next.length)
    }

    function draftReplaceAll() {
        const needle = draftFindField.text
        if (needle.length === 0 || !promptEditor)
            return
        // 全部替换给一次确认并显示替换条数（规划 03 §三）。
        replaceAllConfirm.open()
    }

    // 确认后的全部替换：文档侧一个编辑块完成（一次撤销全部回退），
    // 编辑器 text 经 Document 牵引自动跟随（remove/insert 与全文替换
    // 同走 QTextDocument，绑不用手动拨）。
    function doReplaceAll() {
        const needle = draftFindField.text
        const count = MarkdownEdit.replaceAll(
                    promptEditor, needle, draftReplaceField.text,
                    tools.findCaseSensitive, tools.findWholeWords)
        workbench.notify("success", qsTr("Replaced"),
                         qsTr("%1 replaced.").arg(count))
    }

    // --- B3 清空草稿 -------------------------------------------------------

    function confirmClearDraft() {
        clearDraftConfirm.open()
    }

    function doClearDraft() {
        if (!promptEditor || tools.draft.length === 0)
            return
        // 双保险：确认弹窗（本函数的调用者）+ 编辑器 undo 栈一步回退
        //（clear() 是一次可撤销的编辑动作）。空串写回走既有防抖通道。
        promptEditor.clear()
        workbench.notify("info", qsTr("Cleared"),
                         qsTr("The draft is empty. Press Ctrl+Z to undo."))
    }

    property int draftFindTotal: 0

    // --- B9 体检与 C4 预算条 -----------------------------------------------

    // 跑一次体检并把结果存进窗口状态（右栏段消费）。文本来自草稿
    // （体检是提示词质量工具，文件 tab 上入口禁用）。
    function runDraftChecks() {
        draftCheckResults = tools.runChecks(tools.draft)
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
            editor.paste()
    }

    function saveDocument() {
        const result = markdown.saveNow()
        if (!result.ok)
            workbench.notify("error", qsTr("Could not save"), result.error)
        // 成功那条 toast 在 onSaved 里发：存盘的路径不止这一条（菜单、快捷键），
        // 而「存下了」这件事只由控制器的 saved 信号定义。
        return result.ok
    }

    function toggleFullTools() {
        if (preset === "prompt") {
            // 收起 = 切到文档模式预设；没有可看的文档时这个开关根本
            // 不会亮（enabled 绑在 documentUsable 上）。
            applyPreset("document")
        }
        else {
            applyPreset("prompt")
        }
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

    // --- 搜索 --------------------------------------------------------------
    // 空关键词也要发出去：页面在 find() 开头就把上一次的高亮拆掉，不发就
    // 留着一屏黄底。
    function runSearch(restart) {
        markdown.requestFind(searchField.text, searchForward,
                             caseSensitive, restart)
        if (searchField.text.length === 0) {
            findTotal = 0
            findActive = 0
        }
    }

    function openSearch() {
        searchOpen = true
        searchField.forceActiveFocus()
    }

    function closeSearch() {
        searchOpen = false
        searchField.text = ""
    }

    function approveClose() {
        // 只放行，报到交给 onClosing：两条关闭路径（点 × 与这里的对话框）都
        // 从那里过，状态放一次就够。
        closeApproved = true
        workbenchWindow.close()
    }

    // 关窗守卫的「全部保存」：逐个按 id 存，不换激活（中途失败就地报因，
    // 剩下的下一轮再存）。返回是否全部成功。
    function saveAllDirtySessions() {
        let allOk = true
        for (let i = 0; i < markdown.tabs.length; ++i) {
            const tab = markdown.tabs[i]
            if (!tab.dirty)
                continue
            const result = markdown.saveSession(tab.docId)
            if (!result.ok) {
                allOk = false
                workbench.notify("error", qsTr("Could not save"),
                                 tab.title + " — " + result.error)
            }
        }
        return allOk
    }

    // 会话落盘：把窗口现场（预设/开合）交给 workbench 合成（tab 组与激活位
    // 从控制器取）。结构变化立即的「立即」是防抖 800 ms——脏标记翻转也会
    // 触发 tabsChanged，逐键落盘没必要；关窗走 flushSession() 的不加防抖版。
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

    // 开窗即向控制器报到：从此它才往外排命令（窗口不在时命令一律不排）。
    // 草稿会话在这里灌进初始正文——之后每敲一键经防抖器增量喂给它。
    // 本轮应用的第一次开窗顺带恢复上次的 tab 组（E3）：之后窗口的销毁
    // 重建走控制器内存里的会话，不需要再恢复。
    Component.onCompleted: {
        markdown.notifyWindowOpened()
        markdown.setDraftText(tools.draft)
        var restored = null
        if (markdown.tabs.length === 0)
            restored = workbench.restoreMarkdownSession()
        if (restored && restored.restored) {
            promptLeftOpen = restored.promptLeftOpen
            documentLeftOpen = restored.documentLeftOpen
            documentRibbonCollapsed = restored.documentRibbonCollapsed
            applyPreset(restored.preset === "document" ? "document"
                                                       : "prompt")
        }
        else {
            applyPreset(initialPreset)
        }
        if (!markdown.canEdit && markdown.mode === "source")
            markdown.setMode("preview")
    }

    // 没有关闭事件也要把状态位放下来：主窗退出、Loader 被直接关掉都会跳过
    // onClosing。控制器里的 m_windowOpen 有幂等守卫。
    Component.onDestruction: {
        draftSyncTimer.stop()
        markdown.notifyWindowClosed()
    }

    // --- 关闭语义 ----------------------------------------------------------
    // 只读文档不能 Ctrl+S，但「只读」挡不住别的 tab 脏：守卫看的是全部文件
    // 会话，不只有激活的那个——静默丢掉一个后台 tab 的改动是这个窗口能犯的
    // 最贵的错。
    onClosing: function (close) {
        if (closeApproved || dirtyFileTabs.length === 0) {
            closeApproved = false
            // 最后一次现场落盘（不加防抖：进程可能马上就没了）。
            flushSession()
            // 关窗必须报到：QML 的 Window 关掉只是藏起来，对象还活着。不报
            // 的话 `markdown.active` 永远为真，主窗那个 Loader 的 active 不会
            // 翻转，第二次请求就只剩一次 raise 打在一扇已经隐藏的窗上。
            // 顺序要紧：先让控制器把窗口态放下，窗口随后才销毁（会话保留）。
            markdown.notifyWindowClosed()
            return
        }
        // 有未保存的文件时不静默丢弃：这扇窗的存盘键是 Ctrl+S，用户完全
        // 可能改完直接点 ×。
        close.accepted = false
        unsavedDialog.open()
    }

    // --- 快捷键 ------------------------------------------------------------
    // context 用 ApplicationShortcut + enabled 绑本窗激活：Chromium 会吃掉
    // 普通窗口作用域的按键，而限定 active 又保证不会在用户回到主窗时抢下
    // Ctrl+S / Ctrl+F。
    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        enabled: workbenchWindow.active && preset === "document"
                 && markdown.canEdit
        onActivated: workbenchWindow.saveDocument()
    }
    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.ApplicationShortcut
        // 同一个键两种去向：文档模式查预览渲染区（既有搜索条），提示词
        // 模式查草稿源码（B2 的编辑区查找条）。草稿查找条开着时 Ctrl+F
        // 把焦点送回搜索字段（连续查找的肌肉记忆）。
        enabled: workbenchWindow.active
        onActivated: {
            if (preset === "document")
                workbenchWindow.openSearch()
            else
                workbenchWindow.openDraftFind()
        }
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        // Esc 的就近对象：草稿查找条 > 文档搜索条。都没有时静默（不
        // 抢弹层/菜单的 Esc）。
        enabled: workbenchWindow.active
                 && (draftFindBar.visible || searchOpen)
        onActivated: {
            if (draftFindBar.visible)
                workbenchWindow.closeDraftFind()
            else
                workbenchWindow.closeSearch()
        }
    }
    // F5 是「重读磁盘」而不是「重载页面」：内容本来就在本地，重载视图只会
    // 把已经挂好的 DOM 丢掉重来一遍。
    Shortcut {
        sequence: "F5"
        context: Qt.ApplicationShortcut
        enabled: workbenchWindow.active && preset === "document"
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // --- 左侧：工作区文件树（可折叠为窄边） -----------------------
            // 树是「拿什么素材」的浏览器，不是动作区；折叠后留一条可点击
            // 的窄边。提示词预设默认展开，文档预设默认收起。
            Rectangle {
                id: leftPanel

                readonly property bool stripMode: !leftOpen

                Layout.fillHeight: true
                Layout.preferredWidth: stripMode ? 26 : 320
                Layout.minimumWidth: stripMode ? 26 : 220
                color: stripMode ? theme.chromeBg : theme.surfaceBg
                border.color: stripMode ? "transparent" : theme.borderSubtle
                border.width: 1
                clip: true

                // 窄边：一条竖排的展开按钮。
                Item {
                    anchors.fill: parent
                    visible: leftPanel.stripMode

                    AIconButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: theme.spacingXs
                        iconSource: "qrc:/icons/chevron-right.svg"
                        tooltip: qsTr("Show the workspace tree")
                        onClicked: {
                            if (preset === "prompt")
                                promptLeftOpen = true
                            else
                                documentLeftOpen = true
                        }
                    }
                }

                // 展开态：头部一排（工作区切换 + 选文件夹 + 刷新）+ 文件树。
                // 与原提示词页同一套结构，objectName 契约不变。
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    visible: !leftPanel.stripMode

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: theme.spacingXs
                        spacing: theme.spacingXs

                        AIconButton {
                            iconSource: "qrc:/icons/chevron-left.svg"
                            tooltip: qsTr("Hide the workspace tree")
                            onClicked: {
                                if (preset === "prompt")
                                    promptLeftOpen = false
                                else
                                    documentLeftOpen = false
                            }
                        }

                        AComboBox {
                            id: workspaceCombo

                            Layout.fillWidth: true
                            Layout.minimumWidth: 110
                            model: tools.workspaces
                            currentIndex: {
                                for (let i = 0; i < tools.workspaces.length; ++i) {
                                    if (tools.workspaces[i] === tools.currentWorkspace)
                                        return i
                                }
                                return -1
                            }
                            enabled: tools.workspaces.length > 0

                            // 路径可能很长，中部省略让盘符与末段同时可见。
                            contentItem: Text {
                                text: workspaceCombo.displayText
                                font.pixelSize: theme.fontSizeCaption
                                color: workspaceCombo.enabled ? theme.textPrimary
                                                              : theme.textDisabled
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                                leftPadding: 10
                                rightPadding: 34
                            }

                            onActivated: function(index) {
                                tools.currentWorkspace = tools.workspaces[index]
                            }

                            delegate: ItemDelegate {
                                id: workspaceChoice

                                required property var modelData
                                required property int index

                                width: workspaceCombo.width
                                highlighted: workspaceCombo.highlightedIndex === index
                                // 弹层底色随 AComboBox 主题化后，条目悬停态也要跟令
                                // 牌走（默认 ItemDelegate 背景是硬编码浅色系）。
                                background: Rectangle {
                                    color: workspaceChoice.hovered
                                           || workspaceChoice.highlighted
                                           ? theme.surfaceHoverBg : "transparent"
                                }

                                contentItem: RowLayout {
                                    spacing: theme.spacingS

                                    Label {
                                        Layout.fillWidth: true
                                        text: workspaceChoice.modelData
                                        color: theme.textPrimary
                                        font.pixelSize: theme.fontSizeCaption
                                        elide: Text.ElideMiddle
                                    }

                                    AIconButton {
                                        iconSource: "qrc:/icons/close.svg"
                                        tooltip: qsTr("Remove this workspace")
                                        onClicked: {
                                            const result =
                                                    tools.removeWorkspace(workspaceChoice.modelData)
                                            if (!result.ok)
                                                workbench.notify("error",
                                                                 qsTr("Could not remove the workspace"),
                                                                 result.error)
                                        }
                                    }
                                }
                            }
                        }

                        // 选工作区文件夹：只放图标，说明走 tooltip。
                        AIconButton {
                            iconSource: "qrc:/icons/folder.svg"
                            tooltip: qsTr("Choose a workspace folder")
                            onClicked: workbenchWindow.addWorkspaceViaDialog()
                        }

                        AIconButton {
                            iconSource: "qrc:/icons/refresh.svg"
                            tooltip: qsTr("Refresh the file tree")
                            enabled: tools.currentWorkspace.length > 0
                            onClicked: tools.refresh()
                        }
                    }

                    // --- 段 1 · 工作区与文件树（可折叠段，默认展开） --------
                    ColumnLayout {
                        id: workspaceSection
                        objectName: "workspaceSection"

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: theme.spacingXs
                        visible: true

                        ASectionHeader {
                            Layout.fillWidth: true
                            text: qsTr("Workspace")

                            AIconButton {
                                iconSource: workspaceSection.visible
                                        ? "qrc:/icons/chevron-left.svg"
                                        : "qrc:/icons/chevron-right.svg"
                                tooltip: workspaceSection.visible
                                        ? qsTr("Hide the file tree")
                                        : qsTr("Show the file tree")
                                onClicked:
                                    workspaceSection.visible = !workspaceSection.visible
                            }
                        }

                        // 文件树消费扁平投影（FileTreeFlatModel，经 tools.model
                        // 暴露）：Qt 6.3 的 TreeView 在 Qt 5.15 不存在，两个版本
                        // 统一用 ListView + depth/expanded role 渲染同一份数据。
                        // 交互级冒烟（tst_toolsui）经 objectName 定位本树。
                        ListView {
                            id: fileTree
                            objectName: "fileTree"

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: tools.currentWorkspace.length > 0
                                     && workspaceSection.visible
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: tools.model

                            ScrollBar.vertical: AScrollBar {}

                        delegate: Item {
                            id: treeRow

                            width: fileTree.width
                            height: 28

                            // DropArea 据此识别拖拽来源（见草稿区的 onDropped）。
                            readonly property bool isFileReferenceDrag: true
                            // 行被回收进池子到再次被复用之间为真。模型 reset
                            // 期间 role 会短暂换值，动画要闭嘴。
                            property bool rebinding: false

                            // role 一律经 required property 声明；index 同理：
                            // Qt 5.15 里声明了任何 required property 后
                            // contextObject 即被清空，裸 index 会抛
                            // ReferenceError。
                            required property string name
                            required property string path
                            required property string relativePath
                            required property bool isDir
                            required property string iconSource
                            required property int depth
                            required property bool expanded
                            required property bool hasChildren
                            required property int index

                            Connections {
                                target: tools.model
                                function onModelReset() {
                                    treeRow.rebinding = true
                                }
                            }
                            Component.onCompleted: treeRow.rebinding = false

                            // 拖出文件引用：Automatic 型拖拽，Drag.active 置真
                            // 即开始（mimeData 在会话开始时求值）。
                            Drag.dragType: Drag.Automatic
                            Drag.supportedActions: Qt.CopyAction
                            Drag.mimeData: {
                                "text/plain": tools.fileReference(treeRow.relativePath)
                            }
                            Drag.active: rowDragHandler.active

                            DragHandler {
                                id: rowDragHandler
                                target: null
                            }

                            // 悬停高亮：读树时逐行反馈落点。
                            Rectangle {
                                anchors.fill: parent
                                radius: theme.radiusControl
                                color: theme.surfaceHoverBg
                                visible: rowHover.hovered
                            }

                            HoverHandler {
                                id: rowHover
                            }

                            // 单击目录：切换展开（fetch 兜底在模型的
                            // toggleExpanded 内部完成）。
                            TapHandler {
                                onSingleTapped: {
                                    if (treeRow.isDir)
                                        tools.model.toggleExpanded(index)
                                }
                            }

                            // 双击文件：在草稿光标处插入引用（目录的双击被
                            // 两次单击的展开/收起抵消）。
                            TapHandler {
                                onDoubleTapped: {
                                    if (!treeRow.isDir) {
                                        promptEditor.insert(
                                                    promptEditor.cursorPosition,
                                                    tools.fileReference(
                                                        treeRow.relativePath))
                                    }
                                }
                            }

                            // 右键复制路径：只吃右键，左键留给上面的 TapHandler。
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: function(mouse) {
                                    pathMenu.popup(mouse.x, mouse.y)
                                }
                            }

                            // 右键：Markdown 文件多一条预览，其余仍是复制路径。
                            // 条目出不出场交给 workbench.canPreviewMarkdown()——
                            // 哪些后缀归预览管是纯层的事实，QML 不写后缀判断。
                            AMenu {
                                id: pathMenu
                                // tst_toolsui 的锚点：右键弹出后按名字找到这份
                                // 菜单，再断言「Preview Markdown」条目的出场。
                                objectName: "treeRowMenu"

                                AMenuItem {
                                    visible: workbench.canPreviewMarkdown(treeRow.path)
                                    icon.source: "qrc:/icons/file.svg"
                                    text: qsTr("Preview Markdown")
                                    onTriggered: workbench.previewMarkdown(treeRow.path)
                                }
                                AMenuSeparator {
                                    visible: workbench.canPreviewMarkdown(treeRow.path)
                                }
                                AMenuItem {
                                    icon.source: "qrc:/icons/copy.svg"
                                    text: qsTr("Copy relative path")
                                    onTriggered: workbench.copyText(treeRow.relativePath)
                                }
                                AMenuItem {
                                    icon.source: "qrc:/icons/copy.svg"
                                    text: qsTr("Copy absolute path")
                                    onTriggered: workbench.copyText(treeRow.path)
                                }
                            }

                            // 展开箭头：收起朝右，展开转 90 度朝下。
                            Image {
                                x: 4 + treeRow.depth * 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 14
                                source: "qrc:/icons/chevron-right.svg"
                                // sourceSize 与显示尺寸一致：SVG 不设时按
                                // 24px 自然尺寸栅格再缩小绘制，箭头会发糊。
                                sourceSize: Qt.size(14, 14)
                                visible: treeRow.hasChildren
                                rotation: treeRow.expanded ? 90 : 0

                                Behavior on rotation {
                                    enabled: !treeRow.rebinding
                                    NumberAnimation {
                                        duration: theme.durationFast
                                    }
                                }
                            }

                            // 图标由模型按文件名/后缀查表给出（见 FileIcons）。
                            Image {
                                x: 4 + treeRow.depth * 16 + 18
                                anchors.verticalCenter: parent.verticalCenter
                                width: 16
                                height: 16
                                source: treeRow.iconSource
                                sourceSize: Qt.size(16, 16)
                            }

                            Label {
                                x: 4 + treeRow.depth * 16 + 40
                                width: parent.width - x - 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: treeRow.name
                                color: theme.textPrimary
                                font.pixelSize: theme.fontSizeCaption
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    // 树的空态与未选态也收进工作区段（段收起时一起藏）。
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: workspaceSection.visible ? 60 : 0
                        visible: tools.currentWorkspace.length > 0
                                 && tools.model.visibleCount === 0
                                 && workspaceSection.visible
                        Layout.margins: theme.spacingM
                        text: qsTr("The workspace folder is empty or unavailable.")
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    AEmptyState {
                        Layout.fillWidth: true
                        Layout.preferredHeight: workspaceSection.visible ? 160 : 0
                        visible: tools.currentWorkspace.length === 0
                                 && workspaceSection.visible
                        iconSource: "qrc:/icons/folder.svg"
                        title: qsTr("No workspace selected")
                        description: qsTr("Add a folder to browse its files and insert references into the prompt.")
                        actionText: qsTr("Add Folder...")
                        onActionClicked: workbenchWindow.addWorkspaceViaDialog()
                    }

                    // workspaceSection 的收尾（段内容到此为止）。
                    }

                    // --- 段 2 · 模板库（可折叠段，默认展开） ----------------
                    // 既是浏览器也是选择器：Ribbon 的 Insert Template 引导到
                    // 这里（openTemplatePicker），不在 Ribbon 里再造一份
                    // 列表——SARibbon 的 gallery/menuItems 是静态
                    // listProperty，绑不了模型。
                    ColumnLayout {
                        id: templateSection
                        objectName: "templateSection"

                        Layout.fillWidth: true
                        Layout.preferredHeight: templateSectionExpanded ? 260 : 0
                        visible: templateSectionExpanded
                        spacing: theme.spacingXs

                        ASectionHeader {
                            Layout.fillWidth: true
                            text: qsTr("Templates")

                            AIconButton {
                                iconSource: "qrc:/icons/chevron-right.svg"
                                rotation: templateSectionExpanded ? 90 : 0
                                tooltip: templateSectionExpanded
                                        ? qsTr("Hide the template library")
                                        : qsTr("Show the template library")
                                onClicked:
                                    templateSectionExpanded = !templateSectionExpanded
                            }
                        }

                        // 检索框：实时过滤（filteredTemplates 的空+空=全部）。
                        ASearchField {
                            id: templateSearch
                            objectName: "templateSearch"

                            Layout.fillWidth: true
                            Layout.leftMargin: theme.spacingXs
                            Layout.rightMargin: theme.spacingXs
                            placeholderText: qsTr("Search templates")

                            onTextChanged: templateModel.rebuild()
                        }

                        // 列表上限 30：检索后一般够用；全部模板走目录管理。
                        // 高度跟内容（同收藏列表的理由：fillHeight 对零
                        // 内容量不出高），段高由 preferredHeight 兜住。
                        ListView {
                            id: templateList
                            objectName: "templateList"

                            implicitHeight: Math.min(contentHeight, 220)
                            Layout.fillWidth: true
                            Layout.leftMargin: theme.spacingXs
                            Layout.rightMargin: theme.spacingXs
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            spacing: 2
                            model: ObjectModel {
                                id: templateModel

                                // 检索结果 → 行对象（上限 30）。
                                function rebuild() {
                                    while (count > 0)
                                        remove(0)
                                    const filtered = tools.filteredTemplates(
                                                templateSearch.text, "")
                                    const cap = Math.min(filtered.length, 30)
                                    for (let i = 0; i < cap; ++i) {
                                        append(rowComponent.createObject(
                                                   templateModel, {
                                                       "entry": filtered[i]
                                                   }))
                                    }
                                }

                                Component {
                                    id: rowComponent

                                    AListRow {
                                        id: templateRow
                                        required property var entry

                                        // ObjectModel 的行是视图直接摆放的
                                        // 对象：宽度必须显式跟列表（AListRow
                                        // 只有 height 没有 implicitWidth，
                                        // 不设就是 0 宽——点击永远落空）。
                                        width: templateList.width
                                        rowHeight: 40

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: theme.spacingS
                                            anchors.rightMargin: theme.spacingS
                                            spacing: theme.spacingS

                                            Label {
                                                Layout.fillWidth: true
                                                text: templateRow.entry.title
                                                color: theme.textPrimary
                                                font.pixelSize: theme.fontSizeCaption
                                                elide: Text.ElideMiddle
                                            }

                                            // 内置徽标：subtle 的 APill，与
                                            // 文件 tab 的来源徽标同一形态。
                                            APill {
                                                visible: templateRow.entry.builtin
                                                text: qsTr("built-in")
                                            }
                                        }

                                        TapHandler {
                                            onSingleTapped:
                                                workbenchWindow.insertTemplate(
                                                    templateRow.entry.id)
                                        }
                                    }
                                }
                            }

                            Connections {
                                target: tools
                                function onTemplatesChanged() {
                                    templateModel.rebuild()
                                }
                            }

                            Component.onCompleted: templateModel.rebuild()

                            onVisibleChanged: {
                                if (visible)
                                    templateModel.rebuild()
                            }
                        }
                    }

                    // --- 段 3 · 收藏夹（可折叠段，默认收起） ----------------
                    ColumnLayout {
                        id: favoriteSection
                        objectName: "favoriteSection"

                        Layout.fillWidth: true
                        Layout.preferredHeight: favoriteSectionExpanded ? 200 : 0
                        visible: favoriteSectionExpanded
                        spacing: theme.spacingXs

                        ASectionHeader {
                            Layout.fillWidth: true
                            text: qsTr("Favorites")

                            AIconButton {
                                iconSource: "qrc:/icons/chevron-right.svg"
                                rotation: favoriteSectionExpanded ? 90 : 0
                                tooltip: favoriteSectionExpanded
                                        ? qsTr("Hide the favorites")
                                        : qsTr("Show the favorites")
                                onClicked:
                                    favoriteSectionExpanded = !favoriteSectionExpanded
                            }
                        }

                        // 空态：一行弱化提示（收藏是成稿，模板是骨架）。
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: theme.spacingS
                            Layout.rightMargin: theme.spacingS
                            visible: tools.favorites.length === 0
                            text: qsTr("Save finished prompts here")
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                            wrapMode: Text.Wrap
                        }

                        ListView {
                            id: favoriteList
                            objectName: "favoriteList"

                            // fillHeight 对零内容的列表量不出高度（段
                            // 展开时高度仍为 0，行被裁掉）；收藏夹给内容
                            // 高度，段高由 preferredHeight 兜住上限。
                            implicitHeight: Math.min(contentHeight, 160)
                            Layout.fillWidth: true
                            Layout.leftMargin: theme.spacingXs
                            Layout.rightMargin: theme.spacingXs
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            spacing: 2

                            model: ObjectModel {
                                id: favoriteModel

                                function rebuild() {
                                    while (count > 0)
                                        remove(0)
                                    const favorites = tools.favorites
                                    for (let i = 0; i < favorites.length; ++i) {
                                        append(favoriteRow.createObject(
                                                   favoriteModel, {
                                                       "entry": favorites[i]
                                                   }))
                                    }
                                }

                                Component {
                                    id: favoriteRow

                                    AListRow {
                                        id: favRow
                                        required property var entry

                                        // 同模板行：宽度显式跟列表。
                                        width: favoriteList.width
                                        rowHeight: 40

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: theme.spacingS
                                            spacing: theme.spacingS

                                            Label {
                                                Layout.fillWidth: true
                                                text: favRow.entry.title
                                                color: theme.textPrimary
                                                font.pixelSize: theme.fontSizeCaption
                                                elide: Text.ElideMiddle
                                            }

                                            // 行内动作：复制与删除（悬停可见）。
                                            AIconButton {
                                                iconSource: "qrc:/icons/copy.svg"
                                                tooltip: qsTr("Copy this favorite")
                                                onClicked:
                                                    workbench.copyText(favRow.entry.body)
                                            }
                                            AIconButton {
                                                iconSource: "qrc:/icons/close.svg"
                                                tooltip: qsTr("Remove this favorite")
                                                onClicked:
                                                    tools.removeFavorite(
                                                        favRow.entry.title,
                                                        favRow.entry.body)
                                            }
                                        }

                                        TapHandler {
                                            onSingleTapped:
                                                workbenchWindow.insertTextAtCursor(
                                                    favRow.entry.body)
                                        }
                                    }
                                }
                            }

                            Connections {
                                target: tools
                                function onFavoritesChanged() {
                                    favoriteModel.rebuild()
                                }
                            }

                            Component.onCompleted: favoriteModel.rebuild()

                            onVisibleChanged: {
                                if (visible)
                                    favoriteModel.rebuild()
                            }
                        }
                    }
                }
            }

            // --- 中间：文档区 ----------------------------------------------
            // 文件 tab 条 + 三态 + 状态条。tab 只装文件会话（草稿是提示词
            // 形态的隐式主体，不占 tab；多草稿并入 tab 条是 M4 的事）。
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // tab 条：一条贴顶的窄条。文件 tab 可点（激活 + 切文档
                // 预设）、可关（关视图不删文件）；脏标记以圆点呈现，
                // 不只靠颜色。
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
                                required property var modelData
                                required property int index

                                // 根用 Item + 视觉背景放内层：delegate 的
                                // required property 与同名视觉属性的遮蔽坑
                                //（AGENTS.md QML 契约）。
                                readonly property bool isActive:
                                    fileTab.modelData.active

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

                                    Label {
                                        text: fileTab.modelData.title
                                        color: fileTab.isActive
                                              ? theme.textPrimary
                                              : theme.textSecondary
                                        font.pixelSize: theme.fontSizeCaption
                                        elide: Text.ElideMiddle
                                    }

                                    AIconButton {
                                        iconSource: "qrc:/icons/close.svg"
                                        tooltip: qsTr("Close this tab")
                                        onClicked: {
                                            markdown.closeSession(
                                                fileTab.modelData.docId)
                                        }
                                    }
                                }

                                // 点 tab：激活该会话并切到文档预设。点关闭
                                // 按钮不该走到这里——AIconButton 自己吃点击。
                                TapHandler {
                                    onSingleTapped: {
                                        markdown.activateSession(
                                            fileTab.modelData.docId)
                                        workbenchWindow.applyPreset("document")
                                    }
                                }
                            }
                        }

                        // 三态是「当前在哪」，用 variant 表达选中。两种预设
                        // 都在场：文档模式对文件，提示词模式对草稿——渲染管
                        // 线吃文本不吃文件，草稿同样有 Preview/Split。
                        RowLayout {
                            visible: markdown.available
                            spacing: theme.spacingXs

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
                                    Layout.preferredWidth: 72
                                    variant: markdown.mode === modelData.key
                                             ? "primary" : "ghost"
                                    // 只读文档没有源码可编：Split / Source
                                    // 直接禁用，而不是让人切到一个只能看
                                    // 的编辑框。
                                    enabled: modelData.key === "preview"
                                             || markdown.splitAvailable
                                    onClicked: markdown.setMode(modelData.key)
                                }
                            }
                        }

                        AIconButton {
                            iconSource: "qrc:/icons/bullet-list.svg"
                            tooltip: qsTr("Show outline")
                            active: markdown.showOutline
                            // 大纲对草稿同样成立（它也是一份有标题的正文）。
                            onClicked: markdown.setShowOutline(!markdown.showOutline)
                        }

                        AIconButton {
                            visible: preset === "document"
                            iconSource: "qrc:/icons/search.svg"
                            tooltip: qsTr("Search in document")
                            active: searchOpen
                            onClicked: {
                                if (searchOpen) {
                                    workbenchWindow.closeSearch()
                                    return
                                }
                                workbenchWindow.openSearch()
                            }
                        }

                        AComboBox {
                            Layout.preferredWidth: 120
                            // 标签与值同序：0 = 跟随主题的 fontSizeBody。
                            property var values: [0, 12, 13, 14, 16, 18, 20, 24]
                            model: [qsTr("Theme size"), "12 px", "13 px",
                                    "14 px", "16 px", "18 px", "20 px", "24 px"]
                            currentIndex: values.indexOf(markdown.fontSizePx)
                            onActivated: function (index) {
                                markdown.setFontSizePx(values[index])
                            }
                        }

                        AComboBox {
                            Layout.preferredWidth: 120
                            // 0 = 内置基准。末项是「尽量满栏」。
                            property var values: [0, 640, 760, 900, 1200, 4000]
                            model: [qsTr("Default width"), "640 px", "760 px",
                                    "900 px", "1200 px", qsTr("Full width")]
                            currentIndex: values.indexOf(markdown.contentWidthPx)
                            onActivated: function (index) {
                                markdown.setContentWidthPx(values[index])
                            }
                        }

                        // 更多动作（复制选区/源码、导出、重载）收进菜单；
                        // Ribbon 构建里这些动作在「开始」页，工具栏构建里
                        // 也经这里触达。
                        AIconButton {
                            visible: preset === "document"
                            iconSource: "qrc:/icons/menu.svg"
                            tooltip: qsTr("More actions")
                            onClicked: actionsMenu.popup()
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: theme.separator
                }

                // --- 草稿查找条（B2：Ctrl+F 在提示词预设时展开这里） ---------
                // 不弹对话框：一条从编辑区顶部滑出的横条（规划 03 §三）。
                RowLayout {
                    id: draftFindBar
                    objectName: "draftFindBar"
                    visible: false
                    Layout.fillWidth: true
                    Layout.leftMargin: theme.spacingM
                    Layout.rightMargin: theme.spacingM
                    Layout.topMargin: theme.spacingXs
                    Layout.bottomMargin: theme.spacingXs
                    spacing: theme.spacingS

                    ASearchField {
                        id: draftFindField
                        Layout.preferredWidth: 220
                        placeholderText: qsTr("Find in draft")
                        onAccepted: workbenchWindow.draftFindNext()
                    }

                    ATextField {
                        id: draftReplaceField
                        Layout.preferredWidth: 180
                        placeholderText: qsTr("Replace with")
                    }

                    AButton {
                        text: qsTr("Aa")
                        Layout.preferredWidth: 44
                        variant: tools.findCaseSensitive
                                 ? "primary" : "ghost"
                        ToolTip.delay: 300
                        ToolTip.timeout: 10000
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Match case")
                        onClicked: {
                            tools.findCaseSensitive =
                                    !tools.findCaseSensitive
                            workbenchWindow.draftFindNext()
                        }
                    }

                    AButton {
                        text: qsTr("Whole")
                        variant: tools.findWholeWords ? "primary" : "ghost"
                        onClicked: {
                            tools.findWholeWords = !tools.findWholeWords
                            workbenchWindow.draftFindNext()
                        }
                    }

                    AButton {
                        text: qsTr("Previous")
                        variant: "ghost"
                        // 逐个换向是「再找一次上一个」：实现与下一个共用
                        // 一条 find（从选区起点向前没有独立实现——回卷
                        // 语义已覆盖）。低频按钮保持简单。
                        onClicked: {
                            workbenchWindow.draftFindNext()
                        }
                    }

                    AButton {
                        text: qsTr("Next")
                        variant: "primary"
                        onClicked: workbenchWindow.draftFindNext()
                    }

                    AButton {
                        text: qsTr("Replace")
                        onClicked: workbenchWindow.draftReplaceCurrent()
                    }

                    AButton {
                        text: qsTr("All")
                        onClicked: workbenchWindow.draftReplaceAll()
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    AIconButton {
                        iconSource: "qrc:/icons/close.svg"
                        tooltip: qsTr("Close find")
                        onClicked: workbenchWindow.closeDraftFind()
                    }
                }

                // --- 搜索条（Ctrl+F 展开，仅文档模式） ---------------------
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: theme.spacingM
                    Layout.rightMargin: theme.spacingM
                    Layout.topMargin: theme.spacingS
                    Layout.bottomMargin: theme.spacingS
                    visible: preset === "document" && searchOpen
                    spacing: theme.spacingS

                    ASearchField {
                        id: searchField
                        Layout.preferredWidth: 280
                        placeholderText: qsTr("Find in document")
                        onTextChanged: searchDebounce.restart()
                        onAccepted: {
                            workbenchWindow.searchForward = true
                            workbenchWindow.runSearch(true)
                        }
                    }

                    // 逐键搜索在 2 MB 文档上就是每键扫一遍全篇 DOM：停
                    // 200 ms 再问。
                    Timer {
                        id: searchDebounce
                        interval: 200
                        onTriggered: workbenchWindow.runSearch(true)
                    }

                    AButton {
                        text: qsTr("Aa")
                        Layout.preferredWidth: 44
                        variant: workbenchWindow.caseSensitive ? "primary" : "ghost"
                        ToolTip.delay: 300
                        ToolTip.timeout: 10000
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Match case")
                        onClicked: {
                            workbenchWindow.caseSensitive =
                                    !workbenchWindow.caseSensitive
                            workbenchWindow.runSearch(true)
                        }
                    }

                    AButton {
                        text: qsTr("Previous")
                        variant: "ghost"
                        onClicked: {
                            workbenchWindow.searchForward = false
                            workbenchWindow.runSearch(false)
                        }
                    }
                    AButton {
                        text: qsTr("Next")
                        variant: "ghost"
                        onClicked: {
                            workbenchWindow.searchForward = true
                            workbenchWindow.runSearch(false)
                        }
                    }

                    Label {
                        text: searchField.text.length === 0
                              ? ""
                              : (workbenchWindow.findTotal === 0
                                 ? qsTr("No matches")
                                 : qsTr("%1 of %2").arg(workbenchWindow.findActive)
                                       .arg(workbenchWindow.findTotal))
                        color: theme.textMuted
                        font.pixelSize: theme.fontSizeCaption
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    AIconButton {
                        iconSource: "qrc:/icons/close.svg"
                        tooltip: qsTr("Close search")
                        onClicked: workbenchWindow.closeSearch()
                    }
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

                            placeholderText: qsTr("Write your prompt here. Enter only inserts a new line; nothing is sent from this window.")
                            // 只在初始化时从门面取草稿：常驻双向绑定会与
                            // 用户输入互相打架。
                            Component.onCompleted: {
                                text = tools.draft
                                // markdown 语法高亮在此时挂到 textDocument 上；
                                // 主题切换由 MarkdownEdit 自己跟随。
                                MarkdownEdit.attach(promptEditor)
                            }
                            // 双写：tools.draft 走门面自己的 500 ms 防抖落
                            // tools.json；草稿会话走 200 ms 防抖——一次敲键
                            // 要整篇重切块，逐键喂是把 CPU 花在马上会被改
                            // 掉的中间态上。
                            onTextChanged: {
                                tools.draft = text
                                draftSyncTimer.restart()
                            }

                            // 拖放目标：接住来自文件树的行，把文件引用插到
                            // 落点光标处。
                            DropArea {
                                anchors.fill: parent

                                onEntered: function(drag) {
                                    drag.accept(Qt.CopyAction)
                                }
                                onDropped: function(drop) {
                                    // 只接受文件树来的行：编辑区自身的选区
                                    // 拖动（source 为空）不在此列。
                                    if (!drop.source
                                            || !drop.source.isFileReferenceDrag)
                                        return
                                    // 落点坐标即文档坐标：滚动是 Flickable
                                    // 整体平移编辑器，positionAt 直接用本地
                                    // 坐标即可。
                                    promptEditor.insert(
                                                promptEditor.positionAt(drop.x, drop.y),
                                                drop.text)
                                    drop.acceptProposedAction()
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
                            }
                        }
                    }

                    // 草稿会话的增量喂入（与 editorSyncTimer 同一条纪律）。
                    Timer {
                        id: draftSyncTimer
                        interval: 200
                        onTriggered: markdown.setDraftText(promptEditor.text)
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
                                // 用绝对坐标算比例：相对增量要在 docArea
                                // 坐标系里折，而 x / width 本来就是这里唯一
                                // 需要的两个数。
                                onPositionChanged: function(mouse) {
                                    if (!pressed || !docArea.showsBoth)
                                        return
                                    const at = mapToItem(docArea, mouse.x, 0)
                                    workbenchWindow.editorShare = Math.max(
                                                0.15,
                                                Math.min(0.75,
                                                         at.x / docArea.width))
                                }
                            }
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
                    }
                }
            }

            // --- 右侧：大纲（活跃会话的查阅面板） -------------------------
            // 大纲对草稿同样成立（它也是一份有标题的正文）。
            Rectangle {
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

                    // 大纲：数据是控制器算好的（level / text / anchor /
                    // blockIndex），这里只画。点一条走 scrollToAnchor——页面
                    // 会把那一块的解析插队到分片队列前面。
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
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: theme.separator
                }
            }

            // --- 右侧：体检结果（提示词预设的检查面板，B9） ----------------
            // 体检是提示词质量工具，不是 Markdown 语法工具——只在提示词
            // 预设出现（文件 tab 上不占位）。清单不评分：分数会诱导为
            // 分数写作，清单引导补全信息。
            Rectangle {
                id: checksPanel
                objectName: "checksPanel"
                Layout.fillHeight: true
                Layout.preferredWidth: 264
                visible: workbenchWindow.preset === "prompt"
                color: theme.surfaceBg

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.spacingS
                    spacing: theme.spacingXs

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.spacingS

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Checks")
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                            font.bold: true
                        }
                        AButton {
                            objectName: "checkPromptButton"
                            text: qsTr("Check")
                            variant: "ghost"
                            onClicked: workbenchWindow.runDraftChecks()
                        }
                    }

                    // 命中列表：色点按严重度（0 提示 / 1 提醒 / 2 警告），
                    // 点行跳到编辑区对应区间并选中。「不再提醒」按规则关
                    // 闭（写 tools.json 的 checks 段），该类命中即刻隐去。
                    ListView {
                        id: checkList
                        objectName: "checkList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: workbenchWindow.draftCheckResults || []

                        ScrollBar.vertical: AScrollBar {}

                        delegate: Item {
                            id: checkRow

                            required property var modelData
                            required property int index

                            width: checkList.width
                            // 两行文字 + 关闭按钮的行高；消息长短不定，
                            // 按内容给（最大三行封顶）。
                            height: checkText.implicitHeight
                                    + theme.spacingS

                            Rectangle {
                                anchors.fill: parent
                                radius: theme.radiusControl
                                color: theme.surfaceHoverBg
                                visible: checkHover.hovered
                            }

                            HoverHandler {
                                id: checkHover
                            }

                            TapHandler {
                                // 区间可定位（start >= 0）才可跳转；规则级
                                // 的命中（如缺验收标准）没有对应区间。
                                onSingleTapped: {
                                    if (checkRow.modelData.start >= 0
                                            && workbenchWindow.promptEditor) {
                                        workbenchWindow.promptEditor.select(
                                            checkRow.modelData.start,
                                            checkRow.modelData.start
                                            + checkRow.modelData.length)
                                        workbenchWindow.promptEditor
                                            .forceActiveFocus()
                                    }
                                }
                            }

                            // 严重度色点：提醒用注意色、警告用警示色、
                            // 提示用弱化色（不抢注意力）。
                            Rectangle {
                                x: theme.spacingS
                                anchors.verticalCenter: parent.verticalCenter
                                width: 6
                                height: 6
                                radius: 3
                                color: checkRow.modelData.severity === 2
                                       ? theme.danger
                                       : (checkRow.modelData.severity === 1
                                          ? theme.warning
                                          : theme.textDisabled)
                            }

                            Label {
                                id: checkText
                                x: theme.spacingS + 14
                                width: parent.width - x - theme.spacingS - 20
                                text: checkRow.modelData.message
                                color: theme.textPrimary
                                font.pixelSize: theme.fontSizeCaption
                                wrapMode: Text.Wrap
                            }

                            AIconButton {
                                anchors.right: parent.right
                                anchors.rightMargin: theme.spacingXs
                                anchors.verticalCenter: parent.verticalCenter
                                iconSource: "qrc:/icons/close.svg"
                                tooltip: qsTr("Don't remind me of this again")
                                onClicked: {
                                    const disabled =
                                            tools.disabledCheckIds.slice()
                                    disabled.push(checkRow.modelData.ruleId)
                                    tools.disabledCheckIds = disabled
                                    // 关掉即重跑：该类命中从当前清单消失。
                                    workbenchWindow.runDraftChecks()
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            // null = 没跑过（引导语）；跑过且为空 = 真的没问题。
                            visible: checkList.count === 0
                            text: workbenchWindow.draftCheckResults === null
                                  ? qsTr("Run a check for concrete suggestions.")
                                  : qsTr("No issues found.")
                            color: theme.textMuted
                            font.pixelSize: theme.fontSizeCaption
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: theme.separator
                }
            }
        }
    }

    // --- 与控制器、工作台意图之间的全部信号 --------------------------------
    Connections {
        target: markdown

        // 窗口已经活着时再次右键预览：控制器只会再发一次 windowRequested
        // 信号，主窗那边的 Loader 状态不变，把窗口抬起来并切到文档预设
        // 这两件事只有这里能做。
        function onWindowRequested() {
            workbenchWindow.raise()
            workbenchWindow.requestActivate()
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
            // 换了一篇文档 = 页面重挂 DOM：旧的命中序号与「报过一次」的
            // 闸门都不再成立，搜索条开着就立刻按新内容重跑一次。
            renderNoticeShown = false
            if (searchOpen)
                workbenchWindow.runSearch(true)
            else {
                findTotal = 0
                findActive = 0
            }
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

        function onFindResult(total, active) {
            findTotal = total
            findActive = active
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

    Connections {
        target: workbench

        // 工作台意图（侧栏启动页的主按钮）：已开着的窗口抬起来并回到
        // 提示词预设；没开时这个信号由主窗的 Loader 消费。
        function onMarkdownWorkbenchRequested() {
            workbenchWindow.raise()
            workbenchWindow.requestActivate()
            workbenchWindow.applyPreset("prompt")
        }
    }

    // 外部改动的窗口激活期复查：QFileSystemWatcher 在 Windows 上会静默丢掉
    // 「删除后重建」的文件，光靠监视器有漏报，回到窗口时再问一次。
    onActiveChanged: {
        if (active && preset === "document")
            markdown.refreshDiskState()
    }

    // --- 更多动作菜单（文档模式） ------------------------------------------
    AMenu {
        id: actionsMenu

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

    // 「保存 / 放弃 / 取消」三态：AConfirmDialog 只有两个按钮，所以直接基于
    // ADialog 写（与主窗的退出确认同形，见 designs.md）。守卫覆盖**所有**
    // 脏的文件会话，不只是激活的那个——静默丢掉一个后台 tab 的改动是这个
    // 窗口能犯的最贵的错。
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
                text: dirtyFileTabs.length === 1
                      ? qsTr("Save the changes to %1 before closing?")
                            .arg(dirtyFileTabs[0].title)
                      : qsTr("%1 documents have unsaved changes. Save them before closing?")
                            .arg(dirtyFileTabs.length)
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
                    text: dirtyFileTabs.length === 1
                          ? qsTr("Save and close")
                          : qsTr("Save all and close")
                    onClicked: {
                        unsavedDialog.close()
                        if (workbenchWindow.saveAllDirtySessions())
                            workbenchWindow.approveClose()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Discard changes")
                    onClicked: {
                        unsavedDialog.close()
                        workbenchWindow.approveClose()
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

    // 全部替换的一次确认（规划 03 §三：替换条数要可见，动作不可逆时
    // 先问一声）。Esc/Cancel 走 ADialog 的 reject。
    ADialog {
        id: replaceAllConfirm
        width: 420
        titleText: qsTr("Replace all")

        ColumnLayout {
            width: replaceAllConfirm.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Replace every occurrence of “%1” in the draft?")
                            .arg(draftFindField.text)
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
                    text: qsTr("Replace all")
                    onClicked: {
                        replaceAllConfirm.close()
                        workbenchWindow.doReplaceAll()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: replaceAllConfirm.close()
                }
            }
        }
    }

    // B3 清空草稿的确认：> 200 字符的内容先问再清（规划 03 §四）。
    // 双保险之二：clear() 本身是可撤销的编辑动作，Ctrl+Z 也能回。
    ADialog {
        id: clearDraftConfirm
        width: 420
        titleText: qsTr("Clear the draft")

        ColumnLayout {
            width: clearDraftConfirm.availableWidth
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Delete the draft text? You can undo it right after with Ctrl+Z.")
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
                    text: qsTr("Clear")
                    onClicked: {
                        clearDraftConfirm.close()
                        workbenchWindow.doClearDraft()
                    }
                }
                AButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: clearDraftConfirm.close()
                }
            }
        }
    }
}
