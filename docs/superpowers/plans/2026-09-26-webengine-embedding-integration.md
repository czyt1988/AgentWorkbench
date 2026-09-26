# WebEngine Embedding Integration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed each agent's web UI inside AgentLauncher with Qt WebEngine, behind a per-agent
isolated browser profile, with the interaction pitfalls found in the research closed off and an
"open in browser" escape hatch always available.

**Architecture:** A new optional QML page (`WebUiPage.qml`) pushed onto the existing `StackView`,
hosting one `WebEngineView` per open agent. Profiles come from a C++ `WebProfiles` manager that hands
out one persistent `WebEngineProfile` per agent id, stored under `~/.AgentLauncher/webengine/<id>`.
The launcher keeps its current external-open path untouched as the fallback.

**Tech Stack:** Qt 6.5+ (QML/Quick Controls 2, Basic style), Qt WebEngine (Qt WebEngine Quick), C++17,
CMake + Ninja/MSVC, QtTest for C++ unit tests.

**Research:** `docs/research/webengine-embedding.md` (English) / `docs/zh/research/webengine-embedding.md`
(中文). All numbers quoted below come from that document.

## Global Constraints

- Build: `cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"` then `cmake --build build`
  (MSVC developer prompt / after `vcvars64.bat` — see AGENTS.md).
- **MSVC only once WebEngine is linked** (Qt ships no MinGW WebEngine). The packaging script already
  prefers `msvc*` kits — keep it that way and make the requirement explicit.
- All user-visible strings: English source wrapped in `tr()` / `qsTr()`. Chinese translations go ONLY
  into `translations/agentlauncher_zh_CN.ts`.
- Dark theme colors only (Catppuccin Mocha: bg `#1e1e2e`, surface `#313244`, border `#45475a`, text
  `#cdd6f4`, muted `#a6adc8`/`#7f849c`, accent blue `#89b4fa`, red `#f38ba8`, green `#a6e3a1`,
  yellow `#f9e2af`).
- No hard-coded agent definitions in C++ — everything still comes from `agents.json`, and built-in
  agents come from `config/default_agents.json` alone.
- No config compatibility or migration code (AGENTS.md rules it out explicitly and none exists today).
- Comments/identifiers/log messages in English.
- **Per AGENTS.md: do NOT run `git commit` or `git push` unless the user explicitly asks.**
- Keep `tests/tst_core.cpp`'s source list free of WebEngine: do not add WebEngine includes to
  `AgentConfig.cpp`, `AgentModel.cpp` or `AgentLauncher.cpp`. Path helpers that tests need go in
  `AgentConfig` (testable, WebEngine-free); only `WebProfiles` touches WebEngine types.
- Runtime versions to respect: Qt 6.7.3 → Chromium 118 (see research §4.1). Do not assume APIs that
  landed after Chromium 118 and do not promise Chrome-parity behaviour.

## Decisions (confirm before starting)

| # | Decision | Recommendation |
| --- | --- | --- |
| D1 | Qt version: stay 6.7.3 (Chromium 118) or upgrade to 6.9/6.10 (Chromium 130/134)? | Upgrade first — nearly zero code change, removes ~15 major versions of gap and the worst modern-API risk |
| D2 | Card click: embed by default, or ask "embedded / browser"? | Embed by default, with a header button "Open in browser" always visible on the embedded page |
| D3 | View lifetime after leaving the page | Freeze (`Frozen`) while the agent is running, destroy when it is not; see Task 5 |
| D4 | Ship a no-WebEngine portable variant? | Yes: compile-time option defaulting to ON, so MinGW/CI builds and a small portable variant stay possible |

## File Structure

```
CMakeLists.txt                  MODIFY  option(ENABLE_WEBVIEW), conditional WebEngineQuick link,
                                        conditional QML resource + translation entries
src/main.cpp                    MODIFY  QtWebEngineQuick::initialize() before QGuiApplication,
                                        expose webViewEnabled to QML, extend load-failure diagnostics
src/AgentConfig.h/.cpp          MODIFY  webEngineDataDir() / webEngineProfileDir(agentId) path helpers
src/WebProfiles.h/.cpp          CREATE  one persistent WebEngineProfile per agent id + clearStorage()
src/AgentLauncher.h/.cpp        MODIFY  openExternal(url), detectedPortOwner(port) for the port-conflict
                                        message, optional webUrlFor(id) so QML reuses the token URL
qml/WebUiPage.qml               CREATE  WebEngineView host: header, error/fallback UI, find bar,
                                        shortcut handling, popup/download handling
qml/main.qml                    MODIFY  route a card click to WebUiPage when ENABLE_WEBVIEW and the
                                        agent is running; keep a browser-open action
qml/SettingsPage.qml            MODIFY  "Clear browser data" + engine diagnostics row
config/default_agents.json      MODIFY  free port for OpenCode (Task 9)
scripts/package.sh              MODIFY  MSVC guard when WebEngine is built in, size reporting
tests/tst_core.cpp              MODIFY  tests for the new AgentConfig path helpers
translations/agentlauncher_zh_CN.ts MODIFY  fill Chinese for all new strings
AGENTS.md                       MODIFY  document the embedded-view path and the new data directory
CHANGELOG.md / CHANGELOG-zh.md  MODIFY  record the feature and the size/engine trade-offs
```

---

### Task 1: Make WebEngine a build-time option and initialize it correctly

**Files:**
- Modify: `CMakeLists.txt`, `src/main.cpp`

**Interfaces:**
- Produces: CMake option `ENABLE_WEBVIEW` (default `ON`); compile definition `ENABLE_WEBVIEW`;
  context property `webViewEnabled` (bool) for QML.

- [ ] Add `option(ENABLE_WEBVIEW "Embed agent web UIs with Qt WebEngine" ON)` near the test option.
- [ ] When ON: `find_package(Qt6 6.5 REQUIRED COMPONENTS WebEngineQuick)` and link
  `Qt6::WebEngineQuick`; when OFF, leave the target exactly as today. Build the QML/translation
  resource lists from a variable so `qml/WebUiPage.qml` is only embedded when ON.
- [ ] In `src/main.cpp`, include `<QtWebEngineQuick/QtWebEngineQuick>` behind `#ifdef ENABLE_WEBVIEW`
  and call `QtWebEngineQuick::initialize()` **before** `QGuiApplication app(argc, argv)` is
  constructed (Qt requires this ordering; see research §2.3). Keep `Logger::install()` where it is.
- [ ] Expose `engine.rootContext()->setContextProperty("webViewEnabled", true/false)` so QML can hide
  the embedded entry point in OFF builds.
- [ ] Extend the existing QML-load-failure diagnostics in `main.cpp` to also report whether
  `qml/QtWebEngine` and `QtWebEngineProcess.exe` are present next to the executable when ON.
- [ ] Verify: configure and run both variants (`-DENABLE_WEBVIEW=ON` / `=OFF`); OFF must behave
  exactly as 0.3.0 (cards, health checks, external open). Confirm `AgentLauncherTests` still builds
  and passes in both.

### Task 2: One browser profile per agent (cookie isolation)

Root cause from research §3.1: cookies are host-scoped and port-blind, so all agents on `127.0.0.1`
share one cookie jar under the default profile. Fix: a profile per agent.

**Files:**
- Create: `src/WebProfiles.h`, `src/WebProfiles.cpp`
- Modify: `src/AgentConfig.h/.cpp` (path helpers), `CMakeLists.txt`, `qml/WebUiPage.qml` (consumer)

**Interfaces:**
- `static QString AgentConfig::webEngineDataDir();` → `<userDataDir>/webengine`
- `static QString AgentConfig::webEngineProfileDir(const QString &agentId);` → `<userDataDir>/webengine/<sanitized id>`
  (sanitize the id: keep `[A-Za-z0-9._-]`, replace the rest with `_`; reuse the same rules for any
  future on-disk per-agent state). Both helpers must respect `QStandardPaths` test mode, like
  `userDataDir()` does today.
- `class WebProfiles : public QObject` with `Q_INVOKABLE QQuickWebEngineProfile *profileFor(const QString &agentId);`
  and `Q_INVOKABLE void clearStorage(const QString &agentId);` — lazily creates one profile per id,
  parented to the manager, `setOffTheRecord(false)`, `setStorageName(agentId)`,
  `setPersistentStoragePath(dir)`, `setCachePath(dir + "/cache")`,
  `setPersistentCookiesPolicy(AllowPersistentCookies)`, creates the directories first.
- Context property `webProfiles` for QML.

- [ ] Add the two `AgentConfig` path helpers and unit-test them in `tests/tst_core.cpp` under the
  existing test-mode setup (assert the path is under the test-mode location, and that a hostile id
  such as `../evil` cannot escape the directory).
- [ ] Add `src/WebProfiles.*` to `APP_SOURCES` only when `ENABLE_WEBVIEW` is ON.
- [ ] In `qml/WebUiPage.qml`, assign `profile: webProfiles.profileFor(agentId)` **before** the view
  loads (setting it afterwards does not move already-loaded content to the new jar).
- [ ] Add a Settings entry "Clear browser data" that calls `clearStorage()` and warns that the agent's
  web session will be signed out; run it only while no view for that agent is open.
- [ ] Verify with the research harness (`cookie-server.js`, ports 8741/8742): with per-agent profiles,
  a cookie set through one agent's view must NOT reach another port's view.
- [ ] Verify that a WebUI login survives an app restart (persistence) and that
  `~/.AgentLauncher/webengine/<id>` is created with the expected content.

### Task 3: Take over popups, downloads, dialogs and full screen

Root cause from research §3.2: unhandled `newWindowRequested` fails the load outright; unhandled
downloads never reach disk.

**Files:**
- Modify: `qml/WebUiPage.qml`, `src/AgentLauncher.h/.cpp`

**Interfaces:**
- `Q_INVOKABLE void AgentLauncher::openExternal(const QString &url);` — validates the scheme is
  `http`/`https` and calls `QDesktopServices::openUrl` (ignore anything else).
- Popup policy: same host+port as the agent's `webUrl` → `request.openIn(view)`; anything else →
  `openExternal(request.requestedUrl)`; always `request.accepted = true`.

- [ ] Implement `onNewWindowRequested` with the policy above.
- [ ] Implement `WebEngineProfile.onDownloadRequested`: `download.accept()` into
  `QStandardPaths::DownloadLocation` with a sanitized file name; show a QML toast with "Open folder"
  on `stateChanged == DownloadCompleted`, and an error toast otherwise. Never leave the request
  unhandled.
- [ ] `onFullScreenRequested`: `request.accept()` and switch the page into a full-window state (header
  hidden); `Esc` leaves full screen.
- [ ] Leave `authenticationDialogRequested` / `javaScriptDialogRequested` / `fileDialogRequested`
  unhandled so Qt's default dialogs appear — but add a one-line comment in the QML recording that this
  is a deliberate choice (research §3.2).
- [ ] Verify by hand: a page with `<a target="_blank">`, a `window.open` popup, a `<a download>` link,
  a page full-screen request, and an HTTP basic-auth endpoint. Each must have a defined outcome.

### Task 4: Shortcuts, focus and find-in-page

Root cause from research §3.3: the view swallows keys the app may want.

**Files:**
- Modify: `qml/WebUiPage.qml`

- [ ] Add application-level `Shortcut`s and document the mapping in a comment block:

  | Key | Action |
  | --- | --- |
  | `Esc` | leave full screen, otherwise return to the home page |
  | `F5` / `Ctrl+R` | reload the view |
  | `Ctrl+F` | toggle a find bar driven by `view.findText(text, flags)` |
  | `Ctrl+0` / `Ctrl+=` / `Ctrl+-` | reset / increase / decrease `view.zoomFactor` |
  | `Ctrl+W` | return to the home page (does not stop the agent) |

  Keys not listed here stay with the page. Note in the comment that Ctrl+W/Ctrl+P may be remapped
  after user feedback.
- [ ] `view.forceActiveFocus()` when the page becomes current, so typing works without an extra click;
  clear focus when leaving.
- [ ] Verify that the shortcuts still fire while the `WebEngineView` has focus; if the shortcut system
  loses to the page, fall back to a `Shortcut` on the window plus a `Keys.onPressed` filter and record
  what was needed.
- [ ] Verify IME input reaches the page (also Task 8).

### Task 5: View lifetime and memory budget

Root cause from research §3.4 and §4.2: a live view costs ~250–350 MB working set; hidden views keep
their state but stop rendering.

**Files:**
- Modify: `qml/WebUiPage.qml`, `qml/main.qml`

- [ ] Define and implement the policy: while the agent's health check reports running, hide → freeze
  (`view.lifeCycleState = WebEngineView.LifecycleState.Frozen`; confirm the scoped-enum spelling in
  the installed `qml/QtWebEngine/plugins.qmltypes`); when the agent stops, or the user closes the page,
  destroy the view and release the renderer.
- [ ] Cap concurrent live views (suggested: 1 open page at a time via the existing `StackView`, plus at
  most one frozen view per running agent). Log a warning if the cap is exceeded.
- [ ] Leaving the page must not stop the agent or drop a running task: confirm on a real agent that a
  long response still completes while the page is frozen and is still visible after coming back.
- [ ] Measure before/after with `measure.ps1` from the research harness and record both numbers (baseline
  launcher 181 MB WS; per-view +250–350 MB WS) in the PR description or changelog.
- [ ] Verify there is no leak after open → leave → destroy → reopen (process count returns to 2).

### Task 6: Packaging — MSVC guard, locales, size budget, deployment check

**Files:**
- Modify: `scripts/package.sh`, `CMakeLists.txt`, `AGENTS.md`

- [ ] Fail early in `scripts/package.sh` when `ENABLE_WEBVIEW=ON` and the detected Qt kit is not
  `msvc*`, with a message naming the reason (Qt ships no MinGW WebEngine).
- [ ] Keep `--no-translations` in the `windeployqt` call — it is what keeps
  `qtwebengine_locales` down to `en-US.pak` (404 KB instead of 33 MB; research §2.4). Note that
  `qtwebengine_en.qm` etc. are still copied and are harmless. Add a comment explaining why the flag
  must stay, so nobody "fixes" it away.
- [ ] Confirm `--qmldir qml` picks up the new `WebUiPage.qml` import (the scanner must pull
  `qml/QtWebEngine`); if it does not, add the module explicitly.
- [ ] Print the extracted and zipped sizes at the end of the packaging script and record them in the
  changelog (expected: zip ~110–130 MB, extracted ~250–265 MB, up from 35 MB / 88 MB).
- [ ] Smoke-test the produced bundle the way the research did: run it with a PATH containing no Qt
  paths and confirm the UI and an embedded view both work.
- [ ] Verify a no-WebEngine build (`-DENABLE_WEBVIEW=OFF`) still packages at the old size.

### Task 7: Capability guardrails (failure fallback, engine diagnostics)

Root cause from research §4.1 and §5: the engine is Chromium 118 — no H.264, a few newer JS APIs
missing, and an occasional WebUI may refuse to boot.

**Files:**
- Modify: `qml/WebUiPage.qml`, `src/AgentLauncher.h/.cpp`, `qml/SettingsPage.qml`

**Interfaces:**
- `Q_INVOKABLE QString AgentLauncher::webEngineInfo() const;` → e.g.
  `"Qt WebEngine 6.7.3 (Chromium 118.0.5993.220)"` built from `qWebEngineVersion()` /
  `qWebEngineChromiumVersion()`; returns a short "not built in" string when OFF.

- [ ] Header of `WebUiPage.qml`: agent name, an "Open in browser" button (always visible — this is the
  D2 escape hatch), reload, and "Clear browser data".
- [ ] `onRenderProcessTerminated` and `LoadFailedStatus` show an in-place explanation with two actions:
  "Reload" and "Open in browser". Include the error string so the log is actionable.
- [ ] Show a one-time, dismissible note the first time an embedded view is opened: the embedded browser
  is older than the user's browser and some WebUIs may not support it; the browser button is the way out.
  (String goes through `qsTr()`, translation into the `.ts`.)
- [ ] Add the engine version to the Settings/About area via `webEngineInfo()`.
- [ ] Optional (only if a real agent needs it): a per-agent `webUserAgent` config field to override the
  UA when a WebUI gates on browser version. If added, document it in `AGENTS.md` and the docs.

### Task 8: Manual verification matrix

Nothing here can be automated; run it once on a real machine and record results (tick, or note the
failure and file a follow-up).

- [ ] Chinese IME inside the view: candidates, inline composition, caret tracking.
- [ ] Fractional DPI (125% / 150%): text sharpness, hit targets, no blurry scaling after a DPI change.
- [ ] Copy/paste inside the view, and view ⇄ QML text fields in both directions.
- [ ] Drag a file from Explorer into the page; drag a file within the page (upload).
- [ ] Page full screen, print / export PDF, desktop notification prompt, camera/microphone prompt.
- [ ] Ctrl+wheel zoom and the `Ctrl+0/=/-` shortcuts agree with each other.
- [ ] Long response streamed by an agent while frozen (Task 5) replays/continues correctly.
- [ ] Two agents open in sequence keep separate sessions (Task 2) — e.g. log into both and confirm no
  logout, no cross-talk.
- [ ] RDP / software-rendering session: at minimum confirm the app either works or fails with a clear
  message; document the `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu` workaround if needed.

### Task 9: Fix the OpenCode port conflict

Root cause: `4096` is occupied on this machine by the VS Code Kilo Code extension, so
`opencode web --port 4096` exits with `ServeError` (research §7).

**Files:**
- Modify: `config/default_agents.json`, `src/AgentLauncher.h/.cpp`, `AGENTS.md`
- Modify: `docs/configuration.md` (+ `docs/zh/configuration.md`) if the port is mentioned there

**Interfaces:**
- Built-in agent definitions come from `config/default_agents.json` alone: `AgentConfig::load()`
  regenerates them via `withBuiltinDefaults()` on every start, so editing the bundled default reaches
  existing installs on the next launch. User-added agents keep their own values.

- [ ] Pick a verified-free port for the OpenCode entry (check
  `netstat -ano | grep LISTENING | grep :PORT` before choosing; candidates 4199 / 4299) and update both
  `webUrl` and `command` so they stay in sync. Do not touch the other agents' ports.
- [ ] Rebuild and confirm an existing `~/.AgentLauncher/agents.json` picks the new port up on the next
  start.
- [ ] Document in the `AGENTS.md` config section that a built-in's port belongs in
  `default_agents.json`, not in the Settings page: Settings-page edits to built-ins are overwritten on
  the next start by design. Do **not** add config-migration code — AGENTS.md explicitly rules that out.
- [ ] Improve the failure message: when a launch fails or the port never answers, use the existing
  `findPidsForPort()` to name the occupant (e.g. "port 4096 is already used by kilo.exe (PID 20644)")
  in the `launchFailed` popup. Keep the string in `tr()`.
- [ ] Verify: `opencode web` on the new port is reachable from the embedded view; a deliberately
  conflicting port produces the new, informative message.

### Task 10: Docs and changelog

- [ ] `AGENTS.md`: document the embedded-view path (new page, profile location
  `~/.AgentLauncher/webengine/`, MSVC-only requirement, the fallback to the browser, the
  `ENABLE_WEBVIEW` switch) next to the existing "Windows 下的启动" section.
- [ ] `README.md` / `README-zh.md`: mention the embedded view and the size/engine trade-off.
- [ ] `CHANGELOG.md` / `CHANGELOG-zh.md`: feature entry, plus the measured package sizes and the
  Chromium version at release time.
- [ ] Keep `docs/research/webengine-embedding.md` and its zh twin linked from the plan/changelog so the
  measurements can be re-checked after a Qt upgrade.

## Acceptance criteria

- [ ] `-DENABLE_WEBVIEW=OFF` builds and behaves exactly like 0.3.0; `=ON` builds and runs on MSVC.
- [ ] A running agent's WebUI opens inside the app, is interactive, and can be sent to the real browser
  in one click.
- [ ] Two agents on different ports never share cookies; each agent's session survives a restart.
- [ ] `target="_blank"`, downloads, full screen and page-crash each have a defined, visible outcome.
- [ ] Leaving the page releases memory (measure and record); no view leak after repeated open/close.
- [ ] The packaged zip runs on a machine with no Qt installed; its size is recorded in the changelog.
- [ ] `AgentLauncherTests` still passes; no WebEngine dependency leaked into the test target.

## Non-goals

- Replacing the HTTP health-check based running detection (it stays Qt-Network based, unchanged).
- Automating the IME / DPI / clipboard checks in CI — they stay a manual matrix (Task 8).
- Any agent-specific behaviour in C++: everything stayed config-driven (per AGENTS.md).
- Rewriting the existing external-open flow: it remains the fallback and keeps working as today.
