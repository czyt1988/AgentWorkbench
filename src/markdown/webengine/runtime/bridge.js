// 页面的入口与总装：启动、命令分发、以及页面侧唯一对外暴露的对象 AWB。
//
// AWB 是 QML 与这个页面之间全部的接触面。QML 只做两件事：
//   view.runJavaScript("AWB.dispatch(" + 命令JSON + ")")  推
//   view.runJavaScript("AWB.某()", function(r){ ... })     拉
// 所以**大结果一律从 dispatch 的返回值走**（runJavaScript 的回调没有长度限制），
// 信标只送小事件与小计数。Qt 的 runJavaScript 会不会等待 Promise 因版本而异，
// 这里不赌：需要回数据时返回值是同步算好的字符串。
//
// 页面里没有任何可翻译源串：文案随 doc.json 的 strings 下发（见 MarkdownRuntime
// 的 pageStrings()），因为 .js 文件对 lupdate 与门禁都不可见。

import { createRenderer } from "./renderer.js";

// 页面构建标识：进 ready 信标与「关于」，用来把「用户看到的行为」对回某一份
// 随包的 runtime/*.js——只有 markdown-it / KaTeX 的版本号时排查不了页面逻辑。
const PAGE_REVISION = "awbmd-page-1";

const root = document.getElementById("awb-doc");
const emptyEl = document.getElementById("awb-empty");

let doc = null;
let renderer = null;
let bootedAt = 0;
let readyReported = false;
let lastVariant = "";

function beacon(kind, payload)
{
    const url = "/event?k=" + encodeURIComponent(kind)
        + "&p=" + encodeURIComponent(JSON.stringify(payload || {}));
    if (typeof fetch !== "function") {
        return;
    }
    // no-store 不是洁癖：信标 URL 只在 query 里变，命中缓存的那一次上报会
    // 直接丢进 Chromium 的内存缓存，C++ 侧永远等不到这个事件。
    fetch(url, {cache: "no-store"}).catch(function () { /* 上报失败不值得再报 */ });
}

function documentUrl(rev)
{
    return "/doc.json?rev=" + encodeURIComponent(String(rev == null ? 0 : rev));
}

function fetchJson(url)
{
    return fetch(url, {cache: "no-store"}).then((response) => {
        if (!response.ok) {
            throw new Error("fetch failed: " + url + " (" + response.status + ")");
        }
        return response.json();
    });
}

// 重新取整篇（setDocument 命令只带 rev，正文与块都在 doc.json 里）
function reload(rev)
{
    return fetchJson(documentUrl(rev)).then((payload) => {
        doc = payload;
        renderer.setDocument(payload);
        return payload;
    }).catch((error) => {
        beacon("error", {blockIndex: -1, message: String(error && error.message)});
        return null;
    });
}

function applyView(args)
{
    renderer.setView(args);
}

function applyTheme(args)
{
    renderer.setVars(args.vars);
    // 变体（light/dark）变了才要重画图表：Mermaid 的 SVG 是按主题画死的，
    // 不换变体时重画纯属浪费。变体记在这儿而不是问 renderer，因为 CSS 变量
    // 里那份 --awb-variant 是页面样式用的，语义上不同源。
    const variant = args.variant || "";
    if (variant && variant !== lastVariant) {
        lastVariant = variant;
        renderer.variantChanged();
    }
}

function dispatch(message)
{
    const payload = typeof message === "string" ? JSON.parse(message) : message;
    const cmd = payload && payload.cmd;
    const args = (payload && payload.args) || {};
    switch (cmd) {
    case "setDocument":
        reload(args.rev);
        return true;
    case "updateBlocks":
        renderer.updateBlocks(args);
        return true;
    case "setView":
        applyView(args);
        return true;
    case "setTheme":
        applyTheme(args);
        return true;
    case "scrollTo":
        return renderer.scrollTo(args);
    case "find": {
        // 命中数与当前序号经信标回 C++（小结果，且 UI 要在搜索框里显示）
        const result = renderer.find(args);
        beacon("find", result);
        return result;
    }
    case "renderAll":
        renderer.renderAll();
        return true;
    case "serialize":
        // 返回值即导出件：QML 把 runJavaScript 的返回值交给
        // Markdown.reportMarkup()，所以这条路不受信标 8 KB 上限的约束
        return renderer.serialize();
    default:
        beacon("error", {blockIndex: -1,
                         message: "unknown command: " + String(cmd)});
        return false;
    }
}

function boot()
{
    bootedAt = (globalThis.performance && globalThis.performance.now)
        ? globalThis.performance.now() : 0;
    renderer = createRenderer({
        root: root,
        emptyEl: emptyEl,
        onError: function (info) { beacon("error", info); },
        onFrame: function (frame) { beacon("frame", frame); }
    });

    // 导出要内联的样式表：boot 时取一次即可（随包内容不会变）
    fetch("/app/theme.css", {cache: "no-store"})
        .then((response) => response.text())
        .then((text) => renderer.setStylesheet(text))
        .catch(function () { /* 导出件少一层样式，不是打不开页面的理由 */ });

    return fetchJson(documentUrl(0)).then((payload) => {
        doc = payload;
        renderer.setDocument(payload);
        if (!readyReported) {
            readyReported = true;
            const painted = (globalThis.performance && globalThis.performance.now)
                ? Math.round(globalThis.performance.now() - bootedAt) : 0;
            beacon("ready", {
                page: PAGE_REVISION,
                rev: payload.rev,
                blocks: (payload.blocks || []).length,
                firstPaintMs: painted,
                // 能力位与 C++ 侧的判定对齐：content-visibility 不在这里就等于
                // 大文档的滚动优化没生效，日志里要看得见
                contentVisibility: typeof CSS !== "undefined"
                    && CSS.supports
                    && CSS.supports("content-visibility", "auto"),
                fetchApi: typeof fetch === "function"
            });
        }
        return payload;
    }).catch((error) => {
        beacon("error", {blockIndex: -1,
                         message: "boot failed: " + String(error && error.message)});
        return null;
    });
}

// 页面自己报错的唯一出口：没有这条，脚本级异常只会消失在 Chromium 的
// 控制台里，而预览窗口的用户看不到控制台。
globalThis.addEventListener("error", (event) => {
    beacon("error", {blockIndex: -1,
                     message: String(event && event.message),
                     at: String(event && event.filename)});
});
globalThis.addEventListener("unhandledrejection", (event) => {
    const reason = event && event.reason;
    beacon("error", {blockIndex: -1,
                     message: "rejected: " + String(reason && reason.message
                                                          || reason)});
});

globalThis.AWB = {
    revision: PAGE_REVISION,
    dispatch: dispatch,
    // 以下是拉取口：QML 与测试都用它们把结果读出来，不需要往页面里塞测试钩子
    stats: function () { return renderer ? renderer.stats() : {blocks: 0}; },
    idle: function () { return renderer ? renderer.isIdle() : {}; },
    capabilities: function () {
        return {
            page: PAGE_REVISION,
            fetchApi: typeof fetch === "function",
            contentVisibility: typeof CSS !== "undefined" && !!CSS.supports
                && CSS.supports("content-visibility", "auto"),
            markdownIt: typeof globalThis.markdownit === "function",
            katex: !!(globalThis.katex
                      && typeof globalThis.katex.renderToString === "function"),
            hljs: typeof globalThis.hljs !== "undefined"
        };
    },
    docMeta: function () {
        return doc ? {rev: doc.rev, title: doc.title, readOnly: !!doc.readOnly,
                      bytes: doc.bytes} : {};
    }
};

boot();
