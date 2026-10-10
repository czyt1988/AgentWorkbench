// markdown-it 实例的唯一构造处：解析配置与自定义渲染规则都在这里。
//
// 为什么要单独成文件：预览页（renderer.js）与切分保真性的 spike
// （scripts/spike-markdown-chunker.js）必须用**同一个**配置。整篇渲染与逐块
// 渲染是否等价是这套管线的正确性地基，而两边配置不一致时比的是两个不同的
// 解析器，那份 spike 结论就没有意义。
//
// preset 用 default 而不是 commonmark：commonmark preset 里 linkify 这条 core
// 规则根本没装（options.linkify 于是空转，`http://x.y` 保持纯文本），而
// default = CommonMark + 表格 + 删除线 + 自动链接，正是我们要的那一档。
// html:false 是硬要求——预览别人的 Markdown 时内嵌 HTML 就是脚本注入面，
// 且这条通道上没有任何 CSP 兜着。

import { installAlerts, installMath, installTaskCheckbox } from "./math-rule.js";

const MERMAID_LANGS = ["mermaid"];

const UNDERLINE_OPEN = "<u>";
const UNDERLINE_CLOSE = "</u>";

/**
 * 下划线窄白名单：html:false 关掉了所有内联 HTML 的解析，但 <u> 是
 * markdown 里唯一可移植的下划线记号（编辑器的下划线动作写它）。这条
 * 规则装在 text 之前、只认 "<u>" 与 "</u>" 两个精确串，且**要求同段内
 * 配对**：开标记要在本段找到闭标记才放行，反之亦然——打字中间态（只
 * 输入了 "<u>"）与落单闭标记都照旧走 text 规则转义，预览不会把段落
 * 剩余内容整体拖进下划线。命中时手工构造 html_inline token——内容是
 * 常量、不经任何输入拼接，renderer 对该 token 原样输出；其余 HTML 的
 * 注入面没有扩大。
 */
function installUnderline(md)
{
    md.inline.ruler.before("text", "awb_underline", function (state, silent) {
        const rest = state.src.slice(state.pos, state.posMax);
        if (rest.startsWith(UNDERLINE_OPEN)) {
            const closeAt = state.src.indexOf(UNDERLINE_CLOSE,
                                             state.pos + UNDERLINE_OPEN.length);
            if (closeAt < 0 || closeAt >= state.posMax) {
                return false;
            }
            if (!silent) {
                state.push("html_inline", "", 0).content = UNDERLINE_OPEN;
            }
            state.pos += UNDERLINE_OPEN.length;
            return true;
        }
        if (rest.startsWith(UNDERLINE_CLOSE)) {
            const openAt = state.src.lastIndexOf(UNDERLINE_OPEN, state.pos - 1);
            if (openAt < 0) {
                return false;
            }
            if (!silent) {
                state.push("html_inline", "", 0).content = UNDERLINE_CLOSE;
            }
            state.pos += UNDERLINE_CLOSE.length;
            return true;
        }
        return false;
    });
}

function escapeHtml(value)
{
    return String(value)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;");
}

/**
 * 围栏是不是图表：只看 info 的第一个词（`mermaid title=x` 也算）。
 * renderer.js 与 diagram.js 都按它判断，两处判断不一致时图表块会既不上屏
 * 也不进渲染队列——表现是「图不见了」。
 */
export function isDiagramFence(info)
{
    const lang = String(info || "").trim().split(/\s+/)[0].toLowerCase();
    return MERMAID_LANGS.indexOf(lang) >= 0;
}

// 图表占位：结构与 diagram.js 的期待一致——data-status 记状态，源码留在
// 内层 <pre> 里，渲染成功后整块内容被 SVG 取代。这里不写任何面向用户的
// 文案：文案一律来自 doc.json 的 strings（JS 侧没有可翻译源串）。
function diagramPlaceholder(code)
{
    return '<div class="awb-diagram" data-status="queued">'
        + '<pre class="awb-diagram-source">' + escapeHtml(code) + "</pre>"
        + "</div>";
}

/**
 * 代码围栏自己渲染，而不是沿用 markdown-it 的默认输出。
 *
 * 默认输出只有 `<pre><code class="language-x">`，缺两样页面要用的东西：
 * 语言标签（长代码块里用户会问「这是哪种语言」）与「高亮延后」的钩子
 * （renderer.js 只挑 data-awb-hl 为 1 的元素排队）。
 */
function codeFence(code, info)
{
    const lang = String(info || "").trim().split(/\s+/)[0].toLowerCase();
    const label = lang
        ? '<span class="awb-code-lang">' + escapeHtml(lang) + "</span>"
        : "";
    // 没有标语言就不交给 hljs：自动判定既慢又常常判错，把用户本来对齐的
    // 纯文本染上一堆无关的颜色。
    const cls = lang ? ' class="language-' + escapeHtml(lang) + '"' : "";
    const hl = lang ? ' data-awb-hl="1"' : "";
    return '<div class="awb-code">' + label + "<pre><code" + cls + hl + ">"
        + escapeHtml(code) + "</code></pre></div>";
}

/**
 * 把文档相对路径归一成「能交给 awbmd://runtime/asset/ 的形状」。
 *
 * 只做 `.` 与 `../` 的折叠，不做别的：C++ 侧的 scheme handler 只接受文档目录
 * 之内的相对路径（`..` 一律拒绝），所以一篇引用了上一级目录图片的 README
 * 在这里注定少一张图——把越界的部分原样交给 handler、让它回 404，比在页面里
 * 猜一个「大概是这个位置」更诚实：用户看到的是图裂与 alt 文本，而不是另一张
 * 不相干的图。
 */
function normalizeRelative(relPath)
{
    const trimmed = String(relPath).trim().replace(/\\/g, "/");
    const leadingSlash = trimmed.startsWith("/");
    const segments = trimmed.replace(/^\/+/, "").split("/");
    const out = [];
    for (const segment of segments) {
        if (!segment || segment === ".") {
            continue;
        }
        if (segment === "..") {
            if (out.length) {
                out.pop();
            } else {
                // 越界：留在结果里，让 handler 明确拒绝它
                out.push(segment);
            }
            continue;
        }
        out.push(segment);
    }
    return (leadingSlash ? "/" : "") + out.join("/");
}

function encodeSegments(relPath)
{
    return relPath.split("/").map((segment) => {
        return encodeURIComponent(segment);
    }).join("/");
}

/**
 * @param {object} options
 *   markdownit  markdown-it 工厂；缺省取全局（浏览器里 UMD 挂在 window 上，
 *               Node 侧由调用方 require 后传进来——同一个 UMD 文件两种用法）
 *   assetUrl    (相对路径) => 绝对 URL；不给则图片 src 原样输出
 * @return 装好公式/任务项/告警规则与 fence/image 渲染器的实例
 */
export function createMarkdown(options)
{
    const opts = options || {};
    const factory = opts.markdownit || globalThis.markdownit;
    if (typeof factory !== "function") {
        throw new Error("markdown-it is not available");
    }
    const md = factory("default", {
        html: false,
        breaks: false,
        langPrefix: "language-",
        linkify: true,
        typographer: false
    });

    // 公式、任务项、告警块三个规则集都在 math-rule.js：它们都是「往
    // CommonMark 管线里插规则」的同类改动，顺序敏感，所以一起装。
    installMath(md);
    installTaskCheckbox(md);
    installAlerts(md);
    // 下划线窄白名单（见函数头注释）：必须在 text 之前接管精确串。
    installUnderline(md);

    // 标题**不**生成 slug id：`[章节](#某标题)` 这类站内链接点下去不会动。
    // 原因是逐块重渲染：slug 的去重计数得跨块维护，改一次文就漂一次，
    // 而大纲跳转本来走的是 C++ 算好的块锚点（b<index>）。代价小、行为可解释。
    md.renderer.rules.fence = function (tokens, idx) {
        const token = tokens[idx];
        if (isDiagramFence(token.info)) {
            return diagramPlaceholder(token.content);
        }
        return codeFence(token.content, token.info);
    };

    // 文档相对路径的图片换成本机 scheme 上的地址；带协议的（http/data）与
    // 纯锚点一律不动——离线渲染时外链本来就不该被加载。
    if (typeof opts.assetUrl === "function") {
        md.renderer.rules.image = function (tokens, idx, options, env, self) {
            const token = tokens[idx];
            const at = token.attrIndex("src");
            if (at >= 0) {
                const mapped = opts.assetUrl(token.attrs[at][1]);
                if (mapped) {
                    token.attrs[at][1] = mapped;
                }
            }
            return self.renderToken(tokens, idx, options);
        };
    }

    return md;
}

export { escapeHtml, normalizeRelative, encodeSegments };

