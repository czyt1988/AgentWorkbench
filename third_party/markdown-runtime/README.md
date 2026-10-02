# Vendored Markdown preview runtime

Prebuilt browser bundles for the Markdown preview window. **Committed as
build artifacts, not source** — the app renders offline, so there is no CDN and
no network access at build time (the machine that packages a release may be on
an intranet with nothing reachable).

Each file here is served to the embedded view over the `awbmd://` scheme from
Qt resources; nothing on this line of the codebase runs at build time.

## What is in here

| Library | Version | File(s) | UMD/IIFE global |
|---|---|---|---|
| markdown-it | 15.0.2 | `markdown-it/markdown-it.umd.min.js` | `markdownit` |
| KaTeX | 0.19.0 | `katex/katex.min.js`, `katex/katex.min.css`, `katex/fonts/*.woff2` | `katex` |
| mermaid | 11.17.2 | `mermaid/mermaid.min.js` | `mermaid` |
| highlight.js | 11.12.0 | `highlight-js/highlight.min.js` | `hljs` |

`manifest.json` records version, license and, per file, its byte size and
sha256. `tst_markdown` recomputes the hashes from the source tree, so an edited
or truncated vendor file fails the test suite rather than turning into a
mysterious rendering bug.

## Why exactly these files

Only the single-file browser builds are taken. The ESM builds are deliberately
left out: `mermaid` ships its ESM form as `dist/mermaid.esm.min.mjs` plus a
`dist/chunks/*.mjs` graph, and a chunk graph turns "serve local assets from a
custom scheme" into a dynamic-import resolution problem. `mermaid.min.js` in
11.x is a self-contained IIFE — it contains no `import(` expression and no
reference to `chunks/`, so one request renders a diagram.

KaTeX fonts are **woff2 only**. The upstream `katex.min.css` is vendored
unpatched: its `@font-face` rules list `woff2` first, and Chromium stops at the
first source it can load, so the `woff`/`ttf` URLs are never requested. Keeping
them would add 40 dead files and a byte-for-byte divergence from upstream.

highlight.js comes from `@highlightjs/cdn-assets`, not from `highlight.js`
itself — only the CDN asset package ships a browser bundle. Its root
`highlight.min.js` already includes the common language set; taking
`languages/*.min.js` separately would mean 193 resource aliases and 193
requests to style a code block.

## How to update

Run this on a machine **with** network and Node 20+ (it is never run by the
build):

```bash
bash scripts/update-markdown-runtime.sh                 # pinned versions
bash scripts/update-markdown-runtime.sh --set mermaid 11.18.0
```

The script `npm pack`s the pinned tarballs, copies only the whitelisted files
listed above, rewrites `manifest.json`, and prints the total size. Commit the
directory together with the manifest.

Then verify:

```bash
bash scripts/build.sh --test    # tst_markdown asserts the sha256 table
```

## Version pinning notes

- `mermaid` stays on the 11.x line. 12.x is not taken until its `dist` layout
  is verified to still ship a self-contained IIFE bundle.
- The `markdown-it` browser bundle changed path in 14.x→15.x
  (`dist/markdown-it.min.js` → `dist/browser/markdown-it.umd.min.js`); if the
  update script ever stops finding it, the upstream layout changed again.
- `.gitattributes` marks this directory `-text`. With `core.autocrlf=true`, any
  EOL conversion would silently break the hash comparison in `tst_markdown`.

## Licenses

markdown-it (MIT), KaTeX (MIT), mermaid (MIT), highlight.js (BSD-3-Clause).
Each subdirectory carries the upstream `LICENSE` file; keep it there when
updating.
