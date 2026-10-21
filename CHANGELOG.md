# Changelog

All notable changes to **AgentWorkbench** (called AgentLauncher up to 0.3.0) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **The Markdown editing engine is its own module: `awb_markdownedit`.**
  `MarkdownEdit` (formatting actions, find/replace, Tab semantics) and
  `MarkdownHighlighter` moved out of `awb_tools` into `src/markdownedit/`,
  together with the editor context menu; the namespace is now
  `awb::markdownedit`. The module depends only on `awb_theme` (plus Qt, with
  Qt Quick a `PRIVATE` link) and no longer drags the prompt-writing domain
  behind it, so it can be copied into another project as-is — the integration
  steps are in `docs/development/markdown-edit-module.md`. `awb_tools` loses
  its Qt Quick dependency entirely. Behaviour is unchanged: the QML singleton
  keeps its `MarkdownEdit` name and every page works exactly as before. The
  engine's tests moved with it into the new `tst_markdownedit` target.
- **The Markdown workbench page is broken into components.** The page had
  grown to three thousand lines; the blocks that could stand on their own are
  now separate files, placed by which facade they actually need: the workspace
  tree panel and the two popups are `tools`-domain containers, the floating
  outline is a presentational `markdown`-domain component, and the draggable
  divider is `ASplitHandle` on the component shelf. Components take data
  through properties and report intent through signals, so a host can reuse
  them without dragging a domain behind it. The page keeps state, actions and
  wiring only.
- **`check_architecture` has a sixth rule**: a component-shelf file must not
  reference a domain facade (`tools.`, `markdown.`, `workbench.`, `agents.`,
  `web.`, `skills.`, `environment.`). Rule 1 covers C++ includes, but all QML
  compiles into one module, so the shelf had no gate at all.
- **Architecture docs gained the three QML component tiers** (presentational /
  container / page) — `layers-and-dependencies.md` section 5 — and
  `worktree-add.sh` now initializes SARibbon's nested submodules, without
  which every new worktree failed at CMake configure time.

### Fixed

- **The portable zip shipped without the MSVC C++ runtime.** `windeployqt`
  deploys Qt and nothing else — its `--compiler-runtime` only drops the
  redistributable *installer* into the package, which needs an administrator on
  every target machine — and the build machine always has the runtime
  installed, so the package started there and died on a fresh one with
  "MSVCP140.dll was not found". `package.sh` now copies the runtime DLLs out of
  `<VC>/Redist/MSVC/<version>/x64/Microsoft.VC*.CRT` into the package
  (app-local deployment: no installer, no administrator), and verifies the
  result before zipping — the files the application cannot start without, plus
  the imports of every DLL and executable in the package, which have to be in
  the package or ship with Windows (the MSVC runtime counts as neither). An
  incomplete package now fails the run instead of shipping quietly;
  `AWB_ALLOW_INCOMPLETE=1` overrides that, and `build.sh --print-redist` reports
  the runtime folder it deploys from.

## [0.5.0] - 2026-10-13

This release makes the Markdown workbench the app's centrepiece: prompt
writing and the preview share one window again, and with it the whole preview
lane reaches Qt 5. The prompt page's command bar is rebuilt as a three-tab
ribbon on SARibbon 3.0, drafts become a set with version history, crash
recovery, templates, favourites and a checks panel, and the shell gains a
system tray and a single-instance guard.

### Changed

- **The prompt page's command bar is grouped into three tabs.** **Start**
  carries the clipboard, format, list, heading, insert and copy-prompt
  actions plus the find field; **Prompt** holds the template and favourite
  galleries, the save actions and the checks/history popups; **View** holds
  the editor view and the ribbon style (the three-row style is retired). The
  theme bridge now pushes `theme.chromeBg` as the SARibbon accent — the tab
  row's background — so the bar matches the app chrome in both themes, and
  the ribbon icons are tinted data-URL SVGs. The template library sits on
  SARibbon's own `RibbonGallery`, filled through the upstream dynamic-data
  API, instead of the self-drawn panel; the format buttons are no longer
  checkable (a checked state would have to follow the cursor, and the cursor
  position does not enter a QML binding); and the leftmost application button
  is gone — it had no action, and the tab strip already names the open
  document. On the page itself the left panel keeps only the workspace tree,
  the right panel is gone (checks and history are non-modal popups), **New
  Draft** moved to the tab strip's `+`, **Rename**/**Delete** moved into the
  tab context menu, and find is unified: `Ctrl+F` focuses the command bar's
  find field, while the separate draft find bar and preview search bar are
  retired.
- **Double-clicking a file row opens the file.** Markdown files land in a new
  workbench tab (deduplicated by path, a capacity error is toasted) and every
  other file — including `.md` in builds without the preview capability —
  goes to the system's default opener. Inserting a reference moved to the
  row's context menu as **Insert path at cursor**; drag-and-drop still
  inserts.
- **The workspace panel drags and folds as one surface.** Its width is
  draggable between 180 and 520 px, and dragging past the lower bound
  collapses it (with hysteresis). The section-header toggle now collapses the
  entire left panel instead of only the workspace section, which had emptied
  the panel while leaving its frame behind; the strip's edge button is the
  single restore entry.
- **The floating outline is user-shapeable.** A button in its header folds it
  to the title row (click again to restore), a corner handle drags its height
  and its left edge drags its width — the right edge stays anchored. Geometry
  and the collapse flag are page memory, not persisted, like the split share.
- **Context-menu entries that do not apply are disabled, not hidden.** Hiding
  left blank slots (the menu's content item is a ListView) and made the
  actions undiscoverable; **Preview Markdown** on a non-Markdown row and
  **Rename**/**Delete** on file tabs are now greyed in place. The rule is
  written into `designs.md`.
- **The Markdown workbench lives in the main window again.** It is no longer
  a standalone window: the sidebar's **Agent Tools** group is back, and its
  **Prompt Writing** entry opens the workbench as a page inside the main
  window's work area (a resident page, created on first visit — the editor's
  undo stack, scroll position and split proportion survive page switches).
  The thin **Markdown** start page is gone; right-clicking a Markdown file
  still opens it in the workbench, now by navigating to the page. Exiting
  with unsaved document edits is guarded by the main window's exit flow
  (save-all / discard / cancel). The right side is one bordered panel again
  — outline, checks and draft history as clearly separated sections instead
  of two borderless panels that read as overlapping windows.
- **The workbench ribbon renders its context tab correctly again.** SARibbon
  is updated to the upstream dev-3.0 tip (`d3d45d8`), which fixes panel child
  order scrambling, never-rendering in-panel separators, and hit-test
  visibility of registered categories; the update hard-requires QWindowKit,
  which is built in-tree from SARibbon's own nested submodule chain (the
  workbench keeps its native window frame — no frameless agent is declared).
  The **Prompt tools** context tab now carries `theme.success` instead of
  `theme.accent`: SARibbon paints the whole title row in the accent colour,
  so an accent context colour made the tab and its band invisible — the
  "context category blends into the other categories" defect.
- **The ribbon menus follow SARibbon 3.0's QAction command layer.** The
  submodule pin moves further up the dev-3.0 line (`423b8e8`), past the
  refactor that retires the 2.x `RibbonMenuItem` type — the command bar's
  five menu buttons (headings, code fences, preview font size, content
  width, ribbon style) now author their entries as `menuActions` QAction
  lists, so the ribbon loads again instead of silently falling back to the
  plain toolbar.
- **The ribbon tabs pick up the upstream 3.0 tab behaviours.** The
  submodule pin moves up the dev-3.0 line again (`7ecf2e6`): double-
  clicking a tab now toggles minimum mode and clicking a tab while
  collapsed previews that category, context tab titles render correctly
  again, and the in-tree QWindowKit dependency is the pristine upstream
  `1.5.0` release (the dropped local patch guarded a destructor-order
  segfault that cannot happen here — no frameless agent is declared).
- **The Markdown workbench page loads again on the Qt 5.15 lane.** Its three
  capped lists (templates, favourites, draft history) used `ObjectModel`, a
  Qt 6.5+ type that left the Qt 5 fallback lane with "ObjectModel is not a
  type" and took the whole page down at startup; they run on plain JS-array
  models now, identical on both Qt lanes.

### Added

- **The Markdown preview runs on the Qt 5 lane.** It used to exist on Qt 6 +
  WebEngine only, which left the Qt 5.15 build with a permanently disabled
  **View** tab and no way to see a rendered document. The adapter, the
  `:/markdown/` runtime, the pane registration and the preview smoke tests are
  all built on both majors now. Navigation requests answer through a compat
  shim (Qt 6 calls `accept()`/`reject()` on the request object, Qt 5 writes
  its `action` property — a direct call is a `TypeError` there, and the
  exception silently lets the navigation through), and the MIME table lost its
  `; charset=utf-8` suffixes: the Qt 5.15 job reply accepts a bare MIME only,
  and a compound value made the whole response unparseable (the page loaded as
  empty HTML with every script dropped).
- **Underline**: Markdown has no plain-text underline marker, so `<u>` is the
  only portable notation. The preview keeps `html: false` — the injection
  surface is unchanged — releasing exactly that pair when it closes inside one
  inline segment, everything else still escapes. The editor writes the pair
  and reports the cursor's underline state; the highlighter draws it with an
  underline font.
- **Multiple drafts**: the prompt side of the workbench now holds **a set of
  drafts, not one** — each in its own tab (draft tabs mix with document
  tabs), each with its own undo history, all auto-saved. **New Draft**,
  **Rename**, **Duplicate** and confirmed, not-undoable **Delete** live on
  the Start tab; closing a draft's tab keeps the draft (only Delete
  deletes). Up to twenty; a twenty-first is refused, never silently
  dropped. **From Document** turns the open document's text into a new
  draft — one-way, the draft never writes back into the file.
- **Draft versions and rollback**: the app quietly snapshots each draft
  when you copy, clear, replace or switch away from it; **Save version**
  marks one by hand. The **History** panel lists them (with the reason
  each was taken) and clicking one rolls back — your current text is
  saved as a version first, so rolling back never loses anything.
- **Crash recovery**: if the app closes unexpectedly, the next start
  offers to restore the draft text from that session as a new draft, or
  discard it. Asked once, then the copy is gone.
- **Formatting tools**: the workbench window's **Start** tab grows the full
  Markdown toolbar — **Bold**, **Italic**, **Strike** and **Code** (each
  showing what is true at the cursor), **Link**, headings 1–3, bullet,
  numbered and task lists, quote, divider, fenced code blocks with a
  language menu, and table skeletons. Every action is one undo step, and
  the buttons work in document tabs' source editor too, not just prompts.
- **Find and replace in the prompt**: `Ctrl+F` (or **Find** on the Start
  tab) slides a find bar in above the draft editor — next / previous,
  replace one and move on, replace all (with a count and one undo step),
  plus case-sensitive and whole-word toggles that stick between sessions.
  In document tabs `Ctrl+F` keeps opening the page search, as before.
- **Prompt checks**: the new **Checks** panel reviews the draft against
  ten common-prompt rules — vague wording, missing acceptance criteria,
  Windows-style paths, unfilled `{{placeholders}}`, smart quotes and more —
  and lists the findings as clickable suggestions that jump to the right
  place in the editor. No score, just a list. Any rule can be turned off
  from its finding's × and stays off.
- **Token estimate**: the status line shows `~N tokens (estimated)` for
  the draft (and the same kind of number for an open document) with a
  colour that shifts at a quarter and three fifths of the context window —
  an explicitly rough estimate, never a tokenizer promise. The window size
  base is 32000 tokens by default, editable in the app's `tools.json`.
- **Prompt templates**: the workbench window's left panel gains a
  **Templates** section — a searchable shelf of prompt skeletons. Ten ship
  built in (bug report, feature request, test plan, code review, refactor,
  documentation, explain code, commit message, reproduce an issue, plan a
  change); **your own prompts can be saved as templates** (title, category,
  description) and live as plain Markdown files in the app's data folder,
  one per template — hand-editable, back-up-able, shareable, and a file
  sharing a built-in's id overrides it. Templates carry
  **`{{placeholders}}`**: inserting one with variables opens a small fill-in
  form (pre-filled defaults, `Enter` inserts, `Esc` cancels); a field left
  empty is a deliberate skip and the `{{placeholder}}` stays visible in the
  text. Two placeholders fill themselves — the current workspace and today's
  date — and the editor's selection, if any, is used as the `{{selection}}`
  value. The ribbon grows its first **context tab**, **Prompt tools**
  (visible only while the prompt is active): **Insert Template** opens the
  library, **Save as Template** stores the draft. The no-ribbon toolbar
  carries the same three buttons.
- **Favorites**: finished prompts — the ones you keep reusing exactly as
  they are — can be saved with **Save Favorite** and re-inserted with one
  click from the **Favorites** panel under the templates (or copied straight
  to the clipboard from the row). Kept in the app's own storage, newest
  first, up to fifty.
- **Markdown workbench tabs and sessions**: the workbench window now holds
  **several documents at once** — one tab per file, up to eight (the ninth is
  refused with an explanation, nothing is silently closed). Tabs carry a
  dirty dot, remember their own Preview/Split/Source, and closing one closes
  the view, never the file. Switching tabs swaps the single embedded view;
  closing the window keeps every session — reopening it restores the tabs
  exactly, unsaved edits included. The **draft now has a rendered preview
  too** (Preview/Split/Source, defaulting to split): the same pipeline that
  renders documents renders the prompt as you write it, so long prompts can
  be checked for structure, tables and diagrams on the spot. The outline
  panel serves the draft as well.
- **Session restore and startup**: the app remembers the workbench's open
  tabs (with each tab's view setting), the active tab, and the window's
  shape, and restores them when the window is next opened after a restart —
  a document whose file has been moved or deleted is skipped, the rest come
  back. The start page gains a **Recent documents** list (six entries, one
  click opens the file). By default the app does not open the workbench at
  startup; the new **Open the Markdown workbench on startup** switch
  (Settings → Launchers, `markdown.autoOpenWorkbench`) turns that on for
  heavy writers.
- **.md file association**: double-clicking a `.md` file in the file manager
  can now open it in the workbench. With the app already running, the file
  opens as a new tab and the window comes to the front; otherwise the app
  starts and shows it — the **full application**, deliberately not a
  lightweight viewer. The **Associate .md files** action (Settings →
  Launchers) writes the registration; the default app remains the user's
  choice in Windows Settings, and the switch says exactly that. Builds
  without the preview capability hide the action and report the reason when
  an open request arrives.
- **Markdown workbench window**: prompt writing and the Markdown preview are
  now **one standalone window** with two presets. The **prompt form** (opened
  from the sidebar's new **Markdown** start page) carries the whole former
  prompt page: the workspace file tree on the left, the draft editor with
  highlighting, drag-and-drop file references and the formatting context menu.
  The **document form** (right-click **Preview Markdown** on a tree file) is
  the former preview window — outline, in-document search,
  Preview/Split/Source, byte-faithful saving, export — living in the same
  shell with its side panels collapsed. A **Full Tools** toggle switches
  presets; the two presets' interface state stays independent while the window
  lives. The window itself opens in **every** build: in a build without
  WebEngine the prompt form is a plain editor there, while the document form
  and the preview pane simply do not exist. Still no send path — the window
  delivers by copy only. The window's QML lives in the workbench domain
  (`src/workbench/qml/`), which is the only layer allowed to span the tools
  and markdown domains; the preview surface is split into
  `MarkdownPreviewPane.qml`, loaded by resource URL only in builds that
  registered it.
- **Ribbon command bar**: the workbench window's top band is a SARibbon
  `RibbonBar` in single-row mode (built via the new `AWB_ENABLE_RIBBON`
  option; see the previous entry). **Start** holds the migrated actions —
  **Copy Prompt**, Cut/Copy/Paste on the selection, Undo/Redo, Bold/Code/
  Bulleted list, Find and Save; **View** holds the three view states, the
  outline toggle, the Full Tools preset switch, the collapse toggle, the
  ribbon style (single/two/three rows) and **Follow Theme**. The application
  button shows the current document's name. The ribbon's colours follow the
  app theme through a one-way bridge. With `AWB_ENABLE_RIBBON=OFF` — or in
  test processes that did not register SARibbon — the window falls back to a
  plain toolbar with the same actions. New `tools.json` `ribbon` section
  (collapsed, style, followAppTheme, lastTabIndex) remembers the bar's state;
  missing keys default, no migration.
- **Ribbon smoke test**: `tst_ribbonui` (only built with
  `AWB_ENABLE_RIBBON=ON`) loads the shipped `WorkbenchRibbon.qml` into a real
  engine after `saRibbonRegisterQmlTypes()`, asserting the bar lays out and
  no SARibbon warning (static-qrc init, registration order, leaf creation)
  escapes.

- **SARibbon integration (build only)**: the ribbon toolkit that will power
  the Markdown workbench window is now a git submodule
  (`third_party/SARibbon`, https://gitee.com/czyt1988/SARibbon, MIT — the
  license travels inside the submodule). Pinned at `f89a02e`, the last
  commit whose QML module links only Core/Gui/Qml/Quick/QuickControls2 +
  SARibbonCore; the next upstream commit forces a QWindowKit dependency the
  integration deliberately does not take. New build option
  `AWB_ENABLE_RIBBON` (default `ON`) builds it as a static library — QML
  module only, no QWidget stack, no extra DLL — and registers its types via
  `saRibbonRegisterQmlTypes()` before the engine loads; `OFF` keeps the
  binary ribbon-free without breaking the build. No window uses it yet; the
  workbench window that consumes it lands next. Offline submodule init
  follows the spdlog convention (`scripts/worktree-add.sh` clones from a
  populated checkout).
- **Prompt workbench relayout**: the page (formerly **Agent Tools - Prompt**,
  now **Prompt Writing**) swaps its panes — the file tree sits on the **left**,
  the editor fills the right workspace — and the workspace switcher, the
  folder picker and the refresh button move into a compact header row on top
  of the tree (the picker is an icon-only button whose tooltip names it; the
  switcher tracks the tree's width when the divider is dragged). **Copy** moves
  into the page header, the toolbar row is gone, and both panes drop their
  corner radius at the divider so the tree and the editor meet the splitter
  flush. The **Agent Tools** sidebar group is kept as the reserved home for
  future tools; its member simply drops the redundant "Agent Tools" prefix from
  the title (registration id unchanged).
- **Markdown preview window**: right-clicking a `.md` file in the Agent Tools
  file tree (**Preview Markdown**) opens it in a window of its own. It renders
  fully offline from a vendored runtime (markdown-it 15.0.2, KaTeX 0.19.0,
  mermaid 11.17.2, highlight.js 11.12.0) served over a local `awbmd://`
  scheme: formulas, diagrams, syntax-highlighted code, and the document's own
  relative images. Blocks are parsed in time-budgeted passes and offscreen
  blocks skip layout, so a document of several hundred kilobytes shows its
  first screen in a few hundred milliseconds instead of waiting for the whole
  file. The window also edits the source (`Ctrl+S` saves, guarded by an
  unsaved-changes prompt), and has an outline, in-document search,
  **Copy selection** / **Copy Markdown source**, **Reload from disk**,
  **Open in browser** and HTML export. Settings: `markdown.mode`
  (`preview` / `split` / `source`), `markdown.fontSizePx`,
  `markdown.contentWidthPx`, `markdown.showOutline` — written by the window
  itself, there is no Settings page UI for them. From this release it runs on
  both Qt majors; **WebEngine builds only**: with `AWB_ENABLE_WEBENGINE=OFF`
  the menu item does not appear and none of the runtime reaches the binary
  (otherwise it costs about 4 MB, 2.6 MB of which is mermaid, requested only
  when a document contains a diagram).
- **System tray**: an optional tray icon (Show/Hide/Quit menu, left-click
  toggle, one-time balloon hint) plus a close-choice dialog — while the tray
  is available the app no longer exits on last-window-closed; closing the
  window asks whether to exit or minimize to the tray until the choice is
  remembered, and every exit is explicit and still asks when agents are
  running. Settings: `launcher.trayEnabled` (default `true`),
  `launcher.rememberCloseAction` (default `false`) and `launcher.closeToTray`
  (default `false`, the remembered action), all live. The shell module links
  `Qt::Widgets` for this. Windows may park the new icon in its hidden-icons
  area; the guide says how to pin it.
- **Single instance**: an optional guard (vendored SingleApplication 3.5.6)
  where a second start brings the existing window to the front instead of
  opening another one. Setting: `launcher.singleInstance` (default `true`),
  effective from the next start.
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
- **Runtime install paths in Settings → Environment**: Python and Node.js now
  get one row each instead of sharing a line — the version on top, the full path
  of the executable that answered underneath (the runtime's own
  `sys.executable` / `process.execPath`, falling back to what `PATH` resolves
  to), and Re-detect moved into the page header with a busy indicator.
- **Sidebar group labels and indented rows**: a sidebar row can now be a static
  group heading instead of a destination (`NavKind::Header`), and the rows under
  it indent by their `level`. The first group is **Agent Tools** with its page
  relabelled **Agent Tools - Prompt** (Chinese UI: 「Agent 工具」/「Agent 工具 ·
  提示词」) indented under it — the label is not clickable, never highlights,
  takes no `Ctrl+1…9` slot, carries no badge and cannot become the current
  page, and hides entirely while the sidebar is collapsed. The page keeps its
  registration id `tools`, so the remembered last page still resolves. Covered
  by a new `tst_shell` case (`testSidebarGroupLabel`). (Unreleased: the
  member's title drops the redundant "Agent Tools" prefix and reads
  **Prompt Writing** — see the relayout entry above.)

### Changed

- **Skill cards no longer lift on hover**: the hover highlight (brighter glass,
  stronger border, pointer spotlight) now stays in place instead of nudging the
  whole card up by one spacing unit, which read as a jump as the pointer crossed
  a card edge.

- **Python/Node detection rewritten** (it used to report a red cross for a
  probe that merely timed out): detection now runs on the thread pool instead of
  the GUI thread, tries `python`/`python3`/`py` and `node`/`nodejs` in order (so
  Windows' Microsoft Store placeholder loses to the real interpreter), runs the
  resolved executable directly instead of through `cmd /c`, and asks the runtime
  where it lives. A timeout or a start failure is no longer "not installed": the
  last known verdict stays and the probe retries after 3 s / 15 s / 60 s, with
  the per-round detail in the log and in the Settings row tooltip. Verdicts are
  cached in `<dataRoot>/environment_cache.json`, so the status bar and the
  Settings page fill in immediately at startup and change only when a re-check
  actually finds something different; the badges show an ellipsis, not a red
  cross, while no verdict exists yet.

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
- **Sidebar pointer spotlight**: moving the pointer over the sidebar now
  drags a faint light across the panel and lights a 2 px sheen along its
  border, the same effect the launcher cards use (`ASpotlight`, which
  gained `spotRadius` / `ringRadius` overrides for panels that are not
  roughly square, and `spotAlpha` / `ringAlpha` for the two peaks). Both
  themes light from the accent but tune it differently: on a dark base the
  accent already reads as light, so both peaks sit below the card density,
  while a near-white base has no brightness left to add — there the accent
  is worth the hue shift it brings, so the spot is a very faint accent pool
  kept inside the panel width (a denser one reads as a blue stain). The nav
  rows' current/hover chips became translucent because the overlay is
  painted under the content: solid, they hid the light exactly where the
  pointer rested.

### Fixed

- **Dragging the workspace divider collapsed the tree on the first move.** The
  width formula subtracted the panel's *current* width instead of the grab
  offset recorded at press, so every drag landed in the collapse branch; the
  handle also re-positions itself as the panel resizes, so the width has to be
  the pointer's absolute position minus the grab offset. A `tst_toolsui`
  regression case drives real mouse events (+80 widens, past the threshold
  collapses).
- **The tab menu's "Close tab" did nothing on file tabs** — it silently
  returned; it closes them like any other tab now.
- **The Markdown preview could take its renderer down on Chromium 87–97.**
  `contain-intrinsic-size: auto …` needs Chromium 98: on older engines the
  compound declaration is invalid, display-locked offscreen blocks take zero
  height (the scroll height lies, so the scrollbar is uselessly short), and
  Chromium 87 crashes on the resulting scroll-driven lock transitions. The
  page now probes `CSS.supports('contain-intrinsic-size', 'auto 48px')` before
  the first block mounts and demotes those engines to plain layout — real
  scroll heights and a live renderer, in exchange for the offscreen-skip win,
  which only returns where the feature is whole. The lane self-probe no longer
  leaves a console warning on every start either.
- **The "Start" tab's icon-only buttons rendered oversized in the loose ribbon
  style.** SARibbon resolves an unset proportion to a large button, and a
  large icon-only button stretches its icon over the whole button height. The
  loose (two-row) style shows those buttons with captions now; the compact
  single row stays icon-only.
- **Code Block fenced single-line selections as inline code.** It now always
  fences whole lines, and with the cursor on an empty line it drops a bare
  skeleton and parks the caret between the fences.
- **Skill-root scan noise**: a skill root that does not exist — a plugin
  cache's old version path, a project without `.claude/` — is a normal skip,
  not an anomaly, so four WARNING lines per start became debug-level lines.
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
- **The Agent Tools prompt editor could not scroll.** The editor was a bare
  `ATextArea`, and a `TextArea` never scrolls its own text: once a prompt
  outgrew the visible area, the rest could not be reached — there was no
  scroll bar, and neither the wheel nor dragging moved anything. The editor is
  now a vertical `Flickable` (`StopAtBounds`, clipped) whose content is the
  `ATextArea` attached through the `TextArea.flickable` attached property,
  with `ScrollBar.vertical: AScrollBar`. That attached property supplies the
  three parts scrolling needs: the text area grows to its content height, the
  caret is scrolled back into view after every `cursorRectangleChanged`, and
  the text area's own background is reparented to the `Flickable` and sized to
  it, so the frame and focus ring stay pinned to the viewport instead of
  scrolling away with the text. A new `tst_toolsui` case locks in the scroll
  extent, the appearing scroll bar and the actual movement.
- **Sidebar page titles were never translated**: Qt 6's `moc` writes the fully
  qualified class name into the meta-object, so `tr()` inside a namespaced
  class looks strings up under a context like `awb::workbench::BuiltinPages`,
  while `lupdate` records the bare `BuiltinPages` in the `.ts`. The two keys
  never met, every navigation title silently stayed English, and neither the
  build nor the test suite noticed. `BuiltinPages` now calls
  `QCoreApplication::translate("BuiltinPages", …)` explicitly, so the context
  matches what `lupdate` emits and its titles (including the new group label
  and "Agent Tools - Prompt") translate. The same mismatch still affects the
  other namespaced classes; it is documented in `docs/development/i18n.md`.

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
