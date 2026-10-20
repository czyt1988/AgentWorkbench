# WebEngine crash troubleshooting

What the embedded browser's crash signals mean, how to capture the engine's own verdict, and the
causes seen in the wild. Read this before suspecting host-side code when an embedded tab dies.

User view: [Agent Web UI](../guide/web-ui.md) and [Troubleshooting](../guide/troubleshooting.md).
Implementation: [WebEngine adapter](webengine-adapter.md) and [Web tabs](web-tabs.md).

## First fact: the renderer is not the application

An embedded tab is rendered by a separate Chromium process — `QtWebEngineProcess.exe`, or
`QtWebEngineProcessd.exe` for Debug builds (see below). When it dies, the host application keeps
running and `WebEngineView.renderProcessTerminated(status, exitCode)` fires; the surface logs
`WebEngine: render process terminated <status> <exitCode>` and moves the tab to the `crashed`
state. The overlay never auto-reloads: a crash loop is worse than a manual reload.

Browser-process code (everything in `awb_web`, `awb_web_webengine` and the surface QML) cannot
corrupt the renderer's memory. Host-side changes can only be the cause if they run **inside** the
page — injected scripts (`compat-polyfills.js`) or `runJavaScript` calls.

## Reading status and exit code

`status` is `WebEngineView.RenderProcessTerminationStatus` (the core enum
`WebContentsAdapterClient::RenderProcessTerminationStatus` has the same order):

| Value | Name | Qt maps it from (Chromium `base::TerminationStatus`) |
|---|---|---|
| 0 | `NormalTerminationStatus` | `NORMAL_TERMINATION` |
| 1 | `AbnormalTerminationStatus` | `ABNORMAL_TERMINATION`, `LAUNCH_FAILED` |
| 2 | `CrashedTerminationStatus` | `PROCESS_CRASHED`, `INTEGRITY_FAILURE` (Windows) |
| 3 | `KilledTerminationStatus` | `PROCESS_WAS_KILLED`, `OOM` |

`exitCode` is the raw OS exit status. On Windows, Chromium's `base::GetTerminationStatus`
(`base/process/kill_win.cc`) buckets it: 0 → normal; the external-kill codes (0x1 task manager,
0x40010004 debugger-terminated, 0xC000013A control-C, 0xC0000354 debugger-inactive) → was-killed;
the OOM codes (7012 sandbox memory limit, `kOomExceptionCode`) → OOM; **everything else →
crashed**.

**Do not over-trust the status name.** Observed in this project (2026-10-20, Qt 6.7.3): a renderer
suicide proven by stderr to be a Blink `DCHECK` (the confirmed case below) surfaced as `status 3`
(KilledTerminationStatus) with `exitCode -2147483645` — while the source chain above predicts
status 2 for that exit code. Somewhere in the Windows child-process-launcher path the bucket gets
reclassified. The reliable signals are the **exit code** and the **stderr FATAL line**:

| exitCode | Meaning |
|---|---|
| `-2147483645` = `0x80000003` | `STATUS_BREAKPOINT` — Chromium hit a `CHECK`/`DCHECK` and terminated itself deliberately |
| `-1073741819` = `0xC0000005` | `STATUS_ACCESS_VIOLATION` — a real memory fault in the engine |
| `-1073740791` = `0xC0000409` | `STATUS_STACK_BUFFER_OVERRUN` — stack smashing / fast-fail |
| `1`, `0x40010004`, … | killed externally (task manager, `taskkill`, debugger) — not a bug |

## Debug vs Release: the DCHECK split (read this first)

Qt ships **two** WebEngine renderer binaries side by side in `<Qt>/bin`: `QtWebEngineProcess.exe`
(Release) and `QtWebEngineProcessd.exe` (Debug). A Debug build of the application loads
`Qt6WebEngineCored.dll` and spawns the **d** variant — a Chromium with `DCHECK_IS_ON()`. Every
`DCHECK` in Blink, V8 and the content layer is live, and a failed `DCHECK` is a fatal, deliberate
process suicide (`STATUS_BREAKPOINT`).

Consequence: **a page that crashes only in Debug builds is, in the overwhelming majority of cases,
tripping a Chromium self-verification assertion — not an application bug.** The same page in a
Release build skips the verification entirely (the code is compiled out) and runs, at worst with a
subtle rendering difference.

Which build a log session came from is visible in the log itself: the skill scanner logs its
"project directory" roots, derived from the executable's location —
`SkillScanner: root <build-dir>/.agents/skills ...`. A Qt Creator Debug session shows the IDE's
shadow-build directory (e.g. `build-AgentWorkbench-Qt_..._-Debug`); a `scripts/build.sh --release`
session shows `build-release`.

## Capturing the engine's verdict (stderr)

The app log records only status and exit code. Chromium's own explanation —
`FATAL:<file>(<line>) Check failed: <expression>` — goes to **stderr**, which a GUI launch
discards. To capture it, launch the application through a batch file that:

1. loads the MSVC environment (`vcvars64.bat`) — needed for the Debug CRT;
2. puts `<Qt>/bin` on `PATH`;
3. sets `QTWEBENGINE_CHROMIUM_FLAGS=--enable-logging=stderr`;
4. runs `<build-dir>/AgentWorkbench.exe > <log-file> 2>&1`.

Reproduce the crash, then search the log file for `FATAL` / `Check failed`. The FATAL line names
the Chromium source file and the failed expression — usually enough to identify the subsystem
(style, compositor, V8, network…) without further digging. Two caveats: the file name in the FATAL
line can be display-truncated (match it by the expression, not the name), and Chromium line numbers
are specific to the exact version Qt embeds (118 for Qt 6.7.3, 87 for Qt 5.15).

## Confirmed case: Chromium 118 style-engine DCHECK (Debug only)

Symptom (2026-10-20, qwen web shell 0.25.2): the tab loads fine (`[web-load] succeeded`, state
`ready`), then dies 10–45 s later while idle; the host log is silent in the crash window; the crash
is deterministic across reloads; the Release build is unaffected (verified — the same page ran for
minutes in Release).

stderr verdict (verbatim; the file name is display-truncated — it is
`third_party/blink/renderer/core/css/resolver/style_resolver.cc`, line number as of Chromium 118):

```text
FATAL:er.cc(2350)] Check failed: g_null_atom == ComputeBaseComputedStyleDiff(applied_style, *correct_style) (<null> vs. "Field diff: box_shadow_(was ..., should be ...)")
```

The check lives inside `#if DCHECK_IS_ON()` in `StyleResolver::ApplyPropertiesFromCascade`: Blink
verifies that its "apply inherited-only properties from the cache" fast path produces the same
`ComputedStyle` as a full cascade apply. The qwen shell's CSS makes the `box_shadow` field diverge
— a genuine Chromium 118 bug — and the Debug engine turns the failed verification into a fatal
crash.

Handling: use a Release build for daily driving; during Debug development sessions, open that
agent in the external browser. The root fix is upgrading Qt (a newer Chromium with the upstream bug
fixed). Do not chase host-side code, do not clear the profile, do not disable the service worker —
none of them are involved.

## Other candidate causes (checklist)

Not yet observed in this project, but the standard suspects when the stderr FATAL points elsewhere
(or there is no FATAL at all):

- **GPU / compositing** — crashes or blank pages tied to graphics drivers. Mitigation:
  `web.chromiumFlags` with `--disable-gpu` (the setting feeds `QTWEBENGINE_CHROMIUM_FLAGS` and
  takes effect on the next restart).
- **Renderer OOM** — many live tabs with heavy agent UIs. Bounded by `web.maxLiveTabs` (LRU release
  of non-active tabs) and optionally `web.freezeInactiveTabs`. The exit code lands in the OOM
  bucket, not `STATUS_BREAKPOINT`.
- **Page-side engines** — WASM, workers and audio worklets in modern agent shells against the
  embedded Chromium (118 on Qt 6.7.3; **87** on Qt 5.15). Syntax-level gaps show up as blank pages
  first — the surface's blank-screen probe covers those (see the adapter doc); deeper
  incompatibilities can kill the renderer.
- **Injected script bugs** — the only host-side code that runs inside the renderer is
  `compat-polyfills.js` (DocumentCreation, MainWorld). It must stay parseable by Chromium 87 (see
  the adapter doc's polyfill rules); a syntax error there breaks pages on the old engine.
- **External kills** — task manager, `taskkill`, a detaching debugger: exit code 1 or 0x40010004.
  Not a bug at all.

## Diagnostic playbook

1. **Locate the crash in the log**: the `render process terminated <status> <exitCode>` line plus
   the `WebTabs: tab <id> state ... -> crashed` transition. Note the time distance to the last
   `[web-load]`/`[web-nav]` lines — a crash during idle means no host code was executing (the
   surface only acts on load, navigation and console events).
2. **Identify the build** (Debug vs Release) via the skill-scan fingerprint. Debug +
   `STATUS_BREAKPOINT` ⇒ almost certainly a Chromium DCHECK; re-test on a Release build before any
   deeper digging.
3. **Capture stderr** (recipe above), reproduce, and read the FATAL line — it names the subsystem.
4. **A/B before touching code**: reproduce on a clean `dev` build. Renderer crashes are
   page/engine-side until proven otherwise.
5. **Check `web.chromiumFlags`** — a stale or experimental flag in the user's settings can produce
   crashes that no build reproduces.

## Related

- [WebEngine adapter](webengine-adapter.md) — the surface, profiles, polyfills, blank-screen probe
  and load logging.
- [Web tabs](web-tabs.md) — the tab state machine; the `crashed` state never auto-reloads.
- [Settings](settings.md) — `web.chromiumFlags`, `web.maxLiveTabs`, `web.freezeInactiveTabs`.
- Research: [WebEngine embedding](../research/webengine-embedding.md).
- [Development index](index.md).
