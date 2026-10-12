// 页面的入口与总装：启动、命令分发、以及页面侧唯一对外暴露的对象 AWB。
//
// AWB 是 QML 与这个页面之间全部的接触面。QML 只做两件事：
//   view.runJavaScript("AWB.dispatch(" + 命令JSON + ")")  推
//   view.runJavaScript("AWB.某()", function(r){ ... })     拉
// 所以**大结果一律从 dispatch 的返回值走**（runJavaScript 的回调没有长度限制），
// 信标只送小事件与小计数。Qt 的 runJavaScript 会不会等待 Promise 因版本而异，
// 这里不赌：需要回数据时返回值是同步算好的字符串。
//
// 数据通道按引擎能力二选一（FetchApiAllowed 的 scheme 才有 fetch）：
//   fetch 可用（Qt ≥ 6.6）——页面自己 fetch doc.json / theme.css，信标走
//     /event 像素请求；
//   fetch 不可用（Qt 5 / Qt 6.2–6.5——fetch 对自定义 scheme 一律拒绝）——
//     **推入车道**：QML 在页面装载后经 runJavaScript 调 AWB.push，把文档
//     与样式表文本推进页面；信标改走 console 打点（AWBEVT: 前缀，C++ 侧
//     javaScriptConsoleMessage 解析——javaScriptConsoleMessage 一旦连接，
//     console 就是页面向 C++ 说话的现成通道，不必另开一套）。
// 车道由页面自探（boot 试一次 fetch）：定格后 AWB.channel() 同步可读，
// QML 轮询它、读到 "push" 才开始投递——runJavaScript 的回调在旧引擎上
// 等不到 Promise，车道结论不能靠回调带回。
// 页面里没有任何可翻译源串：文案随 doc.json 的 strings 下发（见 MarkdownRuntime
// 的 pageStrings()），因为 .js 文件对 lupdate 与门禁都不可见。

import { createRenderer } from "./renderer.js";

// 页面构建标识：进 ready 信标与「关于」，用来把「用户看到的行为」对回某一份
// 随包的 runtime/*.js——只有 markdown-it / KaTeX 的版本号时排查不了页面逻辑。
const PAGE_REVISION = "awbmd-page-2";

const root = document.getElementById("awb-doc");
const emptyEl = document.getElementById("awb-empty");

let doc = null;
let renderer = null;
let bootedAt = 0;
let readyReported = false;
let lastVariant = "";

// 推入通道的缓存（fetch 不可用的引擎）：QML 在页面装载完成后立即推入
// doc.json 与 theme.css，reload 时按 rev 重推 doc。
const pushed = {
    document: "",
    stylesheet: ""
};

// 车道名："" = 还在探，"fetch" = 页面自己拉，"push" = 等 QML 推。
// runJavaScript 的回调等不到 Promise（旧引擎），车道结论由页面自己定格、
// QML 经 AWB.channel() 轮询读取。
let channel = "";

function beacon(kind, payload)
{
    if (channel !== "fetch") {
        // console 信标（推入车道与未定格期）：C++ 侧 javaScriptConsoleMessage
        // 已接管转发，warn 级别保证不被默认日志级别滤掉。
        const line = "AWBEVT:" + kind + ":"
            + JSON.stringify(payload || {});
        console.warn(line);
        return;
    }
    const url = "/event?k=" + encodeURIComponent(kind)
        + "&p=" + encodeURIComponent(JSON.stringify(payload || {}));
    // no-store 不是洁癖：信标 URL 只在 query 里变，命中缓存的那一次上报会
    // 直接丢进 Chromium 的内存缓存，C++ 侧永远等不到这个事件。
    fetch(url, {cache: "no-store"}).catch(function () { /* 上报失败不值得再报 */ });
}

function documentUrl(rev)
{
    return "/doc.json?rev=" + encodeURIComponent(String(rev == null ? 0 : rev));
}

// 同步取当前文档载荷（推入车道）；返回文本，形状不对返回 null。
function readDocumentText()
{
    const text = pushed.document;
    if (!text) {
        return null;
    }
    try {
        return text;
    } catch (e) {
        return null;
    }
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

// 重新取整篇（setDocument 命令只带 rev，正文与块都在 doc.json 里）。
// fetch 车道自己拉；推入车道解析缓存（QML 在 setDocument 之前已把新载荷
// 推进来——pane 的 dispatch 收口负责这一步）。车道还没定格且缓存为空时
// 静默让过：那份 setDocument 来得比车道结论早，boot 会带着文档补上。
function reload(rev)
{
    if (channel !== "fetch" || pushed.document) {
        if (!pushed.document) {
            return Promise.resolve(null);
        }
        return parseDocument(pushed.document).then((payload) => {
            if (payload) {
                doc = payload;
                renderer.setDocument(payload);
            }
            return payload;
        }).catch((error) => {
            beacon("error", {blockIndex: -1,
                             message: String(error && error.message)});
            return null;
        });
    }
    return fetchJson(documentUrl(rev)).then((payload) => {
        doc = payload;
        renderer.setDocument(payload);
        return payload;
    }).catch((error) => {
        beacon("error", {blockIndex: -1, message: String(error && error.message)});
        return null;
    });
}

function parseDocument(text)
{
    if (!text) {
        return Promise.reject(new Error("no document pushed yet"));
    }
    return Promise.resolve().then(() => JSON.parse(text));
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

    if (typeof fetch !== "function") {
        // 引擎连 fetch API 都没有（不可能的兜底）：定格推入车道。
        channel = "push";
        return;
    }
    // 车道探测 = 直接试着取 doc.json。FetchApiAllowed 开着的引擎
    // （Qt ≥ 6.6）一步成功，顺带完成启动；没开的（Qt 5 与 Qt 6.2–6.5）
    // fetch 对自定义 scheme 立即拒绝——定格推入车道，等 QML 轮询
    // AWB.channel() 读到 "push" 后推文档进来（push 到来即启动）。
    fetchJson(documentUrl(0)).then(function (payload) {
        channel = "fetch";
        // 导出要内联的样式表：boot 时取一次即可（随包内容不会变）
        fetch("/app/theme.css", {cache: "no-store"})
            .then((response) => response.text())
            .then((text) => renderer.setStylesheet(text))
            .catch(function () { /* 导出件少一层样式，不是打不开页面的理由 */ });
        finishBoot(payload);
    }, function () {
        channel = "push";
    });
}

function finishBoot(payload)
{
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
            fetchApi: channel === "fetch"
        });
    }
    return payload;
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
    // 车道结论（"" = 还在探，"fetch" / "push"）：QML 轮询它决定要不要推入。
    channel: function () { return channel; },
    // 推入车道的注入口（QML 在页面装载后经 runJavaScript 调用）。
    // kind: "document" | "stylesheet"。document 到来即启动（fetch 车道
    // 没有人调它，channel 已定格为 "fetch"）。
    push: function (kind, body) {
        if (channel === "fetch") {
            return;
        }
        channel = "push";
        if (kind === "stylesheet") {
            pushed.stylesheet = body;
            if (renderer) {
                renderer.setStylesheet(body);
            }
            return;
        }
        if (kind !== "document") {
            return;
        }
        pushed.document = body;
        parseDocument(pushed.document).then(function (payload) {
            if (pushed.stylesheet && renderer) {
                renderer.setStylesheet(pushed.stylesheet);
            }
            finishBoot(payload);
        }, function (error) {
            beacon("error", {blockIndex: -1,
                             message: "boot failed: " + String(error && error.message)});
        });
    },
    // 以下是拉取口：QML 与测试都用它们把结果读出来，不需要往页面里塞测试钩子
    stats: function () { return renderer ? renderer.stats() : {blocks: 0}; },
    idle: function () { return renderer ? renderer.isIdle() : {}; },
    capabilities: function () {
        return {
            page: PAGE_REVISION,
            fetchApi: channel === "fetch",
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
