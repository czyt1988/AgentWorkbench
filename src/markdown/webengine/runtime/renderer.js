// 分块渲染管线：把 C++ 切好的块逐趟解析、攒批挂载，并按 sha1 做增量重渲染。
//
// 这套管线存在的唯一理由是文档量级。实测一篇 486 KB / 3150 块的文档：解析加
// 建元素约 1.9 s，把攒好的块挂上树约 0.03 s，全篇强制一次布局也是 0.03 s 量级
// （屏外块的排版由 CSS 的 content-visibility 跳过，所以布局几乎不要钱）。也就
// 是说这里省的不是「总量」——该做的解析一次都省不掉——而是「一次性」：VSCode
// 的预览把整篇的账压在首屏那一趟上，这里把它摊开，首屏只付第一趟。三件事分开：
//
//  1. 解析按趟分片进行（一趟占满 PASS_BUDGET_MS 就让出事件循环），白屏时间由
//     「整篇」变成「第一趟」；
//  2. 挂载攒进 batch，攒够一批（或解析已排空、或一批等得久了）才上树：DOM
//     变更的代价按「上树次数」算，不按块数算；
//  3. 屏外块的布局与绘制由 CSS 的 content-visibility 跳过（见 theme.css），
//     JS 不做窗口虚拟化——那会废掉选区、原生滚动定位与文本可访问性。
//
// 编辑态走 updateBlocks：C++ 侧按块 sha1 算出最小替换区间，这里只重渲染那几
// 块，其余 DOM 原样不动。这是「敲一个字不重排全篇」的那一半，另一半是上一条。
//
// 面向用户的文案一律不在这里：strings 由 doc.json 下发（MarkdownRuntime::
// pageStrings()），label() 缺键时退回键名——可见的错，好过静默的空白。

import { createMarkdown, escapeHtml, normalizeRelative, encodeSegments }
    from "./markdown-config.js";
import { configure as configureMath } from "./math-rule.js";
import { createDiagramHost } from "./diagram.js";
import { createFinder } from "./find.js";

const SLICE_BLOCKS = 256;      // 一趟的块数上限（防病态：一块就是半屏大表格）
const PASS_BUDGET_MS = 48;     // 一趟最多占用主线程的毫秒数（长任务门槛 50 ms
                               // 以下：再大就开始丢输入，见 schedule()）
const FIRST_BUDGET_MS = 32;    // 首屏那一趟放宽：白屏比迟几毫秒更难受
const MOUNT_BATCH_BLOCKS = 512; // 攒够这么多块才上树（一次上树 = 一次全篇排版）
const MOUNT_MAX_WAIT_MS = 300; // 小文档攒不满批时的兜底：别让内容等在半路
const HIGHLIGHT_MARGIN = "200px";

const HEADING = /^H[1-6]$/;

function nowMs()
{
    return (globalThis.performance && globalThis.performance.now)
        ? globalThis.performance.now() : Date.now();
}

function isExternalRef(ref)
{
    const text = String(ref || "").trim();
    if (!text || text.charAt(0) === "#") {
        return true;        // 锚点不映射
    }
    if (text.charAt(0) === "/" && text.slice(0, 2) !== "//") {
        return false;       // 站外绝对路径：按文档目录处理（少数 md 这么写）
    }
    return /^[a-z][a-z0-9+.-]*:/i.test(text) || text.slice(0, 2) === "//";
}

export function createRenderer(options)
{
    const root = options.root;
    const emptyEl = options.emptyEl;
    const onError = options.onError || function () {};
    const onFrame = options.onFrame || function () {};

    let doc = null;           // doc.json 的元信息部分
    let blocks = [];          // [{i,s,e,h,t}]
    let nodes = [];           // 与 blocks 同长；null = 还没挂载
    let pending = [];         // 待解析的块下标（升序）
    let batch = [];           // 攒着待挂载的 {index, element}
    let strings = {};
    let vars = {};
    let stylesheet = "";
    let md = null;
    let scheduled = false;
    let lastFlushMs = 0;      // 上一次上树的时刻，flushDue 的「攒太久」判据
    let appendCursor = 0;     // 顺序追加快路径：下一个 append 应当落在这下标
    let parseMs = 0;
    let mountMs = 0;
    let passes = 0;           // 本次文档跑过多少趟解析（吞吐的诊断口径）
    let renderStartedAt = 0;

    function label(key)
    {
        const value = strings[key];
        return typeof value === "string" && value ? value : key;
    }

    // 文档相对资源 → awbmd://runtime/asset/<docId>/<rel>
    function assetUrl(relPath)
    {
        if (!doc || !doc.assetBase || isExternalRef(relPath)) {
            return "";
        }
        const rel = normalizeRelative(relPath).replace(/^\/+/, "");
        if (!rel) {
            return "";
        }
        return doc.assetBase + encodeSegments(rel);
    }

    const diagram = createDiagramHost({
        label: label,
        variant: function () { return vars["--awb-variant"] || "dark"; },
        onError: function (info) { onError(info); },
        onIdle: function () { reportFrame(); }
    });

    const finder = createFinder({});

    // --- 代码高亮：延后到靠近视口时 ----------------------------------------
    // highlight.min.js 立即加载（代码块底色要用它），但逐元素的 highlightElement
    // 由这个观察器触发：几万行代码全量高亮是秒级的，而用户一次只看得见的部分
    // 远小于此。
    let codeObserver = null;
    function ensureCodeObserver()
    {
        if (codeObserver) {
            return codeObserver;
        }
        codeObserver = new IntersectionObserver((entries) => {
            entries.forEach((entry) => {
                if (!entry.isIntersecting) {
                    return;
                }
                codeObserver.unobserve(entry.target);
                highlight(entry.target);
            });
        }, {rootMargin: HIGHLIGHT_MARGIN});
        return codeObserver;
    }

    function highlight(codeEl)
    {
        const hljs = globalThis.hljs;
        if (!hljs || typeof hljs.highlightElement !== "function") {
            return;   // 没有高亮也不影响阅读：正文颜色由 theme.css 给
        }
        if (codeEl.dataset.awbHl !== "1") {
            return;
        }
        codeEl.dataset.awbHl = "done";
        // 我们交给它的是 markdown-it 转义过的内容；这两项只影响它自己的告警。
        hljs.configure({ignoreUnescapedHTML: true, throwUnescapedHTML: false});
        try {
            hljs.highlightElement(codeEl);
        } catch (error) {
            codeEl.dataset.awbHl = "0";
        }
    }

    // --- 单块渲染 ----------------------------------------------------------

    function renderBlock(index)
    {
        const block = blocks[index];
        const element = document.createElement("section");
        element.className = "awb-block";
        element.dataset.hash = block.h || "";
        element.dataset.startLine = String(block.s);
        const started = nowMs();
        try {
            element.innerHTML = md.render(block.t || "");
        } catch (error) {
            // 单块解析失败不该让整篇空白：把原因就地显示，正文继续渲染。
            element.classList.add("awb-block-error");
            const note = document.createElement("p");
            note.textContent = label("renderError");
            const pre = document.createElement("pre");
            pre.textContent = block.t || "";
            element.replaceChildren(note, pre);
            onError({blockIndex: block.i,
                     message: String(error && error.message ? error.message : error)});
        }
        parseMs += nowMs() - started;
        if (HEADING.test(String(element.firstElementChild
                                   ? element.firstElementChild.tagName : ""))) {
            // 大纲锚点：C++ 侧的 anchor 就是 b<块索引>（outlineFromBlocks），
            // 只有标题块需要它——非标题块挂 id 是白挂，还会在插入后成为
            // renumberAnchors() 的负担。
            element.id = "b" + block.i;
        }
        diagram.install(element);
        if (globalThis.hljs) {
            element.querySelectorAll("code[data-awb-hl]").forEach((codeEl) => {
                ensureCodeObserver().observe(codeEl);
            });
        }
        return element;
    }

    // --- 分片与批量挂载 ----------------------------------------------------

    function place(index, element)
    {
        if (index === appendCursor) {
            root.appendChild(element);
            appendCursor = index + 1;
            return;
        }
        // 中间插入：找后面第一个「已在树上」的块当参照点，插到它前面。
        // 参照点必须核对 parentNode：parseSlice 会先填 nodes[] 再把元素
        // 攒进 batch 等批量上树，这批元素「已解析但未挂载」，insertBefore
        // 拿它们当参照点会抛 NotFoundError（不是 root 的子节点）——那个
        // 未捕获异常会打断整个 flush 宏任务，batch 里的其余块从此既不在
        // DOM 也不在 pending，页面就此缺块（2026-10 用户报告的顶部内容
        // 丢失）。跳过未挂载的参照点是安全的：批内元素随后仍按各自下标
        // 走同一套 place() 定位。
        for (let j = index + 1; j < nodes.length; j += 1) {
            if (nodes[j] && nodes[j].parentNode === root) {
                root.insertBefore(element, nodes[j]);
                return;
            }
        }
        root.appendChild(element);
        appendCursor = Math.max(appendCursor, index + 1);
    }

    /**
     * 把攒下的块一次挂上树。
     *
     * 一次 DOM 变更 = 一次全篇排版：Blink 的布局从插入点往后要重走一遍，实测
     * 每趟 20~40 ms，与文档规模同阶。所以「每块一次 appendChild」在这里不是
     * 快一点的做法，而是慢一个数量级的做法：大文档要在帧间挂完，靠的是**减少
     * 上树的次数**，而不是把上树拆得更碎。判据见 flushDue()。
     */
    function flushBatch()
    {
        if (!batch.length) {
            return;
        }
        const started = nowMs();
        const fragment = document.createDocumentFragment();
        const items = batch;
        batch = [];
        lastFlushMs = started;
        // 批量内按下标升序处理时，一次 appendChild 就够；乱序时退回逐个插入
        let contiguous = true;
        for (let i = 0; i < items.length; i += 1) {
            if (items[i].index !== appendCursor + i) {
                contiguous = false;
                break;
            }
        }
        if (contiguous) {
            items.forEach((item) => fragment.appendChild(item.element));
            root.appendChild(fragment);
            appendCursor += items.length;
        } else {
            items.forEach((item) => place(item.index, item.element));
        }
        mountMs += nowMs() - started;
    }

    /**
     * 该上树了才上树：攒够一批、或者解析已经排空、或者一批攒得太久。
     *
     * 最后那条是给「小文档 + 大阈值」兜底的：一篇只有 30 块的文档永远攒不满
     * MOUNT_BATCH_BLOCKS，不许它因此等上几百毫秒。
     */
    function flushDue()
    {
        if (!batch.length) {
            return;
        }
        if (batch.length >= MOUNT_BATCH_BLOCKS || !pending.length
            || nowMs() - lastFlushMs >= MOUNT_MAX_WAIT_MS) {
            flushBatch();
        }
    }

    /**
     * @brief 解析一趟，结果攒进待挂载的 batch
     * @param cap 本趟的块数上限
     * @param until 绝对时刻（nowMs 的刻度）；到点就交还主线程，缺省表示不设限
     *
     * 这里只解析，上树交给 flushDue()：DOM 变更的代价由批的大小决定，不由
     * 趟数决定。
     */
    function parseSlice(cap, until)
    {
        let made = 0;
        const limit = cap > 0 ? cap : SLICE_BLOCKS;
        while (pending.length && made < limit) {
            const index = pending.shift();
            if (!nodes[index]) {
                const element = renderBlock(index);
                nodes[index] = element;
                batch.push({index: index, element: element});
                made += 1;
            }
            if (made < limit && until !== undefined && nowMs() >= until) {
                break;      // 时间片用完：让出主线程，滚动与输入先走
            }
        }
        flushDue();
    }

    /**
     * 让出一次事件循环，下一趟作为普通宏任务回来。
     *
     * 三条排期路线都量过，同一个文档、同一台机器（486 KB / 3150 块，Qt 6.7.3
     * 内嵌 Chromium 122），全部的实际工作量约 1.85 s：
     *
     *  - requestIdleCallback：平均 470 ms 才给一次（每帧剩余的时间被样式与
     *    布局吃掉，Blink 多数时候只在 240 ms 超时后放行），全篇 **105 s**；
     *  - requestAnimationFrame：把吞吐绑在帧率上，而挂载期间帧率只有 9~15 帧
     *    /秒（同一份 DOM 静止时能到 47 帧/秒），全篇 **16 s**；它还会在窗口
     *    不可见时被 Chromium 冻住，排期就此停摆；
     *  - MessageChannel 的 postMessage：普通宏任务，不受 setTimeout 嵌套五层
     *    之后 4 ms 的钳制，静止页面实测能给到 168 趟/秒。它不保证「一秒多少
     *    趟」：页面一旦有渲染活儿要干，任务照样排在渲染之后。但窗口不可见时
     *    它照样跑，一趟的边界也完全由我们定，它是这条路上唯一由我们说了算的
     *    一环。
     *
     * 结论：分片的对头不是「帧」，是「等排期」；空闲时间不可依赖，帧率由排版
     * 开销决定，只有「什么时候把主线程交出去」这件事是我们自己能定的。
     */
    function yieldToEventLoop(run)
    {
        if (typeof MessageChannel === "function") {
            const channel = new MessageChannel();
            channel.port1.onmessage = run;
            channel.port2.postMessage(0);
            return;
        }
        setTimeout(run, 0);       // 没有 MessageChannel 时的等价物
    }

    /**
     * 排下一趟：用掉 PASS_BUDGET_MS 就让出，直到 pending 清空。
     *
     * 趟与趟之间浏览器要跑一帧（样式、布局、绘制），那一帧的开销是固定的，与
     * 我们在一趟里干多少活无关。所以总时间 ≈ 实际工作量 + 趟数 × 一帧的开销：
     * 一趟越大总账越小（48 ms 相比 24 ms 把同一份文档从 6.9 s 拉到 5.6 s）。
     * 上限取 50 ms 以下，因为那是一条「长任务」的门槛——超过它，输入就开始
     * 丢帧，而挂载期间用户正是在滚动、在挑字。再快就得拿交互去换，不值。
     */
    function schedule()
    {
        if (scheduled || !pending.length) {
            return;
        }
        scheduled = true;
        yieldToEventLoop(function () {
            scheduled = false;
            passes += 1;
            if (pending.length) {
                parseSlice(SLICE_BLOCKS, nowMs() + PASS_BUDGET_MS);
                if (pending.length) {
                    schedule();
                    return;
                }
            }
            reportFrame();
        });
    }

    function reportFrame()
    {
        onFrame({
            parseMs: Math.round(parseMs),
            mountMs: Math.round(mountMs),
            passes: passes,
            blockCount: blocks.length,
            mountedCount: nodes.reduce((sum, node) => sum + (node ? 1 : 0), 0),
            pendingBlocks: pending.length,
            pendingDiagrams: diagram.pending(),
            totalMs: renderStartedAt ? Math.round(nowMs() - renderStartedAt) : 0
        });
    }

    // --- 结构维护 ----------------------------------------------------------

    function refreshAppendCursor()
    {
        for (let i = nodes.length - 1; i >= 0; i -= 1) {
            if (nodes[i]) {
                appendCursor = i + 1;
                return;
            }
        }
        appendCursor = 0;
    }

    // 插入/删除之后，块索引整体平移，标题块的锚点 id 要跟着改。只有标题块
    // 有 id，所以这一趟的 DOM 写入量正比于大纲条数而不是块数。
    function renumberAnchors(fromIndex)
    {
        for (let i = fromIndex; i < nodes.length; i += 1) {
            const element = nodes[i];
            if (!element) {
                continue;
            }
            const first = element.firstElementChild;
            if (first && HEADING.test(first.tagName)) {
                element.id = "b" + blocks[i].i;
            } else {
                element.removeAttribute("id");
            }
        }
    }

    function markEmpty()
    {
        const isEmpty = !blocks.length;
        if (emptyEl) {
            emptyEl.hidden = !isEmpty;
            if (isEmpty) {
                emptyEl.textContent = label("emptyDocument");
            }
        }
    }

    function resetPipeline()
    {
        pending = [];
        batch = [];
        scheduled = false;
        diagram.reset();
        finder.reset();
        if (codeObserver) {
            codeObserver.disconnect();
        }
    }

    return {
        /**
         * 整篇文档换掉（首次打开、从磁盘重载）。
         */
        setDocument(payload)
        {
            doc = payload;
            strings = payload.strings || {};
            blocks = (payload.blocks || []).slice();
            nodes = blocks.map(function () { return null; });
            resetPipeline();
            root.replaceChildren();
            appendCursor = 0;
            parseMs = 0;
            mountMs = 0;
            passes = 0;
            renderStartedAt = nowMs();
            md = createMarkdown({assetUrl: assetUrl});
            this.setVars(payload.vars);
            this.setView(payload.view);
            markEmpty();
            pending = blocks.map(function (_, index) { return index; });
            // 首屏这一趟立刻上树：先让人看到内容，后面才按批攒着。它给的
            // 时间比平时一趟多一点，因为这一趟直接决定白屏多久。
            parseSlice(SLICE_BLOCKS, nowMs() + FIRST_BUDGET_MS);
            flushBatch();
            schedule();
            return {blocks: blocks.length, rev: payload.rev};
        },

        /**
         * 增量替换：C++ 的 MarkdownChunker::diff 给出连续区间，这里删掉旧的
         * `<section>`、把新的插进同一位置。
         */
        updateBlocks(args)
        {
            if (!md) {
                // 还没 boot 完就不会有命令（QML 只在页面 ready 之后排空队列），
                // 但这个守卫值一行：少了它，顺序一旦变了就是整篇空白。
                return;
            }
            const startIndex = Math.max(0, args.startIndex | 0);
            const removeCount = Math.max(0, args.removeCount | 0);
            const inserted = args.blocks || [];
            const removeEnd = Math.min(startIndex + removeCount, nodes.length);
            for (let i = startIndex; i < removeEnd; i += 1) {
                if (nodes[i]) {
                    nodes[i].remove();
                    nodes[i] = null;
                }
            }
            blocks.splice(startIndex, removeEnd - startIndex, ...inserted);
            // 展开写入：直接把 inserted.map(...) 当第三个参数传给 splice，
            // 会把整个数组当作**一个**元素塞进 nodes，之后 nodes[i] 取到的是
            // 数组而不是元素，整页渲染从这一刻起全废。
            nodes.splice(startIndex, removeEnd - startIndex,
                         ...inserted.map(function () { return null; }));
            // 新块的 i 就是它在新序列里的全局下标（split 按位置赋的），这里
            // 只保证万一对不上时锚点仍然自洽。
            for (let i = 0; i < inserted.length; i += 1) {
                inserted[i].i = startIndex + i;
            }
            // 命中缓存的块（内容没变、只是整体平移）原样留着，不重解析
            const start = startIndex;
            const end = startIndex + inserted.length;
            for (let i = start; i < end; i += 1) {
                const existing = nodes[i];
                if (existing && existing.dataset.hash === blocks[i].h) {
                    continue;
                }
                if (existing) {
                    existing.remove();
                }
                nodes[i] = null;
                if (pending.indexOf(i) < 0) {
                    pending.push(i);
                }
            }
            pending.sort(function (a, b) { return a - b; });
            refreshAppendCursor();
            renumberAnchors(start);
            markEmpty();
            schedule();
            reportFrame();
        },

        /**
         * 视图参数：字号与栏宽只是两个 CSS 变量，**不触发重解析**——这是
         * 「调字号不该等一次排版」的实现处。
         */
        setView(args)
        {
            const view = args || {};
            const size = view.fontSizePx | 0;
            const width = view.contentWidthPx | 0;
            const style = document.documentElement.style;
            if (size > 0) {
                style.setProperty("--awb-doc-font-size", size + "px");
            } else {
                style.removeProperty("--awb-doc-font-size");
            }
            if (width > 0) {
                style.setProperty("--awb-doc-width", width + "px");
            } else {
                style.removeProperty("--awb-doc-width");
            }
        },

        setVars(next)
        {
            vars = next || {};
            const style = document.documentElement.style;
            Object.keys(vars).forEach((key) => {
                if (key.indexOf("--") === 0) {
                    style.setProperty(key, vars[key]);
                }
            });
            // KaTeX 的语法错误标红色取自主题的 danger：渲染时逐个问
            // getComputedStyle 太贵（公式密集的文档一处一问），所以在这里刷一次。
            configureMath({errorColor: vars["--awb-danger"] || ""});
        },

        // 主题变体变了：已渲染的图表按新变体重画
        variantChanged()
        {
            diagram.retheme();
        },

        setStylesheet(text)
        {
            stylesheet = text || "";
        },

        scrollTo(args)
        {
            let index = args.blockIndex;
            if (typeof index !== "number" || index < 0) {
                const match = /^b(\d+)$/.exec(String(args.anchor || ""));
                index = match ? Number(match[1]) : -1;
            }
            if (index < 0 || index >= nodes.length) {
                return false;
            }
            // 大纲跳转不该等分片解析：把这一块的解析插队到下一帧之前
            if (!nodes[index]) {
                const at = pending.indexOf(index);
                if (at >= 0) {
                    pending.splice(at, 1);
                }
                const element = renderBlock(index);
                nodes[index] = element;
                batch.push({index: index, element: element});
            }
            flushBatch();
            const target = nodes[index];
            if (!target) {
                return false;
            }
            target.scrollIntoView({block: "start", behavior: "auto"});
            return true;
        },

        find(args)
        {
            return finder.find(args || {}, nodes.filter(Boolean));
        },

        /**
         * 导出/外部打开前把没渲染的全补上：屏外块被 content-visibility 跳过、
         * 屏外图表停在占位态，直接序列化得到的是一堆占位符。
         *
         * 解析在这里同步做完（这是用户主动触发的动作，慢一下换「导出件完整」
         * 是划算的）；图表仍是异步，完成时经由 onIdle 再报一次 frame。
         */
        renderAll()
        {
            while (pending.length) {
                parseSlice(pending.length);
            }
            flushBatch();
            diagram.renderAll();
            reportFrame();
        },

        // 是否还能报「渲染完了」：QML 侧的导出等待就判这个
        isIdle()
        {
            return {
                pendingBlocks: pending.length,
                pendingDiagrams: diagram.pending(),
                blocks: blocks.length
            };
        },

        /**
         * 自包含 HTML：导出与「用外部浏览器打开」共用的一份产物。
         *
         * 三条离线约定：CSS 内联（并把当前主题变量写在最前）、图片改回相对
         * 引用（导出到文档同目录时最准确）、公式只留 MathML 分支——HTML 分支
         * 要对齐就得带上 KaTeX 的字体，那是几百 KB 的 base64，而现代浏览器
         * 原生就能排 MathML。
         */
        serialize()
        {
            flushBatch();
            // 先搬进 <template> 的惰性文档，再动里面的节点。两个理由：
            // 一是 cloneNode 出来的子树仍算「属于本文档」，在里面改 img 的 src
            // 会让 Chromium 真的按当前页去取那张图——每次导出凭空发一轮 404；
            // template.content 那份文档没有 frame，改它没有任何网络动作。
            // 二是跨文档插入会抛 WrongDocumentError，所以新建的节点一律取自
            // 搬进去之后的 ownerDocument。
            const carrier = document.createElement("template");
            const clone = root.cloneNode(true);
            carrier.content.appendChild(clone);
            const inert = clone.ownerDocument;
            clone.querySelectorAll(".awb-mark").forEach((mark) => {
                const parent = mark.parentNode;
                if (parent) {
                    parent.replaceChild(
                        inert.createTextNode(mark.textContent), mark);
                }
            });
            clone.querySelectorAll(".katex-html").forEach((node) => {
                node.remove();
            });
            // 没画成的图表在导出件里回到「能读的源码」，而不是一句点击提示
            clone.querySelectorAll(".awb-diagram-hint").forEach((node) => {
                node.remove();
            });
            const base = doc && doc.assetBase ? doc.assetBase : "";
            if (base) {
                clone.querySelectorAll("img[src]").forEach((img) => {
                    const src = img.getAttribute("src") || "";
                    if (src.indexOf(base) === 0) {
                        img.setAttribute("src", src.slice(base.length));
                    }
                });
            }
            const css = Object.keys(vars).map((key) => {
                return key.indexOf("--") === 0 ? key + ":" + vars[key] : "";
            }).filter(Boolean).join(";");
            const title = doc && doc.title ? doc.title : "Markdown";
            // 语言标签取引擎的界面语言（navigator.language，Qt WebEngine 里
            // 跟随应用 locale），不是写死的 "en"：导出件里的中文文档被标成
            // en 时，外部浏览器会挑错字体、读屏也会读错。取不到才退回 en。
            const lang = (globalThis.navigator && globalThis.navigator.language)
                || "en";
            const head = [
                "<!doctype html>",
                '<html lang="' + escapeHtml(lang) + '" class="awb-export">',
                "<head>",
                '<meta charset="utf-8">',
                '<meta name="viewport" content="width=device-width,',
                ' initial-scale=1">',
                "<title>" + escapeHtml(title) + "</title>",
                "<style>"
            ].join("\n");
            return head + "\n:root{" + css + "}\n" + stylesheet + "\n"
                + "</style>\n</head>\n<body>\n" + clone.outerHTML
                + "\n</body>\n</html>\n";
        },

        stats()
        {
            return {
                rev: doc ? doc.rev : 0,
                blocks: blocks.length,
                mounted: nodes.reduce((sum, node) => sum + (node ? 1 : 0), 0),
                pending: pending.length,
                diagrams: diagram.pending(),
                bytes: doc ? doc.bytes : 0
            };
        }
    };
}
