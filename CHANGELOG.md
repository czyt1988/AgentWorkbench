# Changelog

All notable changes to **AgentWorkbench** (called AgentLauncher up to 0.3.0) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Agent Tools page**: a prompt-composing workbench for agent CLI users —
  draft prompts on the left (Enter only inserts a newline, nothing is ever
  sent from the page; Copy puts the draft on the clipboard), browse the
  workspace on the right. Remembers up to 20 workspace folders (MRU,
  per-entry removal, native folder picker), renders the current workspace
  as a lazily-loaded file tree with auto-refresh (QFileSystemWatcher,
  debounced) and manual refresh, and inserts file references as
  `` `./relative/path` `` by dragging a tree row into the editor (at the
  drop-point cursor) or double-clicking a file row, and copies a row's
  relative or absolute path from its right-click menu. The draft survives
  page switches and restarts (`tools.json`).
- **Configurable file-tree icons**: 17 file-type and 8 folder-type icons
  (`icons/filetypes/`, `icons/foldertypes/`), mapped by file name and suffix
  from `config/default_file_icons.json`; `<dataRoot>/file_icons.json`
  overrides or extends the mapping key by key, so supporting a new suffix
  needs no code change.
- **Web page "Home" button**: the tab-bar toolbar gains a home entry that
  brings back the running-agent list (the no-tabs page) without closing any
  open tab — previously there was no way back to the list once a view was
  open, so agents started later could not be opened from the WebUI page.
  Clicking a tab, opening an agent from the list or cycling `Ctrl+Tab`
  leaves it.
- **Launcher card split button**: while an agent runs, "Open" opens the
  WebUI **in-app and navigates to the web page** (previously the tab opened
  in the background and the user had to switch pages manually); a chevron
  beside it offers "Open in browser" (also in the card's context menu).
- `tst_workbench` test target covering the `openWeb` navigation contract,
  the external-surface no-tab path and the new browser-open intent.

- **Draggable editor/tree split on the Agent Tools page**: the prompt
  editor and the file tree are separated by a draggable handle (themed,
  with a wider hit area), the tree keeping its preferred width and the
  editor filling the rest.
- **Global UI font setting** (`appearance.fontFamily` in settings.json,
  default "Microsoft YaHei"): applied before the engine starts via
  `QGuiApplication::setFont` and propagated at runtime through
  `ApplicationWindow.font`; a missing family falls back to the system
  default. Configurable from Settings -> Appearance ("Theme default" +
  every installed family). Rationale: Win10's default fallback rendered
  the whole UI in SimSun.
### Changed

- **Settings redesigned as sectioned pages**: a fixed navigation column
  (Appearance / Launchers / Environment / Skills / Web / Plugins /
  Advanced) on the left and one section's page at a time on the right
  (StackLayout, pages stay instantiated so half-typed input survives
  switching) — previously one long scroll carried all seven sections.
  Dialogs moved into the section pages that use them.
- **Sidebar footer redesign**: system pages (Settings) no longer render as
  full-width rows pinned above the collapse handle — they are icon-only
  buttons (tooltip shows the title, `AIconButton` gains an `active` state
  filling `surfaceBg` for the current destination) sharing one footer row
  with the handle: side by side when expanded, stacked (icons above,
  handle below, horizontally centered) when collapsed. The collapsed
  sidebar width shrinks from 64 to 42 px — exactly the width of one nav
  icon.
- **Typographic scale unified to four steps**: all text now uses exactly
  four font-size tokens — `fontSizePageTitle` (24), `fontSizeSubtitle`
  (16), `fontSizeBody` (13), `fontSizeCaption` (11). The `fontSizeSmall`
  and `fontSizeCardTitle` tokens are removed (usages merged into `caption`
  and `subtitle` respectively; the caption step itself rises from 10 to
  11 so auxiliary text stays legible), and the ladder is codified in
  `designs.md` together with an icon-rendering rule (see Fixed).
- **Skill cards lost their copy button**: the card footer now shows the
  folder path only — clicking the card still copies it, and the right-click
  menu keeps every copy action, so the button was pure visual weight.
- **Sidebar top header removed**: the app icon + name block (and its
  divider) no longer render at the top of the sidebar, which now starts
  directly with the workflow navigation — the window title bar already
  carries the application identity.

### Fixed

- **Icon buttons rendered blurry and oversized**: `AIconButton` used the
  icon `Image` directly as the Control's `contentItem`, so the control
  forced it to 28×28 and `PreserveAspectFit` upscaled the 16 px raster —
  every icon button in the app (settings gear, collapse chevrons, the Web
  tab toolbar) drew 1.75× too large through bilinear filtering. The icon
  is now centred in an `Item` and renders 1:1 at its `sourceSize`. The
  file-tree arrows in Agent Tools gained the matching `sourceSize` (an
  SVG without one rasterises at its natural 24 px and downscales soft).
- **Agent Tools "Add Folder" did nothing on Qt 5**: the facade invokables
  returned `core::OpResult` written in short form, but the gadget's
  automatic metatype registration name is the fully qualified
  `awb::core::OpResult`; the QML call path resolves return types by that
  name, found nothing and threw "Unknown method return type" (the SkillCard
  copy/open actions were silently dead the same way). All Q_INVOKABLE
  declarations are now fully qualified, a shared test guard
  (`awbUnresolvedQmlCallTypes`) fails on any unresolvable type, and an
  end-to-end QJSEngine test reproduces the script call path. Qt 6 resolves
  through templates and was never affected.
- **Text in the prompt editor could not be selected with the mouse**:
  `TextEdit.selectByMouse` defaults to false and no Controls 2 style flips
  it; `ATextArea` now enables it (Ctrl+C over a selection always worked).
- **Startup "Qt Quick Layouts: Detected recursive rearrange" warnings**:
  the empty-state component's inner container was a ColumnLayout whose
  height auto-follow + WordWrap description + first real geometry stacked
  three synchronous rearranges (one past Qt 5's allowed depth). The inner
  container is now a plain Column positioner — verified zero warnings over
  repeated launches on the tools/agents/skills/settings/web pages.
- **Skills page flooded with "Binding loop detected for contentHeight"**:
  the SkillDetailFlyout height read its contentItem Flickable's
  contentHeight, which fed back into the contentItem size. The popup now
  takes its implicit height from a ColumnLayout contentItem and the
  Flickable caps via Layout.preferredHeight; contentHeight is backfilled
  asynchronously (Qt.callLater).
- **The settings theme picker showed an empty box after re-entering the
  page** (and showed opaque theme names like "Catppuccin Mocha (Dark)"):
  `currentIndex` was bound to `indexOfValue(themeId)`, which evaluated to
  -1 before the ComboBox's delegate model was ready and never re-ran. The
  index is computed by iterating the NOTIFIABLE `theme.availableThemes`
  list instead, and the combo shows a short Dark/Light label (full name in
  a tooltip).
- **Switching Web tabs no longer repaints the whole page.** Two freeze
  bugs: a view that was still loading when its tab was switched away got
  `Frozen` (Chromium suspends a frozen page's JS, so the load stalled until
  you came back and then flashed the loading overlay over the rendered
  page), and error/offline state changes tried to freeze the *active* tab
  (rejected by Qt with "page is visible", leaving the tab stuck). The
  lifecycle binding now only freezes inactive, settled tabs — never the
  active tab, never an in-flight load. `web.freezeInactiveTabs` also
  defaults to **off** now: resuming a frozen SPA visibly repaints it, and
  Chromium already throttles hidden views, so the freeze option is opt-in
  for CPU-constrained setups. The `maxLiveTabs` LRU release no longer
  depends on that setting and bounds memory either way.
- **Web tab buttons rendered as white blocks**: the tab delegate's
  `required property string color` (matching the model's `color` role)
  shadowed the delegate Rectangle's `color`, so the theme binding landed
  on the string and the tab body painted default-white with near-invisible
  text. The delegate root is now an Item with an inner background
  Rectangle (the AgentCard pattern).
- **Running-agent rows collapsed in the Web page's list**: `AListRow` had
  an explicit `height` but no `implicitHeight`, so the layout-driven list
  squeezed rows to ~0 — the "Open" buttons appeared as clipped slivers,
  the second row was invisible and the empty note "No agent is running"
  showed while agents were running. `AListRow` now mirrors its height into
  `implicitHeight` (benefits every layout-managed use), and the running
  list no longer forces per-row heights itself.
- **The Agent Tools file tree no longer flashes on refresh.** `refresh()`
  reset the model, and TreeView answers a reset by destroying every delegate
  and collapsing the whole tree, which the page then re-expanded from a
  snapshot. Refresh now diffs each loaded directory against the disk and
  emits only the rows that actually changed — an unchanged refresh emits
  nothing at all, and expanded directories stay expanded. Expanding a
  directory still makes TreeView recycle every visible delegate (that part is
  Qt's doing), so the chevron rotation animation is now suppressed while a
  row is being rebound to different data.
- **Dropping a file reference into the prompt now lands at the caret.** The
  drop handler added the editor's `contentX`/`contentY` to the drop position,
  but a `TextArea` is not a `Flickable` and has neither — the sum was `NaN`,
  `positionAt()` answered 0, and every drop inserted at the very start of the
  prompt.

### Removed

- The `examples/plugins/hello` example plugin and the
  `AWB_BUILD_PLUGIN_EXAMPLES` build option; the plugin documentation now
  uses inline examples instead.

### Documentation

- The documentation is now organised into three sections with a mirrored
  Chinese translation of every page: **Guide** (`docs/guide/`) describes each
  feature for end users, **Architecture** (`docs/architecture/`) covers the
  layering, the frontend and C++ design principles and the extension points
  with Mermaid diagrams, and **Development** (`docs/development/`) documents
  each feature's front end, back end and business logic by name of file and
  class. The former single `docs/development.md` became
  `docs/development/index.md`.
- `docs/AGENTS.md` records the documentation conventions: directory layout,
  English-source / Chinese-mirror parity, the audience and tone of each
  section, no line numbers anywhere, Mermaid and linking rules, and the table
  of what must be updated when the code changes. `AGENTS.md` now points at it
  and treats a documentation pass as part of the definition of done.

## [0.4.0] - 2026-09-27

This release reworks the project from **AgentLauncher** into
**AgentWorkbench**: a sidebar + workspace shell around the launcher, plus
embedded Web tabs, a Skill browser, config-file-driven themes and
experimental plugins.

### Added

- **Workbench shell**: sidebar navigation with badges and collapse, workspace
  host, status bar; `Ctrl+1…9` page switching, `Ctrl+B`, `Ctrl+,`. Window
  size, sidebar state and the last page persist in `settings.json`.
- **Embedded Web tabs**: agent WebUIs open as in-app tabs (Qt WebEngine)
  with per-agent persistent profiles (Chromium cookies ignore ports — a
  shared profile would cross-contaminate local servers on different ports),
  freeze-on-inactive, LRU release past `maxLiveTabs`, offline/crashed/error
  overlays, and reclaimed shortcuts (`Ctrl+W`, `F5`, `Ctrl+Tab`, zoom).
  "Open in browser" always stays available; `AWB_ENABLE_WEBENGINE=OFF` or
  the external surface setting degrades to the system browser.
- **Skill browser**: scans `~/.agents/skills`, `~/.claude/skills`,
  `~/.codex/skills`, the ZCode plugin caches and project directories,
  parsing SKILL.md frontmatter, with search, source facets, sorting, a
  hover flyout and click-to-copy paths. Multi-version plugin caches keep
  only the highest version.
- **Theme engine**: colors and metrics come from JSON theme files (built-in
  Catppuccin Mocha dark and Latte light), switchable at runtime with hot
  reload. `scripts/check-architecture.sh` gates no-literal-colors, dependency
  direction and English-only source strings through ctest.
- **Experimental plugins**: `awb_plugin_api` header interface, host
  discovery/loading and a compilable example plugin. Plugins share the built-in
  page registration path, are disabled by default and carry an in-process
  trust notice. See [Plugins](plugins.md).
- **Settings sections**: Appearance, Launchers, Environment, Skill roots, Web
  surface and Chromium flags, Plugins, Advanced.

### Changed

- **Renamed to AgentWorkbench**: executable, window title, log file
  (`agentworkbench.log`) and data directory (`~/.AgentWorkbench`). The first
  start **copies** the old `~/.AgentLauncher` data (the old directory stays)
  and shows a one-time notice. The root `title` field of `agents.json` is
  retired in favour of `settings.json` `window.title`.
- **Layered codebase**: the flat `src/` is split into core/theme/agents/
  shell/skills/web/workbench modules with an app/ assembly layer, zero
  dependencies between domain modules, and one test target per module — all
  15 previous test cases were migrated and still pass.
- **settings.json**: eight key groups take defaults in place; there is no
  migration code.

### Fixed

- QML singleton type names must be uppercase (Qt ≥ 6 rejects lowercase names
  and the UI failed to load): C++ registers `Theme`, QML keeps the lowercase
  contract name through a root alias.
- `currentPage` is now a notifiable property — as a bare invokable it read as
  a function reference and workspace pages never actually loaded.
- **QML could not call three C++ methods** (they lacked `Q_INVOKABLE`, so QML
  threw "…is not a function" and the action silently did nothing): sidebar
  clicks and `Ctrl+1…9` page switching (`setCurrentPageId`), the settings
  page's surface switch (`setWebSurface`) and Chromium-flags edit
  (`setWebChromiumFlags`) — all broken since S4, found by a manual run because
  page-load smoke never clicks. `check-architecture` now has a rule that gates
  every QML singleton method call on `Q_INVOKABLE` and every property write on
  a `WRITE` accessor; `tst_shell` invokes all three through the meta-object to
  keep them callable.
- Quality-review round (5 P0 + ~25 P1): the first-start `settings.json` write
  no longer runs before the legacy-directory adoption check (which made the
  one-time `~/.AgentLauncher` import dead code); `workbench.openWeb` keeps the
  `#token=` fragment (mutation routes 401'd) while logs/toasts strip it; the
  Skills card description renders (its height was clamped to ~4 px); the Web
  empty state and zoom shortcuts work again (`tabCount`/`tabObject` are now
  real notifiable API); plugin-skill dedup keeps *all* skills of the winning
  version; closing a tab left of the active one no longer moves the active
  tab; the status bar's running/tabs badges rebind on change; the tab bar
  gains its icon, middle-click close, bottom separator, a reload/stop toggle
  and an `⋯` menu icon; buttons show the keyboard focus ring; `F12` opens
  devtools (Debug builds); the Skills flyout opens on keyboard focus, flips at
  window edges and closes on scroll; facet/kind labels are translatable;
  startup writes honour `logging.*` and `locale.override`.

### Packaging

- `scripts/package.sh` produces `dist/AgentWorkbench-0.4.0-win64-Portable.zip`, **measured at 121,303,277 bytes (≈115.7 MiB / 121.3 MB)** — inside the expected 110–130 MB band (including the WebEngine Chromium runtime).
- The deployed directory was verified to launch in a clean environment (no Qt on PATH) with zero UI errors; opening an embedded tab spawned the `QtWebEngineProcess` helper (log: `opened tab … surface=embedded`) which exited with the host.

### Known limitations

- The embedded engine is Chromium 118 (Qt 6.7.3): no H.264/MP4 playback and
  a UA that misreports `Windows NT 6.2`; use "Open in browser" for affected
  pages.
- IME candidate windows, fractional-DPI sharpness and drag-and-drop need the
  manual acceptance pass.

## [0.3.0] - 2026-09-10

This release adds in-app launcher management, makes the portable build
self-contained, and puts all user data in one directory.

### Added

- **Settings page with in-app launcher management**: the bottom-right gear button opens a Settings page listing every launcher with its running state. Launchers can be added, edited, and deleted there — built-ins included — with a **Restore default launchers** action, so `agents.json` no longer has to be hand-edited.
- **Launcher editor** (`AgentEditPage.qml`, replacing `ConfigPage.qml`): a full add/edit form covering every field, with required-field markers, per-field tooltips, live icon and color previews, and validation.
- **Deleted built-ins stay deleted**: `agents.json` gained an optional root `removed` array recording the ids of built-in agents deleted in the Settings page, so the shipped default does not bring them back on the next start.
- **UI-failure diagnostics**: when the QML interface cannot be loaded, the app logs the import search paths and whether the bundled QML modules are present on disk, and shows a native message box naming the log file instead of exiting silently.

### Changed

- **User data moved to `~/.AgentLauncher/`** (Windows: `%USERPROFILE%\.AgentLauncher\`): `agents.json`, `agent_state.json`, and the log directory now live together, instead of being split between the platform config directory and the home directory.
- **Self-contained portable build**: `scripts/package.sh` writes a `qt.conf` pinning Qt's prefix to the executable's own folder, and the app prefers the QML modules deployed next to the executable over any Qt installation found on the machine. This fixes "module … is not installed" failures on machines that have their own Qt.
- **Packaging script**: the archive version is read from `CMakeLists.txt` (still overridable via `VERSION`), the zip is named `AgentLauncher-<version>-win64-Portable.zip`, and a stale CMake cache left behind by a moved or renamed project folder is detected and cleared automatically.
- **Built-in launchers are defined by the shipped config**: on every start each built-in agent is re-applied from the bundled `config/default_agents.json`, and the config in `~/.AgentLauncher/` holds only the launchers you added yourself plus your deletions. Changing a built-in launcher is a one-line edit in that file followed by a rebuild, and no user-data migration or old-config compatibility layer is involved. Editing a built-in in the Settings page therefore lasts only until the next start.
- **Agent install commands**: Kimi Code now installs `@moonshot-ai/kimi-code` (was `@kimi-code/cli`); the other bundled agents' install commands are pinned to `@latest`.

### Fixed

- **Unit tests no longer touch the real user profile**: Qt's test mode does not redirect `HomeLocation`, so after the data-directory move the test suite read and rewrote the developer's real `agents.json`, leaving test launchers behind. The data directory now honours test mode.
- **Icon round-trip**: `file://` icon URLs pass through `resolveIcon()` unchanged, so a locally resolved icon no longer degrades to the default icon after a save and reload.

## [0.2.0] - 2026-08-17

The first formally versioned release of AgentLauncher — a Qt6/QML + C++ desktop app that launches the web UI of AI coding agents from a single card grid. Everything is config-driven: agent definitions live in `agents.json`, never in C++.

### Added

- **Card grid home screen** showing all configured agents, each with a Start button (launches the agent's web server) and a Configure button (opens the agent's settings page).
- **HTTP health-check state detection**: running cards are highlighted with the agent's own color and a colored border; any HTTP response means running, connection refused/timeout means stopped.
- **Stop button (×)** on running cards for agents started this session — `launch()` records the PID from `startDetached`, and `stop()` runs `taskkill /F /T /PID` (Windows) so the whole `cmd → .cmd → node` tree dies.
- **Transient `launching` state** with a 30s safety timeout: the card shows a spinner until the health check confirms the server is up.
- **Launch error feedback**: `launchFailed` signal triggers an at-place red flash on the card plus a scrollable, monospace central error popup.
- **Configurable agent icons and colors** via `agents.json`: icons accept `qrc:/` resources, local file paths (with `%VAR%` and `~` expansion), `http(s)://` URLs, or empty (falls back to built-in default); colors are auto-assigned from a built-in Catppuccin Mocha palette when empty, and an optional `cardColor` sets the non-running background.
- **Four neutral built-in icons**: `default`, `terminal`, `cube`, `bot`.
- **Install / update / version support**: each agent can declare `installCommand`, `updateCommand`, and `versionCommand`; card version labels show the installed agent version.
- **Streaming command output to the card**: while an agent is installing, updating, or running its one-time setup, a scrollable monospace console below the status line shows live stdout/stderr. It hides on success, stays visible for 5s on failure, is dismissible, and can be re-opened via a right-click "Show output" action.
- **One-time `setupCommand`**: runs before the first launch of an agent (e.g. generating a bearer token for `qwen serve`). On exit code 0 the result is persisted to `agent_state.json` and never re-run unless the user picks "Re-initialize" from the card's context menu.
- **Bearer token authentication** via the `tokenFile` field (Qwen Code): the token is set as `QWEN_SERVER_TOKEN` on launch and appended as `#token=<value>` to the web URL.
- **Force Stop** context-menu action: kills the process listening on an agent's web port, even when this launcher didn't start it.
- **Runtime version badges** in the top-right corner: detected Python and Node.js versions are shown as green badges; missing runtimes show a red × with a tooltip explaining affected agents may not work.
- **Window title config**: an optional `title` field in the `agents.json` root overrides the application window title; empty/absent falls back to `AgentLauncher`.
- **Rotating file logger**: all Qt log output is written to `~/.AgentLauncher/log/agentlauncher.log`, rotating at 10 MB keeping 2 files (current + 1 backup). Existing `qWarning()` calls are captured automatically.
- **Window close confirmation**: when agents have been launched, closing the window prompts whether to terminate all background processes started this session.
- **Default agents**: Kimi Code, OpenCode, Qwen Code, OpenClaw, and DeepSeek Harness ship in the bundled `default_agents.json`.
- **Windows packaging script** (`scripts/package.sh`): one-command Release build + `windeployqt`. Double-clickable from Explorer — it auto-loads `vcvars64.bat` when the MSVC environment is missing, always cds to the project root, and uses an explicit `windeployqt.exe` path.
- **Application icon** (`app.rc`, `app-icon.png`).
- **Internationalization (i18n)**: source strings are English; a `QTranslator` auto-loads Chinese (`agentlauncher_zh_CN.qm`) based on system locale. `.ts` sources are synced via `lupdate` and compiled to `.qm` embedded under `:/i18n/`.
- **Documentation site** (MkDocs + Material, English + 中文) with a configuration guide, plus main-window screenshots in the README/docs.

### Changed

- **Launch reliability**: bare commands are resolved through `QStandardPaths::findExecutable` (which applies PATHEXT), and `.cmd`/`.bat` shims run via `cmd /c` so npm-style agents (e.g. `qwen.cmd`) launch correctly — `CreateProcess` alone can't find them.
- **Qwen Code command simplified** to `qwen serve`; the bearer token is handled via `tokenFile` + `setupCommand` instead of being inline.
- **Context menu stability**: Force Stop and Re-initialize items now use `enabled` (greyed out) instead of `visible`, so the menu no longer grows/shrinks as state changes — consistent with Update/Install and Show output.
- **Card layout**: the button row is anchored to the bottom of the card rectangle, eliminating the large empty gap left by top-stacked Column content.
- **Version-check UX**: a 500ms minimum spinner duration so the indicator is always visible; `checkingVersion` is initialized before QML renders so cards show the spinner from the first frame; stderr is read as a fallback for version extraction, and a non-zero exit with a parseable version string is treated as installed.

### Fixed

- **ConfigPage property collision**: the `data` property was renamed to `agentData` to stop shadowing `QQuickItem.data`, which had left fields empty by resolving child bindings to the wrong object.
- **Card Flow overflow**: the 4th card (OpenClaw) overflowed off the right edge because `ColumnLayout` width was bound to an undefined `parent.availableWidth` (ScrollView's internal Flickable has none); rebound to `scrollView.availableWidth` so cards wrap and reflow on resize.
- **Install/update state stuck**: removed the trailing `& pause` from install/update commands (it waited for a keypress so `QProcess::finished` never fired, leaving the card stuck on "Installing…"); added running-protection (reject while the agent is running) and correct `installFinished` signal emission for both success and failure.
- **Stop button state**: the `stopping` state now resets when `stop()` fails, so the button no longer stays stuck.

[0.3.0]: https://github.com/czyt1988/AgentWorkbench/releases/tag/v0.3.0
[0.2.0]: https://github.com/czyt1988/AgentWorkbench/releases/tag/v0.2.0
