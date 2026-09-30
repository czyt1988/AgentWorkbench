# Embedding Agent WebUIs with Qt WebEngine — Research

> Research date: 2026-09-26
> Scope: AgentLauncher 0.3.0 (Qt 6.7.3 / msvc2019_64 / Windows 10.0.26200 x64)
> Verdict: **feasible**. A real agent WebUI renders and works inside an embedded view; the main
> costs are portable-package size (35 MB → roughly 110–130 MB) and an older engine
> (Chromium 118 vs. Edge 153 on this machine).
> The remediation plan lives in `docs/superpowers/plans/2026-09-26-webengine-embedding-integration.md`.

## 1. Questions and method

### 1.1 The three questions

1. Is embedding the agent WebUIs inside the app with a Qt web view feasible?
2. Will there be interaction problems?
3. How much slower is it than using Chrome / Edge directly?

### 1.2 Method

Without touching this repository, a throwaway probe app (Qt Quick + `WebEngineView`) was built
outside it. Four experiments answer the questions above:

| Experiment | How | Section |
| --- | --- | --- |
| Capability probe | In-page JS probing engine identity, codecs, JS/CSS features, WebGL | 4.1 |
| Real WebUI | `opencode web` served locally and loaded into the embedded view, then screenshotted | 2.2 |
| Same-page A/B | One benchmark page simulating chat-UI load, run in QtWebEngine and in Edge | 4.3 |
| Packaging | `windeployqt` on the probe, then run with a Qt-free PATH | 2.4 |

Two targeted experiments back specific findings: cross-port cookie / localStorage isolation (3.1)
and process layout (4.2).

### 1.3 Environment

| Item | Value |
| --- | --- |
| Qt | 6.7.3, `C:/Qt/6.7.3/msvc2019_64` (WebEngine / WebView already installed) |
| Compiler | MSVC 2019 (VS 16 Community) + Ninja |
| Browser reference | Edge 153.0.4234.48 (WebView2 runtime also 153) |
| Embedded WebUI | OpenCode (global npm `opencode`, `opencode web`) |
| Display | single monitor, 100% scaling (DPR = 1; fractional-scaling behaviour needs manual checks) |

## 2. Feasibility (question a)

### 2.1 Environment readiness

The local Qt 6.7.3 install already ships everything needed — no extra components to download:

- `Qt6WebEngineQuick` / `Qt6WebEngineCore` / `Qt6WebEngineQuickDelegatesQml`
- `QtWebEngineProcess.exe`, `resources/` (`icudtl.dat` + `*.pak`), `translations/qtwebengine_locales`
- QML module `qml/QtWebEngine` with `WebEngineView`, `WebEngineProfile`, `WebEngineSettings`,
  `WebEngineScript`, …

### 2.2 A real WebUI, embedded

Loading OpenCode's WebUI (`http://127.0.0.1:4199/`) into the embedded view:

- Page loaded in **0.87 s** (`loadStarted` at 507 ms → `LoadSucceeded` at 872 ms, TTFB 39 ms).
- The SPA booted: the title changed from `127.0.0.1:4199` to `OpenCode`, and the sidebar project list
  came from the backend (the WebSocket connected).
- **Zero** console errors, no renderer crash.
- Screenshot: `shot-opencode.png` in the probe project — Chinese UI, icons and layout all intact.

The same probe loading a self-made rendering test page (`caps.html`) was also clean: Chinese/emoji/
rare glyphs, inline SVG, gradients, rounded corners and shadows, native `textarea`, and the reflow of
a long token-by-token text stream (`shot-caps.png`).

### 2.3 API availability (per this install's `plugins.qmltypes`)

| Capability | QML API in 6.7.3 | Notes |
| --- | --- | --- |
| View | `WebEngineView` | `url`, `zoomFactor`, `loadProgress`, `renderProcessTerminated`, … |
| Profile | `WebEngineProfile` | `offTheRecord`, `storageName`, `persistentCookiesPolicy`, `httpCacheType` |
| Context menu | `contextMenuRequested` | Custom menus possible (default is Chromium's) |
| New windows | `newWindowRequested` | **Unhandled ⇒ the requested load fails** (see 3.2) |
| Downloads | `WebEngineProfile.downloadRequested` | Unhandled ⇒ nothing is saved (see 3.2) |
| Dialogs | `authenticationDialogRequested` / `javaScriptDialogRequested` / `fileDialogRequested` | Qt shows a default dialog unless handled |
| Full screen | `fullScreenRequested` | Must be handled or the request is ignored |
| DevTools | `devToolsView` / `devToolsId` | **An in-app DevTools view is possible** |
| Find in page | `findText` | UI must be built |
| Print | `printToPdf` | — |

Initialization requirement (Qt docs): `QtWebEngineQuick::initialize()` "**has to be done before
QGuiApplication is created**". In `src/main.cpp` the `QGuiApplication` is currently the first object
constructed, so the order must change when this lands.

### 2.4 Packaging and standalone run

Running `windeployqt --release --no-translations --no-system-d3d-compiler --qmldir ..` on the probe:

- Output **242 MB** on disk / **111 MB** zipped, of which `Qt6WebEngineCore.dll` alone is **149.6 MB**,
  `resources/` 13.9 MB, `QtWebEngineProcess.exe` 0.7 MB.
- `--no-translations` copies only `qtwebengine_locales/en-US.pak` (404 KB), avoiding the 33 MB locale
  directory in the Qt install.
- Running the deployed exe with **no Qt paths at all** in the environment rendered fine and produced a
  screenshot — the portable path holds.
- Current baseline for comparison: `dist/AgentLauncher` is 88 MB and
  `dist/AgentLauncher-0.3.0-win64-Portable.zip` is 35.0 MB. Expect roughly 110–130 MB zipped and
  250–265 MB extracted after embedding.

## 3. Interaction risks (question b)

### 3.1 Cookies are shared across ports (the key risk for this project)

When no profile is assigned, `WebEngineView` uses the default profile (off-the-record, in-memory
storage), and **every view shares one cookie jar**. Chromium cookies are keyed by host and
**ignore the port** (RFC 6265). Measured, with server-side evidence — a cookie set on
`127.0.0.1:8741` is sent to `127.0.0.1:8742`:

```
NOTE from 127.0.0.1:8741 :: {"js_cookie":"alapp=port8741; session=agent-A","ls":"port8741"}
NOTE from 127.0.0.1:8742 :: {"js_cookie":"alapp=port8741; session=agent-A","ls":null,
                            "server_saw_cookie":"alapp=port8741; session=agent-A"}
```

`localStorage`, by contrast, is correctly isolated (per origin, port included). Every built-in agent
in AgentLauncher listens on `127.0.0.1` (58627 / 4096 / 4170 / 18789 / 3080), so any WebUI that keeps
its session in a cookie — even a generically named `session` — will collide with the others.

**Fix:** one `WebEngineProfile` per agent (persistent, own `storageName` and storage directory), which
also settles whether localStorage survives a restart.

### 3.2 Default behaviours that need code

| Case | Unhandled behaviour (Qt docs / verified locally) | Impact |
| --- | --- | --- |
| `target="_blank"` / `window.open` / OAuth popups | **Request fails** ("If this signal is not handled, the requested load will fail") | Logins and external links appear dead |
| Downloads (export a session, save a file) | `downloadRequested` unhandled ⇒ nothing is written | User clicks, nothing happens |
| HTTP basic auth / `alert` / `<input type=file>` | Qt default dialogs appear (`request.accepted = true` to take over) | Acceptable; styling may differ from the app |
| Page full-screen requests | Ignored | Video full-screen breaks |

### 3.3 Shortcuts and focus

Once the view has focus, F5 / Esc / Ctrl+W / Ctrl+F / Ctrl+P belong to Chromium and collide with
application-level shortcuts. Decide which keys the app owns and reclaim them with
`Shortcut { context: Qt.ApplicationShortcut }`; find-in-page (`findText`) needs its own UI.

### 3.4 Background throttling and view lifetime

Chromium throttles occluded/hidden pages. Measured in Edge with the window occluded:
`document.hidden = true` and `requestAnimationFrame` dropped to **1 fps**. QtWebEngine uses the same
throttling logic and its visibility follows the QQuickItem/window state, so the same behaviour is
expected (not separately reproduced on the Qt side here; re-verify after implementing). Effect: a
hidden view stops rendering, while WebSocket/SSE connections and message handlers keep running and
catch up when shown again; a WebUI that polls with `setInterval` may drop to roughly once a minute
while hidden.

Memory matters more: a single view costs about **+250–350 MB** working set (see 4.2), so five open
views can reach the 1.5 GB range. An explicit keep-alive policy is needed (`lifeCycleState` supports
`Frozen` since Qt 6.5, which releases memory while keeping the session).

### 3.5 Manual verification checklist

These cannot be automated and must be checked by hand once implemented:

- [ ] Chinese IME: candidate window, inline composition, caret tracking
- [ ] Fractional DPI scaling (125% / 150%): sharpness and hit targets
- [ ] Copy/paste inside the view and between the view and QML text fields
- [ ] Drag and drop files from Explorer into the page; in-page drag-and-drop uploads
- [ ] Page full screen, print / export PDF, desktop notifications, camera/microphone prompts
- [ ] Page zoom (Ctrl+wheel / `zoomFactor`) versus the app's own scaling policy

## 4. Performance (question c)

### 4.1 Engine and capability matrix

Embedded engine identity: `QtWebEngine/6.7.3 Chrome/118.0.5993.220` (Chromium 118, 2023-09).
Edge on this machine: 153 (2026). **That is roughly 2.5 years / 35 major versions behind.**

Measured features (in-page JS probing):

| Category | Result |
| --- | --- |
| Present | WebGL 1/2, `navigator.gpu` (API present), WebSocket/EventSource, Worker/SharedWorker/ServiceWorker, WebAssembly, `crypto.subtle`, `navigator.clipboard`, `Notification`, `toSorted`/`toReversed`, `Object.groupBy`/`Map.groupBy`, `structuredClone`, `Intl.Segmenter`, `scrollend`, CSS `:has` / container queries / `text-wrap: balance` / subgrid / `oklch`, `startViewTransition`, `showPopover` |
| **Missing** | `Promise.withResolvers` (Chrome 119+), `URL.canParse` (Chrome 120+) |
| **Missing** | **H.264 / H.265 decoding**: `canPlayType('video/mp4; codecs="avc1…"')` returns empty; VP9 / AV1 report available. Official Qt binaries ship free codecs only, so an MP4 embedded in a WebUI will not play (it does in Edge) |
| Other differences | UA reports `Windows NT 6.2` with brands `Not=A?Brand 99, Chromium 118`; no Chrome/Edge extensions or account features |

> The `NT 6.2` UA and the brand string are QtWebEngine quirks. A WebUI that gates on browser version
> (say, "requires Chrome ≥ 120") will misjudge — a risk to check tool by tool.

### 4.2 Resource footprint (measured)

| Scenario | Processes | Working set | Private bytes |
| --- | --- | --- | --- |
| Today: AgentLauncher 0.3.0 | 1 | 181 MB | 161.5 MB |
| Probe (minimal Qt Quick app) + 1 view (blank page) | 2 | 347.8 MB (app) + 78.9 MB (renderer) | 290 + 29.7 MB |
| Same, OpenCode WebUI loaded | 2 | 324.2 + 102.4 MB | 263.7 + 45 MB |
| Same, benchmark page (heavy DOM) | 2 | 301.3 + 111.6 MB | 249.4 + 61.7 MB |
| Edge (fresh profile, one tab, same benchmark page) | 16–18 | 674 MB – 1.1 GB | 388 – 631 MB |

So QtWebEngine is **lighter than launching an Edge instance** (2 processes vs. 16–18), but Chromium's
browser/GPU/network/storage logic runs **inside the application process** — exactly one
`--type=renderer` child was observed, with no separate GPU/utility children. Each embedded view
therefore adds roughly 250–350 MB on top of the launcher's 181 MB.

The renderer is separate, so a page crash does not take the app down; a GPU driver fault, however, is
**not** isolated the way it is in a browser.

### 4.3 Same-page A/B benchmark

One benchmark page (simulating a chat UI: streamed token appends, re-render of 1200 nodes, JSON
round-trips, regex transforms, forced layout thrash, sorting 200k numbers, scrolling, frame rate),
median of 2–3 runs per side, in ms:

| Benchmark | QtWebEngine 118 (visible) | Edge 153 (occluded) |
| --- | --- | --- |
| Stream 3000 DOM updates | 1.8 / 1.9 | 1.6 – 2.1 |
| Render 1200 nodes | 1.7 | 1.5 – 2.6 |
| JSON round-trip ×20 (~300 KB) | 14.1 / 15.3 | 13.4 – 20.1 |
| Regex transforms ×10 | 20.3 / 20.8 | 16.7 – 22.6 |
| **Forced layout thrash ×2000** | **1129 / 1243** | **580 – 656** |
| Sort 200k numbers | 37.8 / 43.1 | 68.7 – 140.3 |
| Scroll 60 steps | 0.1 / 0.2 | 0 – 0.2 |
| Frame rate (1.5 s window) | **60 fps** | 0–1 (throttled, meaningless) |
| Page load to `LoadSucceeded` | 0.42 – 0.97 s | same order |

**Method and bias (important):** the Edge window could not be brought to the foreground in this
environment (`SetForegroundWindow` returned false; the page reported `document.hidden = true`), so its
CPU numbers are affected by background priority/throttling and its frame rate is unusable — treat the
Edge column as a lower bound. The defensible conclusions are:

1. DOM/JS throughput is within noise of each other (both are Chromium); a typical chat-style WebUI
   **will not feel slower** than in Edge.
2. The one consistent gap is **forced layout being ~2× slower** (Blink gained a lot of layout/style
   recalc optimisation between 118 and 153) — the most likely place an old engine is noticed.
3. A visible embedded view holds a steady **60 fps** (vsync-limited), with working GPU compositing
   and WebGL.

## 5. Costs and risks

| Item | Today | With embedding | Level |
| --- | --- | --- | --- |
| Portable package (zip / extracted) | 35 MB / 88 MB | ~110–130 MB / 250–265 MB | Medium (download experience) |
| Memory per view | — | +250–350 MB working set | Medium (adds up with several views) |
| Build toolchain | MSVC or MinGW | **MSVC only** (no WebEngine for MinGW) | Low (the kit is already msvc2019_64) |
| Engine version | — | Chromium 118, ~2.5 years behind; no H.264; a few newer JS APIs missing | Medium (verify per tool) |
| Security updates | — | Chromium 118's security fixes stop at 2023-09; low risk for local content, re-assess if the view ever loads remote content | Low–Medium |
| GPU dependency | plain Qt Quick | WebEngine needs a working GPU/OpenGL; RDP, old drivers and software-rendering setups may need `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu` or a software OpenGL fallback | Medium |
| Packaging | `--qmldir qml` already covers it | Verify per 2.4; `--no-translations` ships only the en-US locale | Low |

## 6. Recommended approach

**Recommended: embed with QtWebEngine, and always keep "open in browser" as an escape hatch.**

- The escape hatch is not optional: OAuth logins, browser-extension needs and H.264 playback still
  require a real browser.
- On crash / load failure / `renderProcessTerminated`, the card should route the user to that path.
- If size or engine freshness ever becomes a hard constraint, the alternative is **Edge WebView2**:
  about +2 MB, engine always current with Edge, H.264 included — but it is a heavyweight HWND control
  that is awkward to overlay or animate inside a Qt Quick scene, and it has no QML API. Note that
  **Qt's own `QtWebView` module saves nothing on 6.7.3**: `plugins/webview/` contains only
  `qtwebview_webengine.dll`, i.e. a wrapper over WebEngine.
- **Either way, upgrade Qt to 6.9 / 6.10 first**: Chromium 130 / 134
  (source: <https://wiki.qt.io/QtWebEngine/ChromiumVersions>), cutting the version gap from 35 major
  versions to about 20 for nearly zero code change.

## 7. Remediation plan overview

The remediation work is split into 10 tasks covering the issues in sections 3.x and 5. The full task
list (files, steps, acceptance criteria) is in
`docs/superpowers/plans/2026-09-26-webengine-embedding-integration.md`:

| # | Task | Problem solved | Acceptance |
| --- | --- | --- | --- |
| 1 | Make WebEngine a build-time option; fix init order | `initialize()` before `QGuiApplication`; keep MinGW / no-WebEngine builds working | Both configurations start; the test target is unaffected |
| 2 | One `WebEngineProfile` per agent + storage dir | Cross-port cookie sharing (3.1) | No cookie bleed between ports; localStorage policy as designed |
| 3 | Take over new windows, downloads, dialogs, full screen | 3.2 defaults | External links / OAuth have a defined outcome; downloads land visibly |
| 4 | Shortcuts, focus, find-in-page | 3.3 | F5/Esc/Ctrl+W behave as designed; find works |
| 5 | View lifetime and memory budget | 3.4, 4.2 | Memory falls after leaving a page; a cap on live views |
| 6 | Packaging: MSVC guard, locales, size budget, deployment check | 2.4, section 5 | Deployed folder runs with no Qt in the environment; zip size recorded |
| 7 | Capability guardrails: fallback on failure, engine-version notes | 4.1, section 5 | A WebUI that fails to start gives the user a clear next step |
| 8 | Manual verification matrix (IME/DPI/clipboard/DnD/full screen/notifications/print) | 3.5 | Checklist signed off item by item |
| 9 | Fix the OpenCode port conflict | see below | New default port is free; existing installs pick the change up |
| 10 | Docs and changelog | — | AGENTS.md / README / CHANGELOG updated |

> **Separate problem found along the way:** OpenCode's default port `4096` is taken on this machine by
> the VS Code Kilo Code extension (`kilo.exe`), so `opencode web --port 4096` fails immediately
> (`ServeError`; port 4199 works). The fix is to edit `webUrl` and `command` in
> `config/default_agents.json` and rebuild: built-in agents are regenerated from the bundled default on
> every start (`AgentConfig::withBuiltinDefaults()`), so the change reaches existing installs. Agents
> the user added in the Settings page do not take that path and must be edited there.

### 7.1 Decisions to make

1. **Qt version**: stay on 6.7.3 (Chromium 118) or move to 6.9/6.10 (Chromium 130/134)?
2. **Default action**: click a card → embed, or ask "embedded / open in browser"? Where does the
   fallback entry point live?
3. **View lifetime**: destroy the view on leaving, or freeze it (`Frozen`) to keep session state?
4. **Do we need a no-WebEngine portable variant** (and what is the compile-time default)?

## Appendix A: Reproducing this

The probe project lives outside the repository at `C:\src\Qt\_al_webengine_probe` (deletable; the
files below are enough to rebuild it).

| File | Purpose |
| --- | --- |
| `CMakeLists.txt` / `main.cpp` / `main.qml` | The probe app (URL, screenshot path, quit delay, optional second URL) |
| `caps.html` | Rendering and capability probe page |
| `fair.html` + `bench-server.js` | Same-page A/B benchmark and its report server |
| `set.html` / `echo.html` / `cookie-server.js` | Cross-port cookie / localStorage isolation experiment |
| `build.bat` | MSVC + Ninja build |
| `activate.ps1` / `measure.ps1` / `edge-focus.ps1` | Window raising, process memory measurement, window-state diagnostics |

```bash
# Build (MSVC 2019 + Ninja + Qt 6.7.3)
cd C:/src/Qt/_al_webengine_probe && cmd //c build.bat

# Load any URL: <url> <screenshot path> <quit after ms> [second URL]
export PATH="/c/Qt/6.7.3/msvc2019_64/bin:$PATH"
./build/al_probe.exe "http://127.0.0.1:4199/" shot.png 25000

# Deployed size
cp build/al_probe.exe deploy/ && C:/Qt/6.7.3/msvc2019_64/bin/windeployqt.exe \
    --release --no-translations --no-system-d3d-compiler --qmldir . deploy/al_probe.exe
```

## Appendix B: Data provenance and uncertainty

- **Directly reproducible**: engine identity, capability matrix, load timings, memory, process layout,
  deployed size, cross-port cookie behaviour, standalone run of the deployed bundle.
- **Single machine, single run, directional**: the benchmarks in 4.3 (including the throttling bias on
  the Edge side).
- **Unverified, needs a human**: the checklist in 3.5 (IME, fractional DPI, clipboard, drag-and-drop,
  full screen, notifications, printing).
- **Inference to re-verify**: QtWebEngine's background throttling (based on shared Chromium logic plus
  the Edge measurement; not separately reproduced on the Qt side).

## Appendix C: The Qt 5.15 engine era gap (addendum 2026-09-29)

> Trigger: on a machine whose only Qt is 5.15.16 LTS, **every** embedded agent WebUI stayed blank
> while the same pages worked in the system browser. Evidence: `~/.AgentWorkbench/log/agentworkbench.log`
> `[js]` CRITICAL lines plus the on-disk frontend bundles.

Qt 5.15.16 embeds **Chromium 87.0.4280.144** (`<Qt>/Src/qtwebengine/src/3rdparty/chromium/chrome/VERSION`),
versus Chromium 118 for Qt 6.7.3 used in the main study. Agent frontends built for modern browsers fail
in two distinct ways on 87:

| Failure class | Observed instance | Missing feature (Chrome since) | Polyfillable? |
| --- | --- | --- | --- |
| Runtime API gap | qwen-code: `mte.at is not a function` | `Array/String.prototype.at` (92) | yes |
| Runtime API gap | kimi-code: `e.toSorted is not a function` | `Array.prototype.toSorted` (110) | yes |
| Runtime API gap | dsh inline bootstrap: `Promise.withResolvers is not a function` | `Promise.withResolvers` (119) | yes |
| **Syntax gap** | dsh main bundle: `Uncaught SyntaxError: Unexpected token '{'` | class static initialization blocks (94) | **no** |

The syntax gap is the hard limit: a bundle containing `static { ... }` fails to *parse* on Chromium 87,
and no injected script can repair that. There is no V8 flag for it in 8.7 either.

Remediation shipped in `fix/web-js-compat`:

- `src/web/webengine/compat-polyfills.js` — feature-guarded polyfills for the runtime gaps above
  (plus `findLast(Index)`, `toReversed/toSpliced/with`, `Object.hasOwn`, `Object/Map.groupBy`,
  `structuredClone`, `AbortSignal.timeout/any`, `crypto.randomUUID`, `URL.canParse`, `Response.json`).
  Injected MainWorld + DocumentCreation: Qt 6.8+ per-profile via the inherited
  `QWebEngineProfile::scripts()`; Qt 6.2–6.7 and Qt 5 per-view (the Quick profile's
  `QQuickWebEngineScriptCollection` only becomes usable after a QML engine is attached — inserting at
  profile creation hits its `Q_ASSERT(engine)`, observed on 6.7.3; the Qt 5 Quick profile has no
  collection at all), appended from `Component.onCompleted` — early enough because Qt 5 defers adapter
  initialization with `singleShot(0)` and binds user scripts in `initializationFinished()`.
- Blank-page fallback in `WebEngineSurface.qml` — when a load ends with uncaught JS exceptions *and*
  the body is still blank (no text, no canvas/svg/img/video/iframe), the tab drops to the `error`
  state whose overlay offers "Open in browser". This is the honest outcome for syntax-gap bundles
  (dsh on Chromium 87): they cannot run in the embedded engine at all.
- The surface also took over `javaScriptConsoleMessage`: connecting the signal suppresses Qt's default
  `[js]` log routing on both majors, so the surface re-logs Warning+Error itself — with `token=`
  values redacted, which the default route did **not** do (tokens leaked into the log file).

Remaining risk on Chromium 87: further unpolyfillable syntax (top-level await, regex `/d` flag,
private brand checks) and unfillable CSS (`:has()`, container queries, nesting, `color-mix()`).
Bundles that parse may still degrade visually. The durable fix for the Qt 5 route is a newer engine;
polyfills only extend Chromium 87's reach for runtime APIs.
