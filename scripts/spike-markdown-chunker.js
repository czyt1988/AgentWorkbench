// S4 spike：逐块渲染再拼接，与整篇渲染是否等价。
//
// 分块管线（renderer.js）的性能全部建立在「块是独立可渲染单位」这个前提上，
// 而前提错了的表现不是报错，是**内容悄悄变形**：吞掉的段落、变成字面文本的
// 表格、多出来的 <p>。这个脚本就是用来把那种变形当场抓出来的。
//
// 它跑的是页面用的同一个 markdown-it 实例与同一套规则（markdown-config.js
// 的 createMarkdown），所以比对的是真实渲染路径，不是另写一份近似实现。
//
// 用法（两步，块由 C++ 的切分器产出，脚本自己不懂切块）：
//
//   AWB_MD_CHUNK_DUMP=/tmp/mdchunks ./build/tst_markdown testCorpusFidelity
//   node scripts/spike-markdown-chunker.js /tmp/mdchunks
//
// 退出码：0 = 语料全部等价；1 = 有文档不一致（并打印差异上下文）；2 = 用法错误。
// 语料是本仓库的 docs/（几十篇真实 Markdown，含中英混排、表格、围栏、公式），
// 所以这里比对的是「会被渲染的东西」，不是构造出来的边界样例——后者由
// tst_markdownchunker 的用例负责。

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import {fileURLToPath, pathToFileURL} from "node:url";

// 脚本在 scripts/ 下，仓库根就是它的上一级
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function argList()
{
    return process.argv.slice(2);
}

const dumpDir = argList()[0];
if (!dumpDir || !fs.existsSync(dumpDir)) {
    process.stderr.write("usage: node scripts/spike-markdown-chunker.js <dump-dir>\n"
                         + "  (produce it with AWB_MD_CHUNK_DUMP=<dump-dir> "
                         + "./build/tst_markdown testCorpusFidelity)\n");
    process.exit(2);
}

// --max-diffs N：打印多少条差异样本（默认 3 条，够看出规律又不至于刷屏）
function flagValue(name, fallback)
{
    const argv = argList();
    const at = argv.indexOf(name);
    return at >= 0 && argv[at + 1] ? Number(argv[at + 1]) : fallback;
}
const maxDiffs = flagValue("--max-diffs", 3);

// --- 加载随包的 UMD 构建 -------------------------------------------------
// new Function 的函数体里没有 exports/module/define，UMD 的三条分支只剩
// 「挂到全局」那一条；this 由 call(globalThis) 给定，所以脚本落到
// globalThis.markdownit / globalThis.katex 上——和页面里 <script src> 的效果
// 一致（页面侧的 KaTeX 全局是对象，math-rule.js 读的就是 globalThis.katex）。
function loadUmd(relativePath)
{
    const file = path.join(repoRoot, relativePath);
    const code = fs.readFileSync(file, "utf8");
    (new Function(code)).call(globalThis);
}

loadUmd("third_party/markdown-runtime/markdown-it/markdown-it.umd.min.js");
loadUmd("third_party/markdown-runtime/katex/katex.min.js");

async function loadCreateMarkdown()
{
    const modulePath = path.join(repoRoot,
                                 "src/markdown/webengine/runtime/markdown-config.js");
    const url = pathToFileURL(modulePath).href;
    const mod = await import(url);
    if (typeof mod.createMarkdown !== "function") {
        throw new Error("markdown-config.js does not export createMarkdown");
    }
    return mod;
}

const {createMarkdown, normalizeRelative, encodeSegments} = await loadCreateMarkdown();

// 与 renderer.js 的 assetUrl 同形：相对路径 → awbmd://runtime/asset/d1/<rel>，
// 外链与锚点原样不动。图片 src 必须在两种渲染下变成同一个字符串，否则差异
// 里分不清是切块问题还是映射问题。
function assetUrl(relPath)
{
    const text = String(relPath || "");
    if (!text || text.charAt(0) === "#"
        || /^[a-z][a-z0-9+.-]*:/i.test(text.trim()) || text.slice(0, 2) === "//") {
        return "";
    }
    const rel = normalizeRelative(text).replace(/^\/+/, "");
    return rel ? "awbmd://runtime/asset/d1/" + encodeSegments(rel) : "";
}

// 每次比对都新建实例：markdown-it 的实例带内部状态（引用定义、linkify 的
// 一次扫描），复用同一实例做整篇与逐块两次渲染会让「逐块」沾到整篇留下的状态，
// 比对结果因此偏乐观。
function freshRender(text)
{
    return createMarkdown({assetUrl: assetUrl}).render(text);
}

// 归一化只消掉「块与块之间的分隔换行」：拼接时每个块的输出之间必然多插一个 \n，而整篇渲染
// 里 markdown-it 自己决定要不要补（它按后面还有没有 token 决定，单独渲染一块时
// 那个 token 是最后一个，就不补）。于是两边的字符串一定在标签边界上差空白，而那不是
// 内容差异。
//
// 规则因此只有一条：把 `>` 与 `<` 之间的空白整个去掉。代码内容的换行**保留**
// ——`abc\n</code>` 里换行的前面是文本不是 `>`，动不到它，「少渲染一行代码」这类
// 真差异照样会露出来。`<pre><code>` 这类本来就没有空白的边界同样不受影响。
function normalize(html)
{
    return html.replace(/>\s+</g, "><").trim();
}

function context(whole, chunked, at)
{
    const from = Math.max(0, at - 90);
    const cut = (text) => JSON.stringify(text.slice(from, at + 90));
    return "  first difference at offset " + at + "\n"
        + "    whole : " + cut(whole) + "\n"
        + "    chunk : " + cut(chunked) + "\n";
}

// 找出「哪个块开始的输出对不上」：按块累加，只要累计长度越过了差异偏移，
// 那个块就是嫌疑块。打印块索引和它的原文首行，直接就能拿去复现。
function suspectBlock(blocks, perBlock, at)
{
    let acc = 0;
    for (let i = 0; i < blocks.length; ++i) {
        acc += perBlock[i].length;
        if (acc >= at) {
            const firstLine = String(blocks[i].t || "").split("\n")[0].slice(0, 70);
            return "  suspect block: #" + blocks[i].i + " (line " + blocks[i].s
                + ") `" + firstLine + "`\n";
        }
    }
    return "";
}

const files = fs.readdirSync(dumpDir).filter((name) => name.endsWith(".json"))
    .map((name) => path.join(dumpDir, name));
if (!files.length) {
    process.stderr.write("no *.json dumps under " + dumpDir + "\n");
    process.exit(2);
}

let identical = 0;
let whitespaceOnly = 0;
const mismatches = [];
let totalBlocks = 0;
let featureDocs = {math: 0, diagram: 0, table: 0, code: 0, alert: 0, task: 0};

for (const dumpFile of files) {
    const dump = JSON.parse(fs.readFileSync(dumpFile, "utf8"));
    const sourcePath = dump.path;
    if (!sourcePath || !fs.existsSync(sourcePath)) {
        process.stderr.write("dump refers to a missing file: " + sourcePath + "\n");
        process.exit(2);
    }
    // 与 MarkdownDocument 载入后的内存形态一致：CRLF 归成 LF
    const text = fs.readFileSync(sourcePath, "utf8")
        .replace(/\r\n/g, "\n").replace(/\r/g, "\n");
    const blocks = dump.blocks || [];
    totalBlocks += blocks.length;

    if (/\$\$|\$[^$\n]+\$/.test(text)) { featureDocs.math += 1; }
    if (/```[ \t]*mermaid/.test(text)) { featureDocs.diagram += 1; }
    if (/^\|.*\|$/m.test(text)) { featureDocs.table += 1; }
    if (/^```[ \t]*[a-z]/m.test(text)) { featureDocs.code += 1; }
    if (/^>[ \t]*\[![A-Z]+?\]/m.test(text)) { featureDocs.alert += 1; }
    if (/^[-*+][ \t]+\[[ x]\]/m.test(text)) { featureDocs.task += 1; }

    const whole = normalize(freshRender(text));
    const perBlock = blocks.map((block) => normalize(freshRender(block.t || "")));
    // 逐块拼接 = 每块输出直接相接。归一化已经把边界空白消掉了，所以这里不需要
    // 再插分隔符——插了反而让 suspectBlock 的偏移量算不准。
    const chunked = perBlock.join("");
    if (whole === chunked) {
        identical += 1;
        continue;
    }
    // 拼接产生的换行位置本就与整篇不同；只有连归一化都对不上才是语义差异。
    // 这里两边都已 normalize，所以走到这一步一律算真差异——保留 whitespaceOnly
    // 计数位是为了让「差异全是空白」这种状态可见，而不是把它混进 identical。
    if (whole.replace(/\s+/g, " ") === chunked.replace(/\s+/g, " ")) {
        whitespaceOnly += 1;
        continue;
    }
    let at = 0;
    while (at < whole.length && at < chunked.length
           && whole.charAt(at) === chunked.charAt(at)) {
        at += 1;
    }
    mismatches.push({
        name: path.basename(sourcePath),
        blocks: blocks.length,
        detail: context(whole, chunked, at) + suspectBlock(blocks, perBlock, at)
    });
}

process.stdout.write("corpus: " + files.length + " documents, " + totalBlocks
                     + " blocks\n");
process.stdout.write("features present: math=" + featureDocs.math
                     + " mermaid=" + featureDocs.diagram
                     + " table=" + featureDocs.table
                     + " code=" + featureDocs.code
                     + " alert=" + featureDocs.alert
                     + " task=" + featureDocs.task + "\n");
process.stdout.write("identical (tag-boundary whitespace normalised): " + identical
                     + "\n");
process.stdout.write("differ only in whitespace: " + whitespaceOnly + "\n");
process.stdout.write("semantic mismatches: " + mismatches.length + "\n");

for (const mismatch of mismatches.slice(0, maxDiffs)) {
    process.stdout.write("\n" + mismatch.name + " (" + mismatch.blocks
                         + " blocks)\n" + mismatch.detail);
}
process.exit(mismatches.length ? 1 : 0);
