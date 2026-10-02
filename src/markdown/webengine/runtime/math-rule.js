// markdown-it 的自定义规则集：公式（KaTeX）、任务项复选框、告警块。
//
// 单独成一个文件而不是散在 renderer.js 里，是因为这三样都是「往 CommonMark
// 的解析管线里插规则」的同类改动，改动形态一致、出问题的形态也一致（规则
// 顺序错会让 `$$` 吞掉后面的段落，或让任务项在嵌套列表里失效）。
//
// 页面上的可翻译文案一律不在这里：规则产出的都是结构（class、data 属性），
// 面向用户的字面量由 C++ 的 MarkdownRuntime::pageStrings() 随 doc.json 下发。
// 本文件里出现的英文只出现在 class 名与 data 属性值里。

function escapeHtml(value)
{
    return String(value)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;");
}

// 主题里的危险色，用于 KaTeX 自己标红的语法错误。取的是 :root 上的 CSS
// 变量——渲染时逐个问 getComputedStyle 太贵（一段公式密集的文档有上千处），
// 所以由 renderer.js 在 setTheme 时调 configure() 刷新一次。
let errorColor = "#e06c75";

export function configure(options)
{
    if (options && options.errorColor) {
        errorColor = options.errorColor;
    }
}

function renderMath(token, displayMode)
{
    // 取 globalThis 而不是 window：浏览器里两者同一个对象，但 spike 脚本在
    // Node 里跑同一份规则，那边只有 globalThis——写 window 就等于要求测试
    // 先伪造一个假 window。
    const katex = globalThis.katex;
    if (!katex || typeof katex.renderToString !== "function") {
        // KaTeX 没供出来（资源缺项或 MIME 不对）。正文不能因此空白：
        // 原样留下公式文本并标红，用户看得见「这里本该是个公式」。
        return '<span class="awb-math-error">' + escapeHtml(token.content)
            + "</span>";
    }
    let html;
    try {
        // output 用 htmlAndMathml：HTML 分支保证可读与可选，MathML 分支在
        // 字体缺失时仍是可读的公式——导出的独立 HTML 正是那个场景。
        html = katex.renderToString(token.content, {
            displayMode: displayMode,
            output: "htmlAndMathml",
            throwOnError: false,
            strict: false,
            errorColor: errorColor
        });
    } catch (error) {
        return '<span class="awb-math-error">' + escapeHtml(token.content)
            + "</span>";
    }
    if (!displayMode) {
        return html;
    }
    // 行间公式套一层滚动容器：katex-display 里的长公式是 nowrap 的，不包
    // 就会把整页撑出横向滚动条，而一行公式撑破版面的代价是「整篇读不动」。
    return '<div class="awb-math-block">' + html + "</div>";
}

/**
 * 公式规则：块级 `$$ … $$` 与行内 `$ … $`，都走 KaTeX 的同步 renderToString。
 *
 * 选同步而不是 MathJax 的异步排版，是因为渲染管线按块增量更新：一次编辑只
 * 重排改动的块，异步排版会带来「块先空着、几百毫秒后自己长高」的抖动。
 */
export function installMath(md)
{
    md.block.ruler.before("fence", "math_block", function (state, startLine,
                                                            endLine, silent) {
        const open = state.src.slice(state.bMarks[startLine]
                                     + state.tShift[startLine],
                                     state.eMarks[startLine]);
        if (!/^\s*\$\$/.test(open)) {
            return false;
        }
        if (silent) {
            // 静默探测只问「这一行是不是本规则的开头」，不扫闭合行。
            return true;
        }

        const oneLine = /^\s*\$\$(.+?)\$\$\s*$/.exec(open);
        if (oneLine) {
            const token = state.push("math_block", "math", 0);
            token.block = true;
            token.content = oneLine[1].trim();
            token.map = [startLine, startLine + 1];
            state.line = startLine + 1;
            return true;
        }

        const body = [];
        let line = startLine + 1;
        let closed = false;
        while (line < endLine) {
            const text = state.src.slice(state.bMarks[line] + state.tShift[line],
                                         state.eMarks[line]);
            if (/^\s*\$\$\s*$/.test(text)) {
                closed = true;
                line += 1;
                break;
            }
            body.push(text);
            line += 1;
        }
        if (!closed) {
            // 没有闭合的 `$$`：当普通段落处理。吞掉后半篇是最坏的失败方式，
            // 宁可这段公式不渲染成 MathML。
            return false;
        }

        const token = state.push("math_block", "math", 0);
        token.block = true;
        token.content = body.join("\n").trim();
        token.map = [startLine, line];
        state.line = line;
        return true;
    }, {alternate: ["paragraph", "reference", "blockquote", "list"]});

    // 行内公式：开 `$` 后不接空白、闭 `$` 前不接空白，且结束符后不接数字——
    // 最后这条挡掉「价格 $5 和 $10」这类最常见的误伤：不挡的话正文里的美元
    // 金额会被当成公式送去 KaTeX，用户看到一排红字。
    md.inline.ruler.after("escape", "math_inline", function (state, silent) {
        if (state.src[state.pos] !== "$") {
            return false;
        }
        const match = /^\$(?![\s$])((?:\\.|[^$])+?(?<![\s$]))\$(?!\d)/
            .exec(state.src.slice(state.pos));
        if (!match) {
            return false;
        }
        if (!silent) {
            state.push("math_inline", "math", 0).content = match[1];
        }
        state.pos += match[0].length;
        return true;
    });

    md.renderer.rules.math_block = function (tokens, idx) {
        return renderMath(tokens[idx], true);
    };
    md.renderer.rules.math_inline = function (tokens, idx) {
        return renderMath(tokens[idx], false);
    };
}

/**
 * 任务项复选框：GFM 的 `- [ ]` / `- [x]`。
 *
 * markdown-it 的核心不带它（严格 CommonMark），也不额外引插件——一个插件
 * 就是一份要长期跟版本的上游。做法是 core 规则在 inline 之后改写 tokens：
 * 列表项的第一个 inline 内容若以方括号标记开头，就在前面插一个 checkbox
 * 标记、并把标记从正文里去掉，同时给该 list_item 挂上样式类。
 */
export function installTaskCheckbox(md)
{
    md.renderer.rules.checkbox = function (tokens, idx) {
        const checked = tokens[idx].attrGet("data-checked") === "1";
        return '<span class="awb-task-box" data-checked="'
            + (checked ? "1" : "0") + '" aria-hidden="true"></span>';
    };

    md.core.ruler.after("inline", "task_checkbox", function (state) {
        const tokens = state.tokens;
        let openIndex = -1;

        for (let i = 0; i < tokens.length; i += 1) {
            const token = tokens[i];
            if (token.type === "list_item_open") {
                openIndex = i;
                continue;
            }
            if (openIndex < 0 || token.type !== "inline") {
                // 列表项开头不是 inline 的情况（嵌套列表、围栏代码）由下面的
                // openIndex 复位处理；这里只关心「本项的第一段正文」。
                if (token.type === "list_item_close") {
                    openIndex = -1;
                }
                continue;
            }

            const children = token.children;
            const first = children && children.length ? children[0] : null;
            openIndex = -1;
            if (!first || first.type !== "text") {
                continue;
            }
            const match = /^\[([ xX])\]\s?/.exec(first.content);
            if (!match) {
                continue;
            }
            const box = new state.Token("checkbox", "span", 0);
            box.attrSet("data-checked", match[1].toLowerCase() === "x" ? "1" : "0");
            first.content = first.content.slice(match[0].length);
            if (!first.content) {
                children.shift();
            }
            children.unshift(box);
            // 样式类挂在 list_item 上：松列表里 inline 前面还夹着 paragraph_open，
            // 所以按当前 token 往前找最近的那个 list_item_open。
            for (let j = i; j >= 0; j -= 1) {
                if (tokens[j].type === "list_item_open") {
                    tokens[j].attrJoin("class", "awb-task");
                    break;
                }
            }
        }
        return true;
    });
}

// 键带着方括号里的感叹号：下面的正则只捕获 `!NOTE` 这一段（方括号留在捕获组
// 外面），键若是 `[!NOTE]` 就永远查不中——告警块会安静地退化成普通引用。
const ALERT_KINDS = {
    "!NOTE": "note",
    "!TIP": "tip",
    "!IMPORTANT": "important",
    "!WARNING": "warning",
    "!CAUTION": "caution"
};

/**
 * 告警块：GitHub 风格的 `> [!NOTE] 正文`。
 *
 * 只做两件事：给引用块挂上语义色类、把 `[!NOTE]` 这个标记从正文里去掉。**不**
 * 替作者生成「Note」这类标题——上游那几个词是英文专名，页面侧没有对应的可翻译
 * 源串（文案一律来自 C++ 的 pageStrings()），凭空造一个只会让译文用户看到一个
 * 没人翻译过的单词。作者自己写了标题行（`> [!NOTE] 重要提示`）就照原样排版。
 */
export function installAlerts(md)
{
    md.core.ruler.after("task_checkbox", "alerts", function (state) {
        const tokens = state.tokens;
        for (let i = 0; i + 2 < tokens.length; i += 1) {
            if (tokens[i].type !== "blockquote_open") {
                continue;
            }
            const inlineIndex = tokens[i + 1].type === "paragraph_open" ? i + 2 : i + 1;
            const inline = tokens[inlineIndex];
            if (!inline || inline.type !== "inline") {
                continue;
            }
            // `[\s\S]*` 而不是 `.*`：引用块的多行正文里 inline.content 含换行，
            // 用 `.*$` 的话只有单行告警能被认出来。
            const marker = /^\[(![A-Z]+)\][\s\S]*$/.exec(inline.content);
            const kind = marker && ALERT_KINDS[marker[1]];
            if (!kind) {
                continue;
            }
            tokens[i].attrJoin("class", "awb-alert awb-alert-" + kind);
            const first = inline.children && inline.children.length
                ? inline.children[0] : null;
            if (first && first.type === "text") {
                // 只从第一个文本段里摘掉标记：后面的 token（强调、链接）属于
                // 已经解析好的内容，整体替换 inline.content 会把正文重复一遍。
                first.content = first.content.replace(
                    /^\[![A-Z]+\][ \t]*/, "");
                if (!first.content) {
                    inline.children.shift();
                }
            }
        }
        return true;
    });
}
