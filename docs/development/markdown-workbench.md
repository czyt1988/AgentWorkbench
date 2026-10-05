# Markdown workbench

The Markdown workbench is a **standalone window** that unifies the former prompt-writing page and the former Markdown preview window: one window, two presets, many sessions. The **prompt form** composes a prompt against a workspace file tree; the **document form** reads and edits Markdown files, one tab per file. It is opened from the sidebar's **Markdown** entry, by opening a Markdown file from the workspace tree, or by double-clicking an associated `.md` file in the system file manager; it is not a page in `Workspace`.

It exists for three concrete reasons. Referencing a file in a prompt normally means hand-building a relative path — the file tree inserts `` `./relative/path` `` references by drag-and-drop or double-click. Typing a prompt straight into an agent CLI risks hitting Enter on a half-finished message — so the window has **no send action**: Enter inserts a newline, nothing leaves the window, and the only delivery is copy. And on a document of a few hundred kilobytes, the first paint is bounded by *the first pass over it*, not by *the whole document* — [the renderer](#the-renderer-passes-batches-and-visibility) is where that claim is earned, and
[Markdown preview runtime (research)](../research/markdown-preview-runtime.md) holds the numbers behind it.

User view: see [Markdown workbench](../guide/markdown-workbench.md).
Architecture context: [Layers and dependencies](../architecture/layers-and-dependencies.md),
[Frontend design](../architecture/frontend-design.md).

## What this window is, and what it is not

- **One window, two presets.** The presets are interface configurations of the same code, not two applications:

  | | Prompt form | Document form |
  |---|---|---|
  | Left panel | the workspace file tree, open by default | collapsed to a narrow edge that can be clicked open |
  | Ribbon | expanded (single row by default) | collapsed to its tab row |
  | Middle area | the prompt draft, with Preview / Split / Source | the open documents, one per tab, with Preview / Split / Source |

  The right side holds the outline panel, behind its toggle, serving whichever session is active (the draft has headings too). Opening the window from the sidebar or its start page yields the prompt form; opening a Markdown file from the workspace tree yields the document form (or retargets the live window to that file). The preset switch is the **Full Tools** toggle on the ribbon's View tab; a document preset that cannot be rendered (no capability, no file tab) falls back to the prompt form rather than showing a broken window. There is no user-facing "mode" concept — one window with more or fewer tooling.
- **The window is destroyed when closed, the sessions are not.** `MainWindow.qml` keeps the window behind a `Loader` whose `active` no longer includes `markdown.available` — the only thing that turns it off is the window reporting its own close. A QML `Window` survives its own close, so every accepted `onClosing` path calls `MarkdownPreviewController::notifyWindowClosed()` and `Component.onDestruction` reports again for paths that never deliver a close event, such as quitting the app; the report is idempotent. Closing the window puts the sessions to sleep in the controller's memory; reopening the window picks them back up, unsaved edits included. Only closing a tab drops a session. Holding a live preview view (and the ~4 MB of runtime it loads) between uses would be a permanent tax on every start.
- **The prompt form works in every build.** It is a plain QML editor with highlighting; in Qt 5 and no-WebEngine builds the preview pane is simply never loaded (no `WebEngineView` exists in the process), the view switcher is absent, and the document form's entry points degrade honestly — the file-tree menu item never appears, an open request reports its reason (see [Capability gating](#capability-gating-qt-5-and-no-webengine)).
- **One render surface, many sessions.** The controller is a session container: each session carries its own document, blocks, outline, rev, per-tab view mode and export directory, addressed by `docId`. Only one session is *active* at a time; switching sessions rebuilds the page wholesale (`setDocument`). `doc.json` serves the active session, while `asset/<docId>/…` resolves against **every live session** — image requests for a tab you just switched away from may still be in flight. Each embedded view is a Chromium frame with its own renderer-side memory, which is why there is exactly one and it is reused rather than pooled.
- **File tabs are capped at eight.** Opening the same path twice activates the existing tab (deduplicated by absolute path); opening a ninth file is refused with an explanation instead of silently evicting the least recently viewed one — a tab the user is editing in must not vanish because another file was opened.
- **Saving cannot rewrite a file.** A README that is CRLF with a BOM must still be CRLF with a BOM after `Ctrl+S` — the document pipeline records the byte shape on load and reproduces it on save (see [the pure layer](#the-pure-layer-document-blocks-page-strings)). The draft, by contrast, never touches the filesystem directly: it lives in `tools.json`.
- **Not a general HTML viewer.** Only Markdown-suffixed paths are accepted (`MarkdownPreviewController::isMarkdownPath()`, the one place the suffix list lives — it also decides the file-tree menu item and the command-line file arguments). Rendering arbitrary HTML through this surface would mean executing the document's own scripts, which the renderer's `html: false` deliberately refuses.
- **No agent coupling.** The window talks to no agent, runs nothing, and edits no repository file other than the documents the user explicitly opened. Its writes are `tools.json` (workspace memory, draft, ribbon preferences), `markdown_session.json` (the session snapshot), the system clipboard, the document's own files on save, and the export copies.

## The ribbon

The window's actions live in a command bar across the top. In `AWB_ENABLE_RIBBON` builds that bar is a SARibbon `RibbonBar` (`WorkbenchRibbon.qml`); everywhere else — and in test engines that never registered the SARibbon types — it is the plain toolbar `WorkbenchToolbar.qml`. This section records how SARibbon comes into the build, what sits in each tab, and the constraints the integration must respect.

### How SARibbon comes in

- SARibbon is a git submodule at `third_party/SARibbon` (MIT licence), **pinned at `f89a02e`** — deliberately not the upstream tip: the tip forces a second third-party dependency (QWindowKit) when the QML module is on, and the workbench window keeps the native system frame.
- `AWB_ENABLE_RIBBON` (default `ON`, declared in `cmake/AwbOptions.cmake`) wires it in: `SARIBBON_BUILD_QML=ON`, `SARIBBON_BUILD_WIDGETS` / `SARIBBON_BUILD_EXAMPLES` / `SARIBBON_BUILD_TESTS` / `SARIBBON_INSTALL` = `OFF`, static libraries. Only the QML module is built — the QWidget part of SARibbon never enters the binary.
- `app/main.cpp` calls `saRibbonRegisterQmlTypes(&engine)` **before** `engine.load()`. Registration is imperative (there is no qmldir plugin); the function guards itself against double calls.
- The domain libraries do not link SARibbon. `awb_tools` and `awb_markdown` keep depending on `awb_core` + `awb_theme` only; the integration points are `app/` (CMake link + registration) and the command bar QML (`import SARibbon`). This keeps the third-party module out of the domain layers.

### Tabs and what lives in them

The command bar has two tabs: **Start** and **View**.

- **Start** holds the migrated actions: **Copy Prompt** (the delivery path — a second **Copy** sits in the quick-access bar on the title row), the clipboard group **Paste / Cut / Copy Text**, **Undo / Redo**, and the formatting trio **Bold / Code / Bullets**. While a file tab is active, the tab also carries **Find** (opens the document search) and **Save** (enabled only when the document is editable). Every action forwards to the window's action layer or the existing `MarkdownEdit` static cursor operations — the command bar is an entry point only and holds no logic of its own.
- **View** holds the switches: the **Preview / Split / Source** three-state and the outline toggle (both for whichever session is active — the draft renders too), **Full Tools** (the preset switch: on = the prompt form, off = the document form), **Collapse** (fold the whole band to its tab row), **Style** (single / two / three rows), and **Follow Theme** (whether the band's colours track the application theme).

The single row is the default style, and its consequences are worth knowing before adding buttons: panel titles are hidden, and every button renders at the small size regardless of its `proportion` — grouping and button order carry the hierarchy, and labels must stay short. `minimumMode` collapses the band to its tab row; the document-form preset starts collapsed, and clicking the tab row expands it again.

### Theme bridging

Two design systems meet at exactly one point. `WorkbenchRibbon.qml` pushes the application `theme` into `RibbonTheme` — accent, content background, text colour, and the dark/light verdict — once per `Theme::changed`, gated by the **Follow Theme** preference (`tools.ribbonFollowAppTheme`, on by default; turned off, the ribbon keeps its own palette). The bridge is **one-way** (application → ribbon), with no reverse sync and no second auto-follow. The window's own QML keeps using `theme.*` tokens exclusively; the literal-colour architecture gate applies to this repository's sources exactly as before.

### Command bar constraints

- The ribbon is its own component, loaded by the window through a `Loader` **by resource URL** — the same pattern the preview surface uses. `WorkbenchRibbon.qml` is registered into the resource manifest only in `AWB_ENABLE_RIBBON` builds; when the load fails (the switch off, or a test engine without the SARibbon types) the window loads `WorkbenchToolbar.qml` instead. The load happens once at window creation and is never toggled on demand: SARibbon's leaves are created during `Component.onCompleted`, and a bar created on demand dies with an early context.
- The two skins are pure forwarders to the same window action layer — neither may grow behaviour of its own, or the two builds drift apart. The button set matches the context menu and the two ribbon tabs.
- No `Popup`/`Dialog`/`Menu` is declared inside a `RibbonPanel` — popups created during the bar's `componentComplete` become separate native windows and break the window scene. Anything popup-shaped is created through a `Loader`.
- Ribbon menus accept `RibbonMenuItem` only; the `AMenu` family stays for in-window context menus. An action lives in exactly one place — the same action must not appear both in a command bar tab and in a side panel, or label and enabled state will drift apart.
- SARibbon's own strings are not in this repository's `.ts` files and stay English in Chinese sessions. The window adds no ribbon-builtin screens beyond the bar itself.

## Sessions: one container, one render surface

`MarkdownPreviewController` (the QML singleton `Markdown`) is a **session container plus a single render surface**. A *session* is one document the user is looking at:

- a **file session** — a disk file, opened via `openFile()` (the file tree, a recent-documents row, a command-line argument or a second-instance message all land there); each has a generated `docId` (`d1`, `d2`, …);
- the **draft session** — a *virtual document* (`MarkdownDocument::setVirtual()`) holding the prompt draft, with the fixed `docId` `"draft"`. It takes no tab, it cannot be closed via tabs (it is the prompt form's own body), and it is editable-and-renderable like any file: the rendering pipeline consumes text, not paths. The draft's default view mode is **split** (it exists to be written in, and preview-first would hide the editor); builds without the preview capability degrade it to source.

Each session owns its `MarkdownDocument`, block sequence, outline, `rev`, per-tab view mode, export directory and `doc.json` payload cache. The QML property surface (`filePath`, `dirty`, `mode`, `outline`, …) reflects the **active** session; `tabs` is the tab strip's model — `[{docId, title, path, mode, dirty, active}]` in open order, files only (the `path` and `mode` fields exist for the session file; the UI reads the rest).

**Switching is a full page rebuild.** `activateSession()` enqueues `setDocument`, whose same-command overwrite semantics purge the previous session's pending `updateBlocks` — exactly the guard that keeps a stale delta from fighting a full rebuild. Incremental edits (`updateBlocks`) only ever target the active session.

**Lifecycle:**

- `openFile()` on a path that already has a session **activates** it (the tab is already there, the unsaved edits with it) instead of opening a second tab; otherwise it creates a session, activates it and emits `windowRequested()`. The cap is eight file sessions; the ninth is refused with an explanation.
- `closeSession(docId)` closes a **tab** — the view goes away, the disk file stays. Closing the active tab activates the most recently opened remaining file session; with none left, the draft.
- `notifyWindowClosed()` puts the sessions to sleep: the in-flight commands are dropped (no one would pump them), the sessions, tabs and dirty flags all stay. Reopening the window takes `doc.json` straight back to the active session. Only a restart needs the session file (below).
- The draft is fed by `setDraftText()` (the window's draft editor, through a 200 ms debounce — one keystroke re-chunks the whole draft, the same CPU trap as the file editor); persistence of the draft text stays in the tools domain (`tools.draft`, its own 500 ms debounce).

The diagram shows a session's life across open, switch, edit, external change, save and close:

```mermaid
sequenceDiagram
    participant WC as WorkbenchContext
    participant MC as MarkdownPreviewController
    participant S1 as Session d1 (one.md)
    participant S2 as Session d2 (two.md)
    participant Doc as MarkdownDocument
    participant Page as the preview page

    WC->>MC: openFile("one.md")
    MC->>Doc: load() (byte shape recorded)
    MC->>S1: blocks + outline + rev=1
    MC->>MC: activate(d1), emit windowRequested()
    MC->>Page: setDocument (page fetches doc.json of d1)
    WC->>MC: openFile("two.md")
    MC->>S2: new session, activate(d2)
    MC->>Page: setDocument (d1's pending updateBlocks purged)
    Note over S1: d1 stays alive: dirty flag, mode, blocks kept
    MC->>MC: activate(d1) again (tab click)
    MC->>Page: setDocument (same docId, full rebuild)
    WC->>MC: activate(d1), setEditorText edits
    MC->>S1: re-chunk + diff -> updateBlocks (active session only)
    Doc-->>MC: externallyChangedByOther (watcher)
    MC->>MC: changedOnDisk on d1 (asked only when active)
    WC->>MC: saveNow() / saveSession(d1)
    MC->>Doc: save() (byte shape reproduced)
    MC->>MC: closeSession(d1)
    MC->>MC: activate most recent file session (or the draft)
    Note over MC: window close keeps every session; process exit drops them
```

Two invariants worth pinning:

- **Dirty flags are per session.** Editing one tab never flips another's dirty flag, and the close guard (below) inspects every dirty tab, not just the active one.
- **Signals are gated by activity.** A background session's dirty or read-only flip does not emit the controller's global signals — only the session's own state moves; the properties are correct the moment it becomes active again.

## The tab strip

The strip above the middle area renders `markdown.tabs`: one delegate per file session — title, a dirty dot (never colour-only), and a close button (`AIconButton`). Clicking a tab activates its session and switches the document preset; the delegate's root is a plain `Item` with the visuals in a child (the required-property shadowing rule). To its right sit the view switcher, the outline and search buttons, the text-size and width pickers and the More-actions menu — visible per the current capability, and the switcher now serves **both** presets: the prompt form renders the draft through the same pipeline.

The window couples preset and session: the prompt preset activates the draft session, the document preset activates the last file session the user had (`lastFileDocId`, so prompt ⇄ document round-trips return to the document being read, not the newest tab).

## Session persistence (restarts)

`markdown_session.json` (`MarkdownSessionStore`, workbench domain — session state spans tools and markdown, so only L3 may own it) remembers the scene across restarts:

- the tab group: each tab's **path** and **view mode** (no content — the files are re-read from disk);
- the active tab index;
- the preset and the per-preset panel/ribbon collapse states;
- a recent-documents MRU (20 entries), touched by every successful open.

Restoration happens once, when the window is first created during an app run (later destroy/reopen cycles use the controller's in-memory sessions): `WorkbenchContext::restoreMarkdownSession()` re-opens each path in order — a file that no longer exists is skipped with a log line, never failing the whole group — applies each tab's recorded mode, then activates the recorded tab. Writes are debounced 800 ms behind tab/active/preset/panel changes (dirty-flag flips ride the same debounce; per-keystroke persistence buys nothing) and flushed undebounced on close. A corrupted file discards the whole session with one warning: layout state is not worth rescuing. The privacy stance is deliberate — local paths only, no content, no encryption, no cleanup: "find what I had open" is the entire semantic.

The **start page** shows the top six of the MRU (file name plus parent folder, one click opens it as a tab), and `WorkbenchContext::recentMarkdownDocuments()` is its only source.

## File association (double-click a `.md`)

Three pieces, Windows only, none of them a lightweight viewer:

1. **Command line.** `app/main.cpp` scans the arguments for existing Markdown-suffixed paths (`isMarkdownPath()` — the same suffix verdict the file tree's menu item uses) and opens each as a tab via `previewMarkdown()` once the UI is loaded. A no-capability build reports the reason as a toast, exactly like a right-click there.
2. **Single-instance message.** A second instance with file arguments no longer sends a bare `"raise"` but `"open:<JSON path array>"` (`WorkbenchContext::encodeOpenMessage()` / `decodeOpenMessage()`; without a request the message is still `"raise"`). The first instance opens each path as a tab and raises the workbench window.
3. **The Settings action.** **Associate .md files** (Launchers section) writes HKCU only: the ProgId `AgentWorkbench.Markdown.1` with `shell\open\command` pointing at the current executable, registered in `OpenWithProgids` for `.md` and `.markdown`. **UserChoice is never touched** — Windows 10+ guards the default-app choice with a signature, and silently stealing it is neither reliable nor polite. The goal is to appear in the Open-with list; the default app is chosen in Windows Settings, and the UI copy says exactly that. A registry write blocked by policy fails with an honest error. Uninstall does not clean up the keys (harmless leftovers, the Windows convention).

Double-clicking a file in the explorer starts the **full application** — shell, sidebar, engine — not an instant viewer; that is a deliberate decision, and the honest cost is the startup time. Builds without the preview capability hide the association action entirely (registering an entry that would only ever produce an error toast is worse than not offering it).

## Files and classes

| File | Class / component | Responsibility | Collaborates with |
|---|---|---|---|
| `src/workbench/qml/MarkdownWorkbenchWindow.qml` | `MarkdownWorkbenchWindow` | The window: command bar slot, side panels, the file tab strip, the draft/document middle area (one split structure serving both presets), search bar, status line, dialogs, shortcuts, session save/restore (evolved from the former `MarkdownPreviewWindow.qml`; the resource alias moves from `markdown/` to `workbench/`) | `ToolsFacade`, `Markdown`, `MarkdownEdit`, `MarkdownContextMenu`, `workbench`, `theme` |
| `src/workbench/qml/WorkbenchRibbon.qml` | `WorkbenchRibbon` | The SARibbon command bar: two tabs, single-row default, style from `tools.ribbonStyle`, the one-way theme bridge; pure forwarding to the window's action layer | the window QML, `RibbonTheme`, `tools.*` |
| `src/workbench/qml/WorkbenchToolbar.qml` | `WorkbenchToolbar` | The no-ribbon fallback: a plain toolbar with the same actions, loaded when the ribbon file is absent | the window QML |
| `src/markdown/qml/MarkdownPreviewPane.qml` | `MarkdownPreviewPane` | The preview surface (the `WebEngineView` and its wiring), loaded by resource URL only when the capability exists | `Markdown`, the window QML |
| `src/shell/qml/MarkdownStartPage.qml` | `MarkdownStartPage` | The sidebar's **Markdown** start page: the open-workbench button plus the **Recent documents** list (top 6 of the MRU) — a thin shell, not the editor | `workbench.openMarkdownWorkbench()`, `workbench.previewMarkdown()`, `workbench.recentMarkdownDocuments()` |
| `src/workbench/BuiltinPages.{h,cpp}` | `BuiltinPages` | Registers the built-in pages, including the **Markdown** start page | `NavigationModel`, `WorkbenchContext` |
| `src/tools/ToolsStore.{h,cpp}` | `ToolsStore` | `tools.json` state: MRU workspace list, current workspace, prompt draft, and the `ribbon` section (`collapsed` / `style` / `followAppTheme` / `lastTabIndex`); `kMaxWorkspaces` = 20 | `ToolsFacade`, `core::JsonStore` |
| `src/tools/ToolsFacade.{h,cpp}` | `ToolsFacade` | QML facade: workspace properties, draft property, `addWorkspace`/`removeWorkspace`/`refresh`/`fileReference`, the flat tree model as `model`, and the four `ribbon*` interface preferences; 500 ms draft debounce plus destructor flush | the window QML, `app/main.cpp` (`tools` alias), `FileTreeModel`, `FileTreeFlatModel` |
| `src/tools/FileTreeModel.{h,cpp}` | `FileTreeModel` (with private `Node`) | Lazy directory tree over one workspace root; incremental `refresh()`; `QFileSystemWatcher` | `ToolsFacade`, `FileTreeFlatModel` |
| `src/tools/FileTreeFlatModel.{h,cpp}` | `FileTreeFlatModel` (with private `Row`) | Flattened projection for the QML `ListView`; owns the expanded-path set | the window QML, `FileTreeModel` |
| `src/tools/FileIcons.{h,cpp}` | `FileIcons` | Three lookup tables (file names / suffixes / folder names) plus fallbacks; `forFile()`/`forFolder()` | `FileTreeModel` (`IconRole`), `core::IconResolver` |
| `src/tools/MarkdownEdit.{h,cpp}` | `MarkdownEdit` | QML singleton (`AgentWorkbench.App`): `attach()`, `applyBold()`, `applyCode()`, `applyBullet()` — the formatting actions behind the command bar's Start tab and the editor context menu | the window QML, `MarkdownContextMenu`, `theme::Theme` |
| `src/tools/MarkdownHighlighter.{h,cpp}` | `MarkdownHighlighter`, `MarkdownColors` | Lightweight per-block Markdown highlighter; colour tokens | `MarkdownEdit` |
| `src/tools/qml/MarkdownContextMenu.qml` | `MarkdownContextMenu` | The draft editor's `AMenu`: top formatting toolbar + undo/redo/cut/copy/paste items | `MarkdownEdit`, `AMenu`/`AMenuItem` |
| `src/markdown/MarkdownDocument.{h,cpp}` | `MarkdownDocument` | One file on disk: text, dirty flag, read-only verdict, and the **byte shape it was loaded as** (BOM, line endings, UTF-8 validity); `setVirtual()` turns it into a file-less carrier (the draft session: never dirty, `save()` refused, the 2 MB edit cap still applies) | `MarkdownPreviewController`, `core::OpResult` |
| `src/markdown/MarkdownChunker.{h,cpp}` | `MarkdownChunker`, `MarkdownBlock` | Top-level block splitting, the minimal replacement range between two splits, the per-block hash | `MarkdownPreviewController`, `renderer.js`, `scripts/spike-markdown-chunker.js` |
| `src/markdown/MarkdownRuntime.{h,cpp}` | `MarkdownRuntime` | All page-side strings and pure computation: URL layout, MIME table, resource mapping, theme → CSS variables, page strings, outline, command/event name tables, capability verdict | everything below; the only C++ the page protocol is written against |
| `src/markdown/MarkdownPreviewController.{h,cpp}` | `MarkdownPreviewController` | QML singleton `Markdown`: the **session container** (one `Session` per open document plus the draft, `docId`-addressed; `kMaxFileSessions` = 8, `isMarkdownPath()` is the single suffix verdict), the pending-command queue, and the landing point for exports; `saveSession(docId)` saves a file session without switching activation | `core::Settings`, `theme::Theme`, `MarkdownRuntimeHandler`, the window QML |
| `src/markdown/webengine/MarkdownPreviewProvider.{h,cpp}` | `MarkdownPreviewProvider` | Registers the scheme, owns the preview-only profile, installs the handler, and tells the controller the surface exists | `app/main.cpp`, `MarkdownRuntimeHandler` |
| `src/markdown/webengine/MarkdownRuntimeHandler.{h,cpp}` | `MarkdownRuntimeHandler` | The single supplier for `awbmd://runtime/…`, with an in-memory cache for vendor bytes | `MarkdownPreviewController` |
| `src/markdown/webengine/runtime/*` | `shell.html`, `bridge.js`, `renderer.js`, `markdown-config.js`, `math-rule.js`, `diagram.js`, `find.js`, `theme.css` | The page: bootstrap and command dispatch, the chunked pipeline, the markdown-it configuration, KaTeX/task/alert rules, deferred mermaid, in-document search | `MarkdownRuntime` (protocol), `third_party/markdown-runtime/` |
| `src/workbench/WorkbenchContext.{h,cpp}` | `WorkbenchContext` | The cross-domain entry: `previewMarkdown()` / `canPreviewMarkdown()` for the document form, `openMarkdownWorkbench()` for the prompt form, `restoreMarkdownSession()` / `saveMarkdownSession()` / `recentMarkdownDocuments()` for the session file and MRU, and `registerMarkdownFileAssociation()` / `markdownFileAssociationRegistered()` plus the single-instance `encodeOpenMessage()` / `decodeOpenMessage()` for file association — for domains that may not name each other | `MarkdownPreviewController`, `MarkdownSessionStore`, the window QML, the start page, `app/main.cpp` |
| `src/workbench/MarkdownSessionStore.{h,cpp}` | `MarkdownSessionStore`, `MarkdownSessionState` | `markdown_session.json`: tab group (path + mode), active index, preset and panel states, the 20-entry recent-documents MRU; corrupted file = default state with a warning | `WorkbenchContext`, `core::JsonStore` |
| `src/shell/qml/MainWindow.qml` | `MainWindow` | Hosts the window's `Loader`; writes the window's `initialPreset` from the opening intent | `Markdown`, `WorkbenchContext` |
| `cmake/AwbOptions.cmake`, `app/CMakeLists.txt` | `AWB_ENABLE_RIBBON` wiring | SARibbon submodule build and the window's resource registration | `third_party/SARibbon` |
| `third_party/markdown-runtime/` | — | Vendored browser builds plus `manifest.json` (versions, licences, sha256) | `tst_markdown`, `scripts/update-markdown-runtime.sh` |

Outside the modules the window leans on `core::Paths::dataRoot()`, `config/default_file_icons.json`, `theme::Theme`, the shell `A*` components, `workbench.copyText()`/`notify()` and `ui.pickFolder()`.

## Frontend design

### Window skeleton and the two presets

```
┌────────────────────────────────────────────────────────────────┐
│ Command bar: WorkbenchRibbon.qml (SARibbon) or the toolbar     │
│              fallback — tabs: Start · View                      │
├───────────────┬────────────────────────────────┬───────────────┤
│ left panel    │ middle area                    │ outline panel │
│ workspace     │ file tab strip (dirty dots)    │ (any session, │
│ + file tree   │ Preview / Split / Source       │  behind its   │
│               │ search bar · status line       │  toggle)      │
└───────────────┴────────────────────────────────┴───────────────┘
```

- The left panel is the migrated prompt-page tree panel: a compact header row (workspace `AComboBox` with `Layout.fillWidth`, folder-picker `AIconButton`, refresh `AIconButton`) above the tree. It collapses to a narrow, clickable edge; the document-form preset starts it collapsed. Neither the panel nor the editor takes a corner radius where they meet — flush squares at the junction.
- The right panel is the outline, behind the outline toggle, serving the **active session** — the draft has headings too, so it is no longer document-form-only.
- The middle area is headed by the **file tab strip** (`Repeater` over `markdown.tabs`): one tab per file session with its title, dirty dot and close button; the strip also carries the view switcher, the outline and search buttons, the text-size and column-width pickers, and the **More actions** menu. The switcher serves both presets — the draft renders through the same pipeline as the files.
- The middle body is **one split structure for both presets**: the editor slot holds the draft editor when the draft session is active and the file source editor otherwise, and the preview pane renders the active session. It is a hand-rolled two-pane layout, not a `SplitView`: two of the three view states squeeze one pane to zero width, and `SplitView`'s handling of `visible` panes differs across Qt versions. The divider is 4 px visually with a 12 px hit area; the split proportion (`editorShare`) is window state — dragging it should not touch the disk.
- Under the middle area sits the **status line**: in the prompt form it shows the draft's character and line counts; in the document form it shows the file's path, the **Unsaved changes** mark, the read-only reason when one applies, the file size, and the renderer's version numbers. State is never colour-only — dirty and read-only are written out as words.
- The prompt form's ribbon preferences (collapse state, style row, last tab, theme following) persist in `tools.json`'s `ribbon` section. The per-preset panel states and the document form's ribbon collapse persist in the **session file**; within one app run they additionally survive window close/reopen, because the window reads them back from the restore call.

### The draft editor

The editor is the migrated prompt editor, now living in the window's editor slot. A vertical `Flickable` (`objectName: "promptScroll"`, `clip`, `StopAtBounds`, `ScrollBar.vertical: AScrollBar {}`) carries an `ATextArea` (`objectName: "promptEditor"`) through the `TextArea.flickable` attached property. The text is initialised once in `Component.onCompleted` (`text = tools.draft`, then `MarkdownEdit.attach(promptEditor)`) rather than two-way bound — a permanent binding would fight the user's typing. The write path is **double**: `onTextChanged` writes `tools.draft` (the tools domain persists it through its own 500 ms debounce) and restarts the 200 ms `draftSyncTimer`, which feeds the draft **session** via `setDraftText()` — one keystroke re-chunks the whole draft, so per-keystroke feeding burns CPU on intermediate states. The placeholder states the contract in so many words: Enter only inserts a new line, nothing is sent from this window.

A `DropArea` filling the editor accepts only drags from the file tree (`drop.source.isFileReferenceDrag`), computes the insertion offset with `promptEditor.positionAt(drop.x, drop.y)` and inserts `drop.text`; the local drop coordinates are already document coordinates. A right-button `MouseArea` sets `editorMenu.editor = promptEditor` and pops `MarkdownContextMenu`.

### The document body

In the document form the middle body holds the source editor and the preview pane side by side. The source editor is a second `ATextArea` (`editorArea`) in the same `Flickable` structure, in the theme's mono family (tables and code blocks depend on the alignment), read-only when the document is not editable, and carrying the same `MarkdownEdit.attach()` highlighting as the draft — read-only documents get it too, because reading source benefits as much as writing it. Its text is written on init and on external reloads, never two-way bound, and edits are handed to the controller through a 200 ms debounce: one keystroke re-chunks and diffs the whole document, and paying that per keystroke on a 2 MB file burns CPU on intermediate states that are about to be overwritten.

The preview pane is a `Loader` (`active: markdown.available`) whose `source` is the resource URL of `MarkdownPreviewPane.qml` — registered into the binary only in builds that can run it, so a build without WebEngine never creates a `WebEngineView` at all. A load failure surfaces as an error toast, not a silent blank.

### The file tree panel

- **Workspace `AComboBox`.** `model: tools.workspaces`. Both `contentItem` and `delegate` are overridden — the default `ItemDelegate` background is a hard-coded light palette that is unreadable in dark themes — with a per-item remove `AIconButton` in the content row.
- **File tree `ListView` (`objectName: "fileTree"`).** `model: tools.model` (the flat projection), `AScrollBar.vertical`, an empty-state label when the workspace is set but `tools.model.visibleCount === 0`, and an `AEmptyState` when no workspace is selected.
- **Delegate.** Every role is a `required property` (`name`, `path`, `relativePath`, `isDir`, `iconSource`, `depth`, `expanded`, `hasChildren`) **and** `index` — declaring any required property clears `contextObject` in Qt 5.15, and a bare `index` would throw and abort the single-click expand handler. A `rebinding` flag toggled by a `Connections` on `onModelReset` keeps the chevron animation quiet during model resets. Drag-out uses `Drag.Automatic` with `Drag.mimeData` text from `tools.fileReference(relativePath)`. Two `TapHandler`s do single-click expand and double-click insert; a right-button `MouseArea` opens an `AMenu` (`objectName: "treeRowMenu"`) holding two copy-path items plus — when `workbench.canPreviewMarkdown(treeRow.path)` says yes — **Preview Markdown** and a separator above the copy items. The extension list and the capability check both live in the preview domain; the QML never tests a suffix, so a build without the capability simply shows the two copy items.
- **`AEmptyState` constraint.** Its root is a plain `Item` and must not carry `anchors`; inside it use `Column`, not `ColumnLayout` (Qt 5 recursive re-arrange).

### The editor context menu and the command bar skins

`MarkdownContextMenu.qml` is an `AMenu` whose first child is a plain `Item` — a 34 px toolbar holding three `AIconButton`s (Bold, Code, Bulleted list) — so mixing it with `AMenuItem`s yields "toolbar on top, items below" with a faint divider. Actions run through `run(action)`, which closes the menu, returns focus to `editor`, and only then executes: the editor's selection and insertion must receive focus first, or input and selection writes land nowhere. The same discipline governs the command bar: both skins (`WorkbenchRibbon.qml`, `WorkbenchToolbar.qml`) only forward to the window's action layer, which itself returns focus to the active editor before applying a format or clipboard operation.

## Backend design

### `ToolsStore`

Owns `tools.json` under the data root. The keys are `workspaces` (string array, MRU first), `current` (current workspace absolute path), `draft` (the full prompt) and `ribbon` (an object with `collapsed`, `style`, `followAppTheme`, `lastTabIndex`). **The key names are part of the on-disk format; renaming one loses user data.** `load()` skips non-string entries and dedupes, and clears `current` when it no longer appears in the list. The `ribbon` section is read with tolerance: a missing section, wrong types or an unknown style string all fall back to the defaults (expanded, single row, following on, first tab) rather than failing the load. `addWorkspace()` removes the path and prepends it (one MRU-touch path), evicts past `kMaxWorkspaces` = 20 from the tail, and sets it current. `removeWorkspace()` promotes the remaining head if the current entry goes. `setCurrentWorkspace()` accepts the empty string. `setDraft()` returns without writing when the text is unchanged. `save()` writes atomically through `core::JsonStore`.

### `ToolsFacade`

Properties: `model` (CONSTANT, the flat projection), `workspaces`, `currentWorkspace` (WRITE), `draft` (WRITE), plus the four ribbon preferences — `ribbonCollapsed`, `ribbonStyle`, `ribbonFollowAppTheme`, `ribbonLastTabIndex` — each with a `WRITE` that persists immediately and one shared `ribbonSettingsChanged()` signal. Invokables: `addWorkspace`, `removeWorkspace`, `refresh`, `fileReference`. The `awb::core::OpResult` return type of `addWorkspace`/`removeWorkspace` must stay fully qualified: Qt 5's moc records the written name while QML resolves the `QMetaType` registration name, so a short name yields "Unknown method return type" and the button does nothing.

- **Draft debounce.** `setDraft()` updates the buffer, emits `draftChanged()` and restarts a single-shot 500 ms `QTimer`; `persistDraft()` runs on timeout, and the destructor flushes too, so the last keystrokes survive shutdown.
- **Icon table ordering.** The constructor calls `loadUserIconFile(dataRoot + "/file_icons.json")` **before** setting the root — the model does not retroactively emit `dataChanged` for rendered rows.
- **Root selection.** The tree root is set only when the remembered directory exists; otherwise the tree stays empty rather than showing a permanently empty fake root.
- **`refresh()`.** Async-shaped but synchronously completed today; forwards to `FileTreeModel::refresh()` and relays `refreshed` as `refreshFinished()`, so QML cannot tell watcher-triggered refreshes from manual ones.

### `FileTreeModel`, `FileTreeFlatModel`, `FileIcons`

`FileTreeModel` is a lazy tree over one root: `hasChildren()` is true for any unfetched directory (the expander shows before reading), `fetchMore()` reads it, and `fetchChildren()` is an idempotent `Q_INVOKABLE` fallback. `setRootPath()` builds the whole staging tree — including the top level — outside `beginResetModel`; a reset followed immediately by inserted rows is double-counted by the view. Incremental `refresh()` walks every *fetched* directory (collapsed-but-fetched ones too, or their stale rows would show on the next expand) and reconciles in three steps inside `begin`/`end` pairs: match old nodes by name into the desired sequence (a type change is delete-plus-insert), erase runs back to front, insert runs front to back. Survivors keep their subtrees, `fetched` state and expansion state; when nothing changed, no row signal is emitted at all. `QFileSystemWatcher::directoryChanged` is connected through a 300 ms single-shot debounce; `armWatchers()` rebuilds the watch list after any structural change. Sort: directories first, then case-insensitive file name, with a case-sensitive tie-break for stability.

`FileTreeFlatModel` projects the tree into a linear row order and holds the expanded state itself (Qt 6.3's QML `TreeView` does not exist in Qt 5.15, so both versions share one delegate). It consumes only `modelReset` (root change → clear the expanded paths, rebuild) and `FileTreeModel::refreshed`; the per-directory `rows*` signals during a refresh are not consumed, avoiding a rebuild storm. Expanded state is a set of paths relative to the root, so it survives a refresh. `toggleExpanded(row)` expands (fetch fallback, recursive projection into one `rowsInserted`) or **recursively** collapses. The role enum is mapped explicitly to the source model's enum because the numeric values differ.

`FileIcons` holds three tables — `fileNames`, `suffixes`, `folderNames` — plus a fallback per kind; the constructor merges the built-in `:/config/default_file_icons.json` and `loadUserFile()` overlays `<dataRoot>/file_icons.json` with user keys winning. Lookup is case-insensitive, ordered complete file name → suffix → fallback, and every value is normalised through `core::IconResolver`.

### `MarkdownEdit` and `MarkdownHighlighter`

`MarkdownEdit` is the QML singleton behind the formatting actions. `attach(textArea)` reads the `QQuickTextDocument` and installs a `MarkdownHighlighter` parented to the document; a second call for the same document is a no-op. Colours come from the current `theme::Theme` and are re-applied on `Theme::changed` followed by `rehighlight()`; dead highlighter pointers are pruned via `QPointer`. Both the draft editor and the document source editor are its attached clients.

The three formatting actions are pure static cursor operations, so tests drive them against a `QTextDocument`:

- `applyBoldToCursor()`: empty selection inserts `****` and parks the cursor in the middle; otherwise it wraps the trimmed selection in `**`, leaving outer whitespace outside the markers, and re-selects the content.
- `applyCodeToCursor()`: empty selection inserts ` `` `; a single-line selection gets inline backticks; a selection containing a newline gets a fenced block (the `` ``` `` lines are placed after the leading newline and before the trailing newline so existing separators are reused).
- `applyBulletToCursor()`: adds `- ` at the start of every line in the selection (or the cursor's line) and re-selects the whole affected range.

Each action runs inside one `beginEditBlock`/`endEditBlock`, so a single undo reverts it. The selection read goes through the control's `selectionStart`/`selectionEnd` properties and does **not** depend on focus — which is why both the context menu and the command bar can take focus and still operate on the editor's selection.

### Draft debounce and lazy tree

The first diagram shows the draft debounce and the lazy file tree from the window and facade side.

```mermaid
sequenceDiagram
    participant Window as MarkdownWorkbenchWindow.qml
    participant Facade as ToolsFacade
    participant Store as ToolsStore
    participant Tree as FileTreeModel
    participant Flat as FileTreeFlatModel

    Window->>Facade: window creation reads tools.json
    Facade->>Store: load()
    Store-->>Facade: workspaces / current / draft / ribbon
    Facade->>Tree: loadUserIconFile then setRootPath
    Tree-->>Facade: top-level rows (deeper levels lazy)
    Facade->>Flat: setSourceModel(tree)
    Flat-->>Window: flat rows for the file tree

    Window->>Facade: onTextChanged sets draft
    Facade->>Facade: restart 500 ms debounce timer
    Note over Facade: further keystrokes keep restarting it
    Facade->>Store: persistDraft() on timeout
    Store-->>Facade: tools.json written atomically

    Window->>Flat: single click on a directory row
    Flat->>Tree: fetchChildren(source index)
    Tree-->>Tree: read directory, lazy still below
    Flat-->>Window: rowsInserted for the subtree

    Tree->>Tree: directoryChanged debounced 300 ms then refresh()
    Tree->>Flat: refreshed()
    Flat-->>Window: rebuild keeps expanded paths
```

The second diagram shows the incremental refresh reconciliation inside `FileTreeModel`.

```mermaid
flowchart TD
    A["FileTreeModel::refresh()"] --> B["syncNode(root): fetched directories only"]
    B --> C["syncChildren(node): readSortedEntries() from disk"]
    C --> D["match old nodes by name into desired[]; type change means delete+insert"]
    D --> E{"survivors == old children and no new nodes?"}
    E -- yes --> F["no row signal at all"]
    E -- no --> G["step 2: erase runs back to front with beginRemoveRows"]
    G --> H["step 3: insert runs front to back with beginInsertRows"]
    H --> I["renumberChildren(); survivors keep subtree and fetched"]
    I --> J["armWatchers() and refreshed()"]
    F --> J
```

### Workspace memory and file references

The workspace list is MRU-ordered, capped at 20, and the current entry is always either in the list or empty; deleting the current entry promotes the head of the remaining list; the empty string is a legitimate current value. `ToolsFacade::fileReference()` produces `` `./relative/path` `` — backtick wrapped, forward slashes — and drag-and-drop and double-click both use it, so the prompt always receives the same shape.

## The rendering pipeline

### Why a custom scheme, and the shape of `awbmd://`

The preview page needs three things at once: it must `fetch()` its own document, load KaTeX's `woff2` fonts through `@font-face`, and reference images that sit next to the Markdown file — all while nothing is written to disk and no port is opened. `data:` URLs fail the first two (opaque origin); unpacking the runtime to the data directory puts 4 MB of third-party JavaScript into the user's folder; a localhost HTTP server would need a port, a lifecycle and a token story. So the adapter registers a scheme, **`awbmd://runtime/…`**, and one handler serves everything from that single origin.

| URL | Served from | Notes |
|---|---|---|
| `awbmd://runtime/app/shell.html` | `:/markdown/app/` | The page the view loads; `MarkdownPreviewController::pageUrl()` is its only construction site |
| `awbmd://runtime/app/<file>.js` / `.css` | `:/markdown/app/` | This project's own page runtime |
| `awbmd://runtime/vendor/<dir>/<file>` | `:/markdown/vendor/` | markdown-it, KaTeX (+ fonts), mermaid, highlight.js |
| `awbmd://runtime/doc.json?rev=N` | `MarkdownPreviewController::documentBodyJson()` | Text, `strings`, CSS variables, outline, block count; `rev` only changes when content changes |
| `awbmd://runtime/asset/<docId>/<rel>` | The document's own folder | Relative image paths resolve against the document directory; the resolved absolute path must stay inside it |
| `awbmd://runtime/event?k=<kind>&p=<json>` | — | Page → C++ beacon; answers a 1×1 PNG |

URL shapes are decided in exactly one place, `MarkdownRuntime`; `MarkdownRuntimeHandler::routeOf()` maps a URL to one of `Resource`, `Document`, `Asset`, `Event`, `Unknown`, and anything unrecognised is `UrlNotFound` rather than a guess. Beacons are GET-only by construction and the handler rejects any other method, so the page cannot turn this channel into a way of touching the local machine.

**The registration order is a startup contract.** `MarkdownPreviewProvider::registerScheme()` must run *before* `QtWebEngineQuick::initialize()`, because the engine freezes its scheme table when the first context is created and silently ignores later registrations. The failure is not a 404: the handler still serves documents, scripts and fonts, while `fetch()` dies with "URL scheme awbmd is not supported" — prose renders, formulas and diagrams never do, and the build plus every pure-function test stays green. `MarkdownPreviewProvider` therefore verifies its own registration on construction via `QWebEngineUrlScheme::schemeByName()` and, if it is missing, leaves `available` false and logs why. The syntax is `QWebEngineUrlScheme::Syntax::Host`, not `HostAndPort` (with `HostAndPort` a default port is required, and leaving it unspecified makes `registerScheme()` return early with a single warning — precisely the half-broken state above). Flags: `SecureScheme`, `LocalScheme`, `LocalAccessAllowed`, `CorsEnabled`, `ServiceWorkersAllowed`, plus `FetchApiAllowed` on Qt ≥ 6.6 (the flag does not exist on 6.5, where the page receives its text through `runJavaScript` instead of fetching it).

### The pure layer: document, blocks, page strings

`src/markdown/` contains no WebEngine; only `src/markdown/webengine/` (the `awb_markdown_webengine` adapter) links the engine. That split buys the ability to compile and test the whole state machine in a build that cannot render anything.

`MarkdownDocument` exists so that saving cannot rewrite a file: the load records the shape (`m_bom`, `m_crlf`, `m_validUtf8`) and `encodeText()` reproduces it. `m_crlf` is only set when **every** newline is CRLF — in a mixed file, leaving `\r` inside the text keeps `encodeText(text)` byte-identical to the source. Two size caps come out of the product requirement (smooth at a few hundred KB to 2 MB): above `kOpenCapBytes` the file is refused outright, above `kEditCapBytes` it previews read-only. Invalid UTF-8 is a third source of read-only — decoding already lost information, and writing it back would replace the user's bytes with U+FFFD. External changes arrive through `QFileSystemWatcher`, with one deliberate addition: `MarkdownPreviewController::refreshDiskState()` re-compares against the on-disk snapshot on window activation as well, because on Windows the watcher silently loses files that are deleted and recreated. Saving never reports itself as an external change: `save()` re-records the snapshot.

`MarkdownChunker` is the reason incremental rendering is possible at all: its output blocks map one-to-one onto `<section>` elements in the page, so `startLine`/`endLine` are also the anchors for "editor line → preview block". The equivalence requirement is what makes it a line-level state machine rather than a split on blank lines — fences, display math, setext underlines, loose-list continuations and table runs are all handled explicitly, because a wrong block boundary changes semantics. `scripts/spike-markdown-chunker.js` checks whole-document rendering against block-by-block rendering over the repository's own Markdown corpus, importing the **same** parser configuration as the page.

`MarkdownRuntime` collects everything the page cares about that C++ must decide: URL construction, MIME types, theme tokens → `--awb-*` CSS custom properties, the page's string dictionary (`pageStrings()`), outline extraction, and the two protocol name tables. There are no translatable literals in the `.js` files at all — they are invisible to `lupdate`, so a string that lived there would simply never be translated. `commandNames()` and `eventKinds()` are the single authority for the protocol: `enqueue()` refuses names that are not in the table, and the page's dispatch branches are asserted against the same list.

### The renderer: passes, batches, and visibility

`renderer.js` is the performance work. Its doc comment records the measurement: on a 486 KB / 3150 block document, parsing and element construction take about 1.9 s of actual main-thread work, mounting the prepared blocks takes ~0.03 s, and forcing a full-document layout is also ~0.03 s. So this pipeline does not reduce the total — the parsing cannot be avoided — it removes the **one-shot** character of it, so the first screen pays only the first pass.

Three mechanisms, kept deliberately separate:

1. **Passes.** Parsing yields to the event loop after `PASS_BUDGET_MS` (48 ms — under the 50 ms long-task threshold, because past it input starts being dropped), with a more generous `FIRST_BUDGET_MS` for the first pass and a `SLICE_BLOCKS` cap so one pathological block cannot monopolise a pass.
2. **Batched mounts.** DOM cost is paid per *insertion into the tree*, not per block. Blocks accumulate into a batch and are appended once `MOUNT_BATCH_BLOCKS` is reached, or the parser has drained, or the batch has waited `MOUNT_MAX_WAIT_MS` — that last one so a small document never leaves content parked.
3. **CSS `content-visibility`** skips layout and paint for offscreen blocks (`theme.css`). This is why layout is nearly free, and it is also why there is **no JS window virtualisation**: virtualisation would destroy text selection, native scroll anchoring, and accessibility.

Scheduling is hand-rolled rather than `requestIdleCallback` or `requestAnimationFrame`: the same work measured under idle callbacks takes an order of magnitude longer (Chromium's idle budget starves once there are thousands of callbacks), and frame-bound work is capped at the frame rate rather than at the time budget. Editing uses `updateBlocks`: C++ computes the minimal replacement range from the per-block hash (`MarkdownChunker::diff()`), and the page re-renders only those `<section>`s — typing one character must not re-layout the document.

Diagrams are a separate layer for a size reason: the mermaid bundle is 2.6 MB, and a document with no diagrams should not pay to load it. `diagram.js` injects the `<script>` only when the first diagram gets near the viewport; concurrent renders are capped at 2, SVG results are cached by diagram source, and beyond `DEFER_LIMIT` (40 diagrams) the placeholders become click-to-render. Search is also homegrown: Chromium's native find is unreliable against `content-visibility` content, so `find.js` counts and jumps itself — accepting that only **mounted** blocks are counted and that a match spanning inline markup is not found.

### The command queue and the page protocol

```mermaid
sequenceDiagram
    participant Tree as MarkdownWorkbenchWindow.qml
    participant WC as WorkbenchContext
    participant MC as MarkdownPreviewController
    participant MW as MainWindow.qml Loader
    participant SH as MarkdownRuntimeHandler
    participant Page as bridge.js / renderer.js

    Tree->>WC: previewMarkdown(path)
    WC->>MC: openFile(path)
    MC->>MC: MarkdownDocument::load + MarkdownChunker::split
    MC-->>WC: OpResult ok
    MC->>MW: windowRequested()
    MW->>SH: view loads awbmd://runtime/app/shell.html
    Page->>SH: fetch doc.json?rev=N
    SH->>MC: documentBodyJson()
    MC-->>SH: text + strings + cssVars + outline
    MC->>Page: setDocument (drained from the queue)
    Page->>Page: passes, batched mounts
    Page-->>MC: frame beacon (progress, timings)
    Page-->>MC: ready beacon (after mount)
```

Both halves of this lifecycle are asynchronous and neither may assume the other. The queue exists because QML's only route into the page is `WebEngineView.runJavaScript`, and a command sent before the page has booted is simply lost: `MarkdownPreviewController::enqueue()` parks commands, emits `commandQueued()`, and the adapter drains at two moments — when the view reports ready, and whenever a new command arrives. Only the newest command of each kind survives (five consecutive edits send one `updateBlocks`).

Direction of travel is split by payload size on purpose. Commands C++ → page go through `runJavaScript("AWB.dispatch(<json>)")`. Large results the page → C++ come back as the *return value* of a `runJavaScript` call (`serialize()` and `renderAll()` return the exported HTML that way), because the beacon is a GET URL and `kEventPayloadBytes` (8 KiB) truncates it. Small events — `ready`, `frame`, `error`, `find` — use the beacon. Keeping `markup` out of the event table is deliberate: a table that contains one large payload invites the next person to add another.

### One preview surface

The rendering pipeline lives behind `MarkdownPreviewPane.qml`, loaded by resource URL only when `markdown.available` — a build without WebEngine never creates a `WebEngineView`. There is exactly one preview pane in the window, and it renders **whatever the active session is**: a file tab or the draft. In a build without the capability the pane is absent, the view switcher disappears (absent, not greyed out — the same principle the preview entry follows), and the draft's view mode degrades to source.

## Entry points and wiring

The file tree asks, and never decides. Its context menu contains `visible: workbench.canPreviewMarkdown(treeRow.path)` and `onTriggered: workbench.previewMarkdown(treeRow.path)`. The suffix rules live in the preview domain (`isMarkdownPath()`); the QML only asks about a path it already has. `canPreviewMarkdown()` is a *method* rather than a binding source, and that matters: the expression does not re-evaluate, so the item's visibility is computed at menu build time — fine here, because the capability cannot change while the process is running.

The same `previewMarkdown()` is the landing point for every "open a Markdown file" intent: the file tree, a recent-documents row on the start page, a command-line argument, a second-instance message, and a double-clicked associated file in the explorer. One entry, one set of failure paths, one MRU touch.

`WorkbenchContext` is the only place that may name both domains (`src/tools` must not include `src/markdown`; gate rule 1). It holds a nullable pointer to the preview controller, set by `setMarkdownPreview()` from `app/main.cpp`. When it is null — or when the controller reports itself unavailable — `previewMarkdown()` produces a toast with the reason and nothing else, so the failure has one explanation path instead of two. The second intent, `openMarkdownWorkbench()`, is what the start page's button calls; it opens (or raises) the window in the prompt form. The session intents — `restoreMarkdownSession()` on the window's first creation, `saveMarkdownSession()` behind the window's changes — and the file-association pair live here too, all on the same object the QML already talks to.

`MainWindow.qml` hosts the window through a `Loader` and uses `source` (a resource URL string) rather than `sourceComponent` (a type name): the command bar and the preview surface are registered into the QML module only in builds that can run them, and addressing the window by resource URL keeps the shell independent of which variant exists. The `Loader`'s `onLoaded` writes the window's `initialPreset` property — `"document"` when the opening intent was `previewMarkdown()`, `"prompt"` when it was `openMarkdownWorkbench()` — and the window applies the preset idempotently before it appears. Loader failures are surfaced as a toast on purpose — otherwise the symptom of a mis-registered resource is "right-click does nothing", which is much harder to diagnose than an error box.

The startup side of file association and of the auto-open preference also lives in `app/main.cpp`: the argument scan and the second-instance message decoding run after the engine loaded (the UI is up, the tabs open through the normal path), and `markdown.autoOpenWorkbench` emits the open-workbench intent right after the tray wiring — the same signal the start page's button produces.

## View settings, outline, export, external browser

The view settings are read at controller construction by `loadView()` and written back by `persistView()`. The three **global** preferences — `fontSizePx`, `contentWidthPx`, `showOutline` — apply to every session; `0` means "use the theme's measurement" for the two sizes, and `contentWidthPx` beyond the viewport is how **Full width** is expressed. The **mode** (`preview` / `split` / `source`) is **per session**: each tab remembers its own, and a session created from the session file takes back the mode it was saved with; the settings value is the default for newly opened sessions, and `setMode()` syncs it so the preference tracks the user's latest choice. Switching the view on the draft or a file both route through the same `setView` command.

The outline comes from the block sequence (`MarkdownRuntime::outlineFromBlocks()`) with `blockIndex` as its positioning anchor, so clicking an outline row is a `scrollTo` command with a block index, not a fragile DOM query. Heading text is stripped of emphasis, links, inline code, images and HTML tags first. The outline follows the active session — the draft's headings are one click away.

Export and "open in browser" both need the page's own serialisation, so both go through `requestFullRender()` (flush the lazily-rendered diagrams) and then `requestMarkup(purpose)`. The page returns the HTML, and **`reportMarkup()` is the only write site** — QML never touches the filesystem. The export directory is per session: a user choice made on one document does not leak to the next (a leftover choice would export a README into some other project). Landing rules differ by purpose: `"export"` writes next to the document unless the user picked a folder through `setExportDirectory()` (exported HTML references images **relatively**, so only a file beside the document resolves them); `"browser"` writes to `core::Paths::markdownPreviewDir()` (`<dataRoot>/preview`), because an external browser cannot resolve `awbmd://` at all. `exportFileName()` suggests the document's base name with `.html`, falling back to `Markdown.html` when no document is loaded.

## Capability gating: Qt 5 and no WebEngine

`MarkdownRuntime::isSupportedBuild()` is a compile-time verdict (Qt 6 **and** WebEngine), and the same condition decides whether `awb_markdown_webengine` is linked, whether the `:/markdown/` runtime is embedded, and whether the preview pane is registered. Keeping them in one place is the whole point — disagreement between them shows up as "menu item is clickable, page 404s".

At runtime the verdict is refined, not relaxed: `available` is `m_surfaceReady && isSupportedBuild()`, where `m_surfaceReady` is set by the provider once the handler is actually installed on a profile. `unavailableReason()` gives the user the sentence. In a build without the capability the file tree's Preview Markdown item never appears, the preview pane's `Loader` never activates, the view switcher is absent, and a document-preset request falls back to the prompt form — the feature is absent rather than present and broken. The prompt form, needing none of this, stays fully usable, and the draft's view mode degrades to source.

Document-tab and file-association entry points report their reason rather than failing silently: an open request that reaches a no-capability build produces the honest toast (the association action is hidden there in the first place, since registering an entry that can only ever error is worse than not offering it).

The command bar adds a second axis to the matrix: `AWB_ENABLE_RIBBON=ON` and `OFF` must each configure, build and pass the tests, with the OFF build showing the toolbar fallback described above.

## Vendored runtime and how to update it

`third_party/markdown-runtime/` holds prebuilt browser bundles, committed as build artifacts. There is no CDN, no build-time download and no submodule pointer beyond the checkout: a machine packaging a release may be on an intranet with nothing reachable, and the app must render offline. `manifest.json` records version, licence, byte size and sha256 per file, and `tst_markdown` recomputes those hashes — an edited or truncated vendor file fails the test suite instead of becoming a mysterious rendering bug.

Only single-file browser builds are taken: mermaid's ESM form ships as a `chunks/*.mjs` graph, which turns "serve local assets from a custom scheme" into a dynamic-import resolution problem, while `mermaid.min.js` in 11.x is a self-contained IIFE. KaTeX fonts are woff2 only; highlight.js comes from the CDN-assets package. The script is never run by the build, and `.gitattributes` marks the directory `-text` because an EOL conversion would silently break the hash comparison.

```bash
bash scripts/update-markdown-runtime.sh                 # pinned versions, needs network + Node 20+
bash scripts/update-markdown-runtime.sh --set mermaid 11.18.0
bash scripts/build.sh --test                            # tst_markdown asserts the sha256 table
```

## Costs and deliberate limits

- **No send path, ever.** The window has no send action and no delivery channel other than copy; Enter inserts a newline. Any change that introduces a send path contradicts the feature's reason to exist.
- **One render surface.** Switching sessions is a full page rebuild (`setDocument`); only the active session gets incremental edits. The first screen of a newly switched tab pays the first pass again — the "first paint bounded by the first pass" guarantee does not extend to free tab switching.
- **Eight file tabs.** The ninth open is refused with an explanation; nothing is silently evicted.
- **The session file holds paths, not content.** A file that changed on disk between runs is re-read as it is now; the session restores the *layout*, not the texts.
- **Double-clicking an associated file starts the full application.** No lightweight viewer process exists by decision; the honest cost is the startup time.
- **About 4 MB is embedded in the Qt 6 + WebEngine binary**, and 2.6 MB of that is mermaid — loaded only by documents that actually contain diagrams.
- **Inline HTML is not rendered** (`html: false`). Previewing someone else's Markdown over a channel with no CSP is a script-injection surface; the cost is that a document written with embedded `<br>` or `<details>` shows those tags as text.
- **One live render surface, many documents.** The command queue and the `doc.json` body serve the active session; the `docId` asset namespace resolves for every live session. The window is reused and its content swapped, never pooled.
- **Search counts only mounted blocks**, and cannot match across inline markup.
- **More than 40 diagrams means click-to-render.**
- **A file above `kOpenCapBytes` is refused** rather than partially shown.
- **`content-visibility` means the browser's own Ctrl+F is not usable here**, which is why search has to be a feature rather than an afterthought.
- **mermaid stays on 11.x** until its 12.x `dist` layout is verified to still ship a self-contained IIFE.
- **The rAF/idle freeze is a real constraint on verification**: a `WebEngineView` in a hidden window does not pump animation frames or idle callbacks, so any test that waits for the renderer must show its window.
- **Single-row ribbon**: no panel titles, no large buttons — hierarchy is carried by grouping and order, and labels must stay short.
- **Ribbon-builtin strings stay English** in non-English sessions; they are outside this repository's translation pipeline.

## Pitfalls and conventions

- **Role names and order are a contract.** `FileTreeModel::Roles`, `FileTreeFlatModel::Roles` and the delegate's `required property` names must stay in sync; the flat model's explicit `case` mapping exists because the enum numbers differ.
- **No pooling signal on `ListView`.** The chevron animation is muted during a model reset via the `Connections` on `onModelReset` plus the `rebinding` flag.
- **Draft debounce.** Never write `tools.json` per keystroke; the 500 ms debounce plus the destructor flush is the contract.
- **`awb::core::OpResult` fully qualified** on the facade invokables.
- **`ui.pickFolder` is the folder picker.** The QML `FolderDialog` is exclusive to Qt 6 QuickDialogs2; the C++ native dialog is the only route that behaves identically on Qt 5 and Qt 6.
- **`objectName`s are test contracts.** The interaction smoke locates `promptEditor`, `promptScroll`, `promptScrollBar`, `editorMenu`, `fileTree` and `treeRowMenu` by `objectName`; renaming them breaks the test. Command bar controls are created by the third-party host or the fallback toolbar and have no reliable `objectName` — test the actions through the window's action layer, the facade methods and `MarkdownEdit`'s pure functions instead.
- **The command bar skins only forward.** `WorkbenchRibbon.qml` and `WorkbenchToolbar.qml` carry the same actions to the same window functions; behaviour grown in either skin is a drift bug.
- **Actions have exactly one home.** A control appears in the command bar, in a side panel or in the context menu — never in two of them.
- **Session writes go through `saveMarkdownSession()`.** The window composes the state and hands it to `WorkbenchContext` in one call (tab group from the controller, preset from the window, MRU from the context) — writing the file from anywhere else risks losing one of the three sources.
- **The multi-class test runner and `-o`.** `awbRunRegisteredTests()` passes `-o <file>` through to each class's `qExec`; the argument used to be eaten as a test filter, which silently skipped every class and reported success. When debugging a multi-class suite, run one case by name and check it actually ran.

## Tests

| Target | Covers |
|---|---|
| `tst_tools` | `ToolsStore` semantics (MRU, cap, draft debounce, ribbon section tolerance), lazy tree model (roles, fetch, incremental refresh), icon mapping, facade wiring, `MarkdownEdit` cursor operations |
| `tst_toolsui` | The window-level interaction smoke: loads the workbench window, drives the draft editor, the file tree and the context menu through the `objectName` contracts |
| `tst_markdown` | `MarkdownChunker` (including corpus equivalence), `MarkdownDocument` (shape-preserving save, caps, read-only sources, watcher semantics, **the virtual document**), `MarkdownRuntime` (URLs, MIME, routes, protocol tables, theme → CSS, outline), `MarkdownPreviewController` (**the session container**: open/switch/close, path dedup, the 8-tab cap, dirty isolation, doc.json-follows-active, asset resolution for live sessions, the draft session, close-keeps-sessions, save-without-switching; queue de-duplication, open/save/export, capability verdicts), and the vendor sha256 table |
| `tst_markdownpreview` | The page pipeline against a real `WebEngineView`: a document mounts, blocks appear, incremental updates land. The large-document case runs only under `AWB_MD_PERF` |
| `tst_markdownpreviewui` | The shipped `MarkdownWorkbenchWindow.qml` as loaded QML: the command pump runs, find/serialize round-trip, the open/close reporting contract (Qt 6 + WebEngine only) |
| `tst_webengine` | The scheme serving resources, fonts, `doc.json` and beacons end to end |
| `tst_workbench` | `previewMarkdown()` / `openMarkdownWorkbench()` delegation and the toast-only failure paths; the single-instance **open-message round trip**; the file-association **registered-state verdict** (default and after registration, on builds where the registry is writable) |
| `tst_core` | The five `markdown.*` settings keys and `Paths::markdownPreviewDir()` |

The gates that keep `tst_markdownpreview` and `tst_markdownpreviewui` out of Qt 5 and no-WebEngine builds are in `tests/CMakeLists.txt`, matching the feature's own condition. The command bar axis (`AWB_ENABLE_RIBBON` on/off) applies to the whole suite: both states must be green.

## Change checklist

- [ ] New `.cpp`/`.h` files are added to the owning module's `CMakeLists.txt`; if a new source uses `tr()`, add it to `cmake/AwbTranslations.cmake`.
- [ ] New or moved `.qml` files are registered in `app/CMakeLists.txt` (the window and command bar live under the `workbench/` resource alias; the ribbon file only in `AWB_ENABLE_RIBBON` builds) and in `AWB_TS_SOURCES` if they use `qsTr()`.
- [ ] `objectName`s used by the interaction smoke are unchanged, or the test is updated in the same commit.
- [ ] `tools.json` key names (`workspaces`, `current`, `draft`, `ribbon` with its four inner keys) are unchanged — they are on-disk format.
- [ ] `markdown_session.json` key names (`tabs`, `activeIndex`, `preset`, `promptLeftOpen`, `documentLeftOpen`, `documentRibbonCollapsed`, `recentDocuments`, and per-tab `path`/`mode`) are unchanged — same rule.
- [ ] The single-instance message format (`open:<JSON array>` / `raise`) stays in sync between `encodeOpenMessage()` and `decodeOpenMessage()`, and the ProgId (`AgentWorkbench.Markdown.1`) is not renamed — renaming it orphans the registry entry the user already registered.
- [ ] `FileIcons` key names in `config/default_file_icons.json` were not renamed.
- [ ] Both `AWB_ENABLE_RIBBON` states configure, build and pass the tests.
- [ ] `bash scripts/build.sh --test` is green, including `check_architecture`.

## Related

- [Skills browser](skill-browser.md) — the other directory-scanning surface, and the first consumer of the shared `A*` components.
- [Shell and navigation](shell-and-navigation.md) — how pages are registered and loaded, and where the start page fits.
- [Workbench and pages](workbench-and-pages.md) — the cross-domain wiring the window sits on.
- [WebEngine adapter](webengine-adapter.md) — the other WebEngine-linking adapter, and the compat bridge pattern this pipeline shares.
- [Settings](settings.md) — the `A*` form controls the window reuses.
- [Markdown workbench (guide)](../guide/markdown-workbench.md) — the user-facing description.
- [Frontend design](../architecture/frontend-design.md) — the `A*` component shelf and the `AMenu` family.
- [Layers and dependencies](../architecture/layers-and-dependencies.md) — why `tools` and `markdown` may not depend on each other, and why the window lives in `workbench`.
