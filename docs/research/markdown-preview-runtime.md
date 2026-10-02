# Markdown preview runtime — Research

> Research date: 2026-10-02
> Scope: AgentWorkbench `feat/markdown-preview-core` (Qt 6.7.3 / msvc2019_64 / Windows 11, Debug build)
> Verdict: **feasible, and the performance premise holds.** A custom scheme serves the whole
> third-party rendering runtime offline (scripts, CSS, `woff2` fonts, the document, relative images,
> beacons); block-by-block rendering is equivalent to whole-document rendering on the repository's own
> Markdown corpus; a ~486 KB / 3150-block document paints its first content in ~270 ms and finishes in
> ~7.6 s on a **Debug** binary, with ~1.85 s of that being actual parse work. The two things that broke
> during the spikes were both silent-half-failure modes of the WebEngine scheme API, not the renderer.
> The shipped design lives in `docs/development/markdown-preview.md`.

## 1. Questions and method

### 1.1 The questions

Four capability spikes, each with a pre-declared fallback so a failure could not stall the feature,
plus one measurement question:

| # | Question | Pass condition | Pre-agreed fallback |
| --- | --- | --- | --- |
| S1 | Can `awbmd://` serve scripts, `<link>` stylesheets and `@font-face` with correct MIME on Qt 6.7.3's Chromium? | KaTeX glyphs render, module scripts execute, `fetch()` works | Unpack to disk and use `file://` |
| S2 | Can a dedicated worker be constructed under that scheme (parsing off the main thread)? | A worker parses a slice | Main-thread sharding, which was already the default path |
| S3 | Does mermaid 11's single-file IIFE draw a diagram inside this engine? | A diagram reaches `data-status="rendered"` | Fall to mermaid 10.9.x IIFE; last resort: ship without diagrams and record it |
| S4 | Is rendering block-by-block then concatenating equivalent to rendering the whole document? | Identical HTML across a real corpus | Parse whole-document, mount in blocks; incremental update by line range |
| M | Is the first paint actually bounded by the first pass rather than by the document? | Measured timings in the log | No claim in the documentation |

### 1.2 Method

The spikes were run **inside this repository**, not in a throwaway app, because the failure modes
turned out to depend on the exact wiring order in `app/main.cpp`. S1 is a probe page and probe module
(`tests/webengine/md-probe.html`, `tests/webengine/md-probe-module.js`) loaded into a real
`WebEngineView` under the registered scheme; S3 and M are exercised by `tst_markdownpreview`; S4 is
`scripts/spike-markdown-chunker.js` fed by a dump produced from `tst_markdown`.

There was no external benchmark and no comparison app. In particular **no head-to-head measurement
against VS Code was performed** — see section 8.

### 1.3 Environment

| Item | Value |
| --- | --- |
| Qt | 6.7.3, `msvc2019_64` (embedded Chromium 122) |
| Compiler / build | MSVC 2019 + Ninja, **Debug** configuration |
| OS | Windows 11 |
| Rendered fixture | Generated Markdown, 485 880 bytes, 3150 top-level blocks, no diagrams |
| Vendored runtime | markdown-it 15.0.2, KaTeX 0.19.0, mermaid 11.17.2, highlight.js 11.12.0 |

## 2. Verdicts

| # | Verdict | Consequence |
| --- | --- | --- |
| S1 | **Pass**, with two hazards that produce a *half*-broken page rather than an error | The design keeps the custom scheme; the provider self-checks its own registration |
| S2 | **Not required.** The shipped pipeline never constructs a worker | `ServiceWorkersAllowed` is set as a reservation; no second rendering path exists |
| S3 | **Pass** | mermaid stays 11.x, injected lazily |
| S4 | **Pass** on the corpus | The chunked pipeline stands as designed; the chunker stays a line-level state machine |
| M | **Pass**, and the premise was wrong in an instructive way — see 7 | Documented as "first paint pays the first pass", with numbers, not as "faster than VS Code" |

## 3. S1 — a custom scheme as the local asset supplier

`QWebEngineUrlSchemeHandler` on `awbmd://runtime/…` serves Qt resources, the live document, files from
the document's own directory, and small GET beacons from one origin. Everything S1 asked for works:
module scripts execute, the stylesheet resolves `url(fonts/*.woff2)` through a sub-path, KaTeX's woff2
decodes, same-origin `fetch()` of `doc.json` returns, document-relative images load.

Two findings changed the implementation, and both are the kind that stay invisible:

**Order dependency.** `registerScheme()` must precede `QtWebEngineQuick::initialize()`; the engine
freezes the scheme table at first context creation. The failure is not a 404 and not an exception: the
handler still answers requests, so scripts, fonts and the document all arrive, while `fetch()` rejects
with "URL scheme awbmd is not supported". A page in that state shows prose and never shows a formula or
a diagram, and the build plus every pure test stays green. `MarkdownPreviewProvider` therefore reads
its own registration back through `QWebEngineUrlScheme::schemeByName()` and reports the surface as
unavailable when it is absent.

**`Syntax::HostAndPort` is a trap.** The original plan said to declare `HostAndPort` because the URLs
have a host. With that syntax, `registerScheme()` requires a default port; leaving it
`PortUnspecified` makes the call return immediately with one warning, and the scheme never enters the
engine's registry — the exact half-failure above, reached by a *correct-looking* line of code. The URLs
carry no port, so `Syntax::Host` is both sufficient and the only syntax that keeps the port unspecified.
This is a correction to the design plan, not just a detail.

Also established: `FetchApiAllowed` does not exist before Qt 6.6, so the 6.5 route cannot rely on the
page fetching its document; the page has a second entry that receives the text pushed via
`runJavaScript`. Serving bytes with the right `Content-Type` is mandatory — a JS file served as
`text/plain` is not executed by Chromium's MIME checking, which is why `MarkdownRuntime` carries an
explicit extension-to-MIME table rather than sniffing. Vendor bytes are cached in memory after first
read: a 2.6 MB compressed mermaid is not re-inflated per request, and a first paint asks for seven or
eight font files.

## 4. S2 — why no worker

Nothing in the design needed a worker, so the spike was not made a gate. The measurements explain why
that was the right call, and they also say something about scheduling that is more load-bearing than
the parse cost itself: on the same fixture, the ~1.85 s of parse work was accompanied by roughly
**105 s** of wall time when the passes were scheduled with `requestIdleCallback`, and about **16 s**
when they were scheduled on animation frames. The shipped scheduler takes a plain timer and checks
elapsed milliseconds against a 48 ms budget per pass.

In other words, the dominant cost was how often and how much the engine was willing to hand the main
thread over, not markdown-it's speed. That is a property of thousands of small callbacks, and a worker
would not have fixed it — the mount has to happen on the main thread anyway. Moving parse into a worker
would add a second rendering path to keep in sync (worker and page must agree on the markdown-it
configuration) for a benefit that had already been obtained by fixing the scheduling policy.

## 5. S3 — mermaid without a chunk graph

mermaid 11.x's `mermaid.min.js` is a self-contained IIFE: it contains no `import(` expression and no
reference to `dist/chunks/`. That is the reason it is the one taken. The ESM build is a chunk graph, and
a chunk graph turns local asset serving into a dynamic-import resolution problem inside a custom scheme —
for no benefit, since one IIFE request renders a diagram.

Diagrams draw inside Chromium 122 with `securityLevel: 'strict'` and `startOnLoad: false`; rendering is
driven manually, two at a time, by an `IntersectionObserver` so the bundle itself is not even requested
by a document with no `mermaid` fence. Documents with more than 40 diagrams switch placeholders to
click-to-render rather than blocking the page.

## 6. S4 — chunk boundaries and equivalence

The chunker's premise is that rendering each top-level block and concatenating is identical to rendering
the whole file. Getting this wrong does not raise an error; it silently deforms content — a paragraph
after a list swallowed into the list, a table printed as literal pipes, a stray `<p>`.

The corpus is this repository's own `docs/` tree: roughly seventy Markdown files, English pages and
their Chinese mirrors, containing mixed CJK prose, nested lists, fenced code with blank lines and fake
headings inside them, tables, setext headings, and blockquotes. `tst_markdown`'s corpus case asserts
structural properties (every non-blank line belongs to a block, blocks neither overlap nor run past the
file, a block that starts immediately after its neighbour begins a legal new block) and dumps blocks;
`scripts/spike-markdown-chunker.js` then compares per-block HTML against whole-document HTML **using the
same `markdown-config.js` instance the page uses**, so the check exercises the real render path rather
than an approximation of it.

Result: equivalent, no fallback triggered. The cases that had to be handled explicitly to get there are
the ones a "split on blank lines" implementation gets wrong: fenced code blocks, `$$ … $$` display math,
setext underline rows, loose-list continuation paragraphs, and table runs.

## 7. Measured performance

A Debug build, generated fixture, `AWB_MD_PERF=1 ./build/tst_markdownpreview.exe testLargeDocumentMounts`:

```text
markdown perf fixture: 485880 bytes
awb.perf: markdown: opened "note.md" 485880 bytes -> 3150 blocks
awb.perf: markdown: page ready {"page":"awbmd-page-1","rev":1,"blocks":3150,
          "firstPaintMs":266,"contentVisibility":true,"fetchApi":true}
awb.perf: markdown: frame {"parseMs":1851,"mountMs":29,"passes":50,"blockCount":3150,
          "mountedCount":3150,"pendingBlocks":0,"pendingDiagrams":0,"totalMs":5633}
markdown perf: 3150 blocks / 485880 bytes mounted in 7626 ms
markdown perf: appended one block, 274 ms round trip
```

| Measurement | Value | What it means |
| --- | --- | --- |
| `firstPaintMs` | **266 ms** | Time until the first content is on screen — the number the design optimises |
| `parseMs` | 1851 ms | Actual main-thread parse+element-construction work over all 3150 blocks |
| `mountMs` | 29 ms | Cost of inserting all prepared blocks into the tree |
| `passes` | 50 | The document was parsed in fifty budgeted slices |
| `totalMs` | 5633 ms | Page-side span from render start to drained queue |
| Test wall clock | 7626 ms | Includes showing a real window, loading the page, and the C++ round trip |
| One-block append | 274 ms | Editor change → debounced re-split → diff → page update, end to end |

The reading that matters: **the whole-document layout cost is ~0.03 s, and the parse cost is ~1.9 s.**
Layout being nearly free is a property of CSS `content-visibility` (offscreen blocks skip layout and
paint); it is why the pipeline can mount everything and still scroll smoothly, and it is why the
"first pass paints early" strategy is sound. The gap between `parseMs` 1851 ms and `totalMs` 5633 ms is
scheduling — the cost of yielding to keep input responsive.

An earlier run of the same case, recorded during the spike, gave `firstPaintMs` 275 / `parseMs` 1823 /
`totalMs` 5603 / 212 ms for the incremental round trip, so these numbers repeat to within a few percent
on this machine.

## 8. What these numbers do not prove

- **They are Debug-build numbers.** A Release binary will differ, probably by a lot on `parseMs`. The
  re-measurement is listed below, not silently assumed.
- **There is no VS Code comparison.** The claim in the development documentation is *mechanistic* — a
  whole-document first paint versus a first-pass one — and is supported by `firstPaintMs` here. "Faster
  than VS Code on the same file on this machine" was never measured and is not claimed.
- **`content-visibility` is measured as a boolean, not as a delta.** The page reports whether the
  property took effect; an A/B with the property disabled would quantify the benefit more directly.
- **The fixture has no diagrams, no tables of pathological width and no formulas.** It measures text
  throughput. The diagram path is covered by `tst_markdownpreview`'s smoke cases, not by these numbers.
- **A hidden window cannot be used for any of this.** `requestAnimationFrame` and idle callbacks are not
  pumped for a `WebEngineView` that is not shown, so every timing case in this area has to show a real
  window; a test that "hangs" here is usually not hanging on the renderer.

## 9. Costs accepted

- Roughly 4 MB of third-party JavaScript and fonts embedded in every Qt 6 + WebEngine binary, 2.6 MB of
  which is mermaid and loaded only when a document contains a diagram.
- Inline HTML is not rendered (`html: false`), because this window shows files the user did not write
  and there is no CSP on a local custom scheme.
- No Qt 5 support for the feature at all rather than a degraded version of it — the whole runtime is
  gated out of the binary on Qt 5.

## 10. Verification gaps and follow-ups

1. **Qt 5.15.16 was not verified on this machine** (no such SDK installed locally). The change surface
   on that route is provably small — no new QML, no new resources, no new CMake components, one menu
   item that evaluates to hidden — but it has not been compiled here.
2. **Release-build re-measurement** of `testLargeDocumentMounts` to replace the Debug numbers, and
   ideally a recorded A/B with `content-visibility` disabled.
3. **mermaid 12.x** — revisit only after its `dist` layout is confirmed to still ship a self-contained
   IIFE.
4. **A real VS Code comparison** if the "faster than the usual previews" sentence is ever wanted as a
   benchmark rather than as a design statement.
