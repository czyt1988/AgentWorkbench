#!/usr/bin/env bash
# 取 Markdown 预览运行时的第三方构建产物，落到 third_party/markdown-runtime/。
#
# 这是**手动**步骤，不参与构建：需要外网与 Node 20+，只在你手上有源时跑
# （构建机在内网、且没有 Node，跑不了也不该跑）。跑完把整个目录与
# manifest.json 一起提交，并跑 scripts/check-markdown-runtime.sh 复核。
#
# 用法：
#   bash scripts/update-markdown-runtime.sh                 # 按下面的钉定版本取
#   bash scripts/update-markdown-runtime.sh --set mermaid 11.18.0   # 改钉定再取
#
# 为什么只取每个包的单文件浏览器构建（UMD/IIFE）而不是 ESM：预览页从一个自
# 定义 scheme 加载它们，一个 `dist/chunks/*.mjs` 的依赖图会把「离线、同源、
# 无构建期打包工具」这三条都赌上；下列文件全部以 classic script 暴露全局，
# 已在本机核实（mermaid 的 min.js 里 import( 与 chunks/ 引用均为 0 处）。
set -euo pipefail

# 钉定版本：改动要写进 CHANGELOG 与 docs/development/markdown-preview.md。
PIN_MARKDOWN_IT="15.0.2"
PIN_KATEX="0.19.0"
PIN_MERMAID="11.17.2"          # 11.x 线：dist/mermaid.min.js 是自包含 IIFE
PIN_HIGHLIGHT_JS="11.12.0"     # @highlightjs/cdn-assets（根 highlight.min.js 含常用语言）

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${repo_root}/third_party/markdown-runtime"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --set)
            case "$2" in
                markdown-it) PIN_MARKDOWN_IT="$3" ;;
                katex) PIN_KATEX="$3" ;;
                mermaid) PIN_MERMAID="$3" ;;
                highlight.js) PIN_HIGHLIGHT_JS="$3" ;;
                *) echo "unknown package: $2" >&2; exit 2 ;;
            esac
            shift 3
            ;;
        --help|-h)
            sed -n '2,20p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

command -v npm >/dev/null 2>&1 || { echo "npm not found; run this on a machine with Node 20+ and network" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

echo "==> fetching markdown-it@${PIN_MARKDOWN_IT} katex@${PIN_KATEX} mermaid@${PIN_MERMAID} @highlightjs/cdn-assets@${PIN_HIGHLIGHT_JS}"
(
    cd "${work}"
    npm pack --silent "markdown-it@${PIN_MARKDOWN_IT}" >/dev/null
    npm pack --silent "katex@${PIN_KATEX}" >/dev/null
    npm pack --silent "mermaid@${PIN_MERMAID}" >/dev/null
    npm pack --silent "@highlightjs/cdn-assets@${PIN_HIGHLIGHT_JS}" >/dev/null
    for t in *.tgz; do tar xzf "${t}"; mv package "pkg-${t%.tgz}"; done
)

pkg() {
    # 按包名前缀找到解压目录（tarball 名字里的 scope 会把 @ 换成下划线）。
    local prefix="$1"
    (cd "${work}" && ls -d pkg-* 2>/dev/null | grep -F "${prefix}" | head -1)
}

mdit="$(pkg 'markdown-it-')"
kx="$(pkg 'katex-')"
mm="$(pkg 'mermaid-')"
hj="$(pkg 'highlightjs-cdn-assets-')"

for d in "${mdit}" "${kx}" "${mm}" "${hj}"; do
    [[ -n "${d}" ]] || { echo "a package did not unpack" >&2; exit 1; }
done

mkdir -p "${dest}/markdown-it" "${dest}/katex/fonts" "${dest}/mermaid" "${dest}/highlight-js"

# 白名单拷贝：只取渲染需要的文件，不取 sourcemap、ESM 分片、未用的字体格式。
# KaTeX 的字体只留 woff2：上游 CSS 的 @font-face 把 woff2 排在最前，Chromium
# 取到第一个可用源即停，所以 woff/ttf 永远不会被请求——保留它们只是 40 个死文件。
cp "${work}/${mdit}/dist/browser/markdown-it.umd.min.js" "${dest}/markdown-it/"
cp "${work}/${kx}/dist/katex.min.js" "${work}/${kx}/dist/katex.min.css" "${dest}/katex/"
cp "${work}/${kx}"/dist/fonts/*.woff2 "${dest}/katex/fonts/"
cp "${work}/${mm}/dist/mermaid.min.js" "${dest}/mermaid/"
cp "${work}/${hj}/highlight.min.js" "${dest}/highlight-js/"

# 许可证随包分发：这四个都以 MIT/BSD-3 要求保留版权声明。
cp "${work}/${mdit}/LICENSE" "${dest}/markdown-it/LICENSE" 2>/dev/null || true
cp "${work}/${kx}/LICENSE" "${dest}/katex/LICENSE" 2>/dev/null || true
cp "${work}/${mm}/LICENSE" "${dest}/mermaid/LICENSE" 2>/dev/null || true
cp "${work}/${hj}/LICENSE" "${dest}/highlight-js/LICENSE" 2>/dev/null || true

# manifest.json：版本、许可证、逐文件 bytes + sha256。
# 运行时资产与清单的一致性由 tst_markdownruntime 在 `build.sh --test` 里校验
# （不另设需要 node 的 shell 检查脚本：构建机没有 Node，但一定有编译器）。
node -e '
const fs = require("fs"), path = require("path"), crypto = require("crypto");
const dest = process.argv[1];
const libs = [
    { name: "markdown-it", version: process.argv[2], license: "MIT",
      dir: "markdown-it", files: ["markdown-it.umd.min.js"], global: "markdownit" },
    { name: "katex", version: process.argv[3], license: "MIT",
      dir: "katex", files: ["katex.min.js", "katex.min.css"], global: "katex" },
    { name: "mermaid", version: process.argv[4], license: "MIT",
      dir: "mermaid", files: ["mermaid.min.js"], global: "mermaid" },
    { name: "highlight.js", version: process.argv[5], license: "BSD-3-Clause",
      dir: "highlight-js", files: ["highlight.min.js"], global: "hljs" },
];
const fontDir = path.join(dest, "katex", "fonts");
const fonts = fs.readdirSync(fontDir).filter((f) => f.endsWith(".woff2")).sort();
const out = { generated: new Date().toISOString().slice(0, 10), node: process.version, libs: [] };
for (const lib of libs) {
    // 只有 katex 带字体：其它包迭代到 fonts/ 会去敲自己目录下不存在的路径。
    const rels = [...lib.files, ...(lib.dir === "katex" ? fonts.map((f) => "fonts/" + f) : [])];
    const entries = [];
    for (const rel of rels) {
        const p = path.join(dest, lib.dir, rel);
        if (!fs.existsSync(p)) { console.error("missing " + p); process.exit(1); }
        const buf = fs.readFileSync(p);
        entries.push({ path: rel, bytes: buf.length,
                       sha256: crypto.createHash("sha256").update(buf).digest("hex") });
    }
    out.libs.push({ name: lib.name, version: lib.version, license: lib.license,
                    // dir 是入库目录名（highlight.js 的目录是 highlight-js：包名
                    // 带点，不能直接当 URL 段用）。消费方一律用 dir，不用 name。
                    dir: lib.dir, browserGlobal: lib.global,
                    files: entries.filter((e) => !e.path.startsWith("fonts/")),
                    fonts: lib.dir === "katex" ? entries.filter((e) => e.path.startsWith("fonts/")) : undefined });
}
out.libs.forEach((l) => { if (l.fonts === undefined) delete l.fonts; });
fs.writeFileSync(path.join(dest, "manifest.json"), JSON.stringify(out, null, 2) + "\n");
const total = out.libs.reduce((n, l) => n + l.files.reduce((m, f) => m + f.bytes, 0)
    + (l.fonts || []).reduce((m, f) => m + f.bytes, 0), 0);
console.log("manifest written: " + out.libs.map((l) => l.name + "@" + l.version).join(", "));
console.log("total vendored bytes: " + (total / 1024 / 1024).toFixed(2) + " MiB");
' "${dest}" "${PIN_MARKDOWN_IT}" "${PIN_KATEX}" "${PIN_MERMAID}" "${PIN_HIGHLIGHT_JS}"

echo "==> done. next: bash scripts/build.sh --test"
