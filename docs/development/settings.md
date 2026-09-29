# Settings

## What this feature does

The Settings page is the single graphical entry point for application
configuration. It reads and writes **only through `core::Settings`**, which is
the typed access layer for `settings.json`. Data that lives in other files has
its own facade: agent definitions are handled by `AgentsFacade` (from
`agents.json`), the Agent Tools workspace memory by `ToolsFacade` (from
`tools.json`), skill roots by `SkillsFacade` (persisting into `settings.json`'s
`skills.roots` through the same `core::Settings` setters), and the workspace
memory of the launcher page by its own store.

Boundaries:

- The settings sections are a UI over a fixed key schema. There is no migration
  code and there must not be any: missing keys fall back to defaults, unknown
  keys are ignored with a warning.
- Some keys have no UI. `logging.*`, `launcher.*`, `locale.override`,
  `appearance.followSystem` and `web.homeUrl` are read by their consumers but
  are currently edited by hand only.
- Plugin toggles and a few other keys only take effect after a restart, because
  they are consumed during startup assembly.

## Files and classes

| File | Class / component | Responsibility | Collaborates with |
|---|---|---|---|
| `src/core/Settings.h` | `Settings` plus `WindowSettings`, `AppearanceSettings`, `LocaleSettings`, `LauncherSettings`, `WebSettings`, `SkillsSettings`, `LoggingSettings`, `PluginsSettings` | The typed schema: one struct per section, all defaults inline; property-style getters and setters; `valueChanged(key)` | every module |
| `src/core/Settings.cpp` | `Settings::load()/save()` | Key-set validation, typed reads with clamping, unknown-key warnings, atomic write | `core::JsonStore`, `core::Logging` |
| `src/shell/qml/SettingsPage.qml` | `SettingsPage` | Left section nav + right `StackLayout`; section list is the single source of order; pinned app version | the seven section pages |
| `src/shell/qml/SettingsAppearancePage.qml` | `SettingsAppearancePage` | Theme and font selection | `theme.applyTheme()`, `theme.setFontFamily()`, `theme.availableThemes`, `theme.fontFamilies` |
| `src/shell/qml/SettingsLaunchersPage.qml` | `SettingsLaunchersPage` | Launcher list with add/edit/delete and confirmation dialogs | `agents.model`, `agents.removeAgent()`, `agents.isDefaultAgent()`, `AgentEditDialog` |
| `src/shell/qml/SettingsEnvironmentPage.qml` | `SettingsEnvironmentPage` | Python/Node status and a Re-detect button | `environment.*` |
| `src/shell/qml/SettingsSkillsPage.qml` | `SettingsSkillsPage` | Skill root list with enable switches, remove, add field and stats | `skills.roots`, `skills.setRootEnabled()`, `skills.addRoot()`, `skills.removeRoot()`, `skills.kindLabel()`, `skills.statsText` |
| `src/shell/qml/SettingsWebPage.qml` | `SettingsWebPage` | Surface selection and Chromium flags field | `web.engineAvailable`, `shell.webSurface`, `shell.setWebSurface()`, `shell.setWebChromiumFlags()` |
| `src/shell/qml/SettingsPluginsPage.qml` | `SettingsPluginsPage` | Plugin master switch, per-plugin switches, trust notice | `workbench.pluginsEnabled()`, `workbench.setPluginsEnabled()`, `workbench.pluginList()`, `workbench.setPluginEnabled()`, `workbench.pluginTrustNotice()` |
| `src/shell/qml/SettingsAdvancedPage.qml` | `SettingsAdvancedPage` | Data-file path, open data folder, restore default launchers | `agents.configFilePath()`, `agents.restoreDefaults()`, `workbench.openFolder()` |
| `src/shell/ShellController.h` / `.cpp` | `ShellController` | Window-level keys as bindable properties; setters persist immediately; `valueChanged` re-broadcast | `core::Settings` |
| `src/workbench/WorkbenchContext.h` / `.cpp` | `WorkbenchContext` | Plugin switches and trust notice; `legacyImportNotice` | `core::Settings`, `PluginHost` |
| `src/theme/Theme.h` / `.cpp` | `Theme` | `applyTheme()` (rejects unknown ids) and `setFontFamily()`; `availableThemes`, `family`, `fontFamilies` | `core::Settings`, `ThemeRegistry` |
| `src/core/Logging.h` / `.cpp` | `Logging` | `install()` and `isValidLevelName()` used to validate `logging.level` | `core::Settings` |
| `app/main.cpp` | `main()` | Reads settings before `QGuiApplication`, applies Chromium flags and logging options, loads the translator and font, drives plugin loading | `core::Settings` |
| `cmake/AwbTranslations.cmake` | build | Lists the settings QML files among the translatable sources | — |

## Frontend design

### `SettingsPage.qml`

A `Page` with a `RowLayout`: a 200 px nav column on the left and a `StackLayout`
on the right.

- **Section list.** A `readonly property var sections` array of
  `{id, title}` is the single source of both the nav order and the
  `StackLayout` order: `Appearance`, `Launchers`, `Environment`, `Skills`, `Web`,
  `Plugins`, `Advanced`. `sectionIndex(id)` maps an id to a `StackLayout` index
  (unknown ids fall back to 0).
- **Kept instantiated.** The `StackLayout` holds all seven section pages and only
  changes `currentIndex`, so half-typed input (e.g. a new skill root) survives a
  round trip to another section.
- **Pinned version.** The bottom of the nav column is a divider plus
  `"v" + Qt.application.version`, mirroring the main sidebar's pinned footer.
  This is why the version is no longer in the status bar.
- **Delegates.** Nav rows are delegates and carry
  `Component.onDestruction: ToolTip.hide()`.

### Section pages

Each section page shares the skeleton of `ScrollView` + `ColumnLayout` +
`PageHeader`; the ones with dialogs own them privately.

- **`SettingsAppearancePage`.** Two `AComboBox`es.
  - Theme: `model: theme.availableThemes`, `onActivated: theme.applyTheme(currentValue)`.
    The current index comes from a loop over `availableThemes` rather than
    `indexOfValue(theme.themeId)`; a tooltip shows the full theme name.
  - Font: a `Theme default` entry with an empty value followed by
    `theme.fontFamilies`; `onActivated: theme.setFontFamily(currentValue)`.
    The index likewise uses a loop over the model. A caption explains that the
    font applies to the whole application and that "Theme default" follows the
    theme or system font.
- **`SettingsLaunchersPage`.** A `PageHeader` with an `Add Launcher` action, a
  `Repeater` over `agents.model` rendering `AListRow`s (icon, name, command, an
  `AStatusDot`, Edit and Delete). Editing opens `AgentEditDialog`; Delete opens a
  danger `AConfirmDialog` with contextual warnings (running / built-in) whose
  `onConfirmed` calls `agents.removeAgent()`. A failed write opens an
  `AAlertDialog` showing `agents.configFilePath()`. This section edits
  `agents.json`, not `settings.json`.
- **`SettingsEnvironmentPage`.** Two labels from `environment.pythonInstalled`/
  `pythonVersion` and `nodeInstalled`/`nodeVersion` (red when missing) plus a
  Re-detect button calling `environment.refresh()`. There are no settings keys
  here; the detection result is process state.
- **`SettingsSkillsPage`.** A `PageHeader` with a `Rescan` action
  (`busy: skills.scanning`), a `Repeater` over the `skills.roots` property with a
  `Switch` (→ `skills.setRootEnabled()`) and a remove `AIconButton`
  (→ `skills.removeRoot()`), an `ATextField` + `Add` button
  (→ `skills.addRoot()`), then `skills.statsText` and a caption explaining the
  plugin version de-duplication. This is the UI for `skills.roots`.
- **`SettingsWebPage`.** A surface `AComboBox` whose model is built from
  `web.engineAvailable` (only `External` when the embedded engine is not
  compiled in), calling `shell.setWebSurface()`; and an `ATextField` whose
  `onEditingFinished` calls `shell.setWebChromiumFlags()` with a caption noting
  it applies after restart and that "Open in browser" always works as a fallback.
- **`SettingsPluginsPage`.** The trust notice from
  `workbench.pluginTrustNotice()`, a master `Switch` bound to
  `workbench.pluginsEnabled()`/`setPluginsEnabled()`, an empty-state label when
  `workbench.pluginList()` is empty, and a `Repeater` of `AListRow`s with a
  per-plugin `Switch` calling `workbench.setPluginEnabled()`.
- **`SettingsAdvancedPage`.** The mono path from `agents.configFilePath()`, an
  `Open data folder` button that strips the trailing `agents.json` and calls
  `workbench.openFolder()`, and a `Restore default launchers` button calling
  `agents.restoreDefaults()` (a failed write opens the error dialog).

## Backend design: the settings.json key table

Every key below is validated by `Settings::load()` and written back by
`Settings::save()`. Clamp ranges are enforced on read: a value outside the range
(or a wrong type) logs a warning and falls back to the default.

| Key | Type | Default | Meaning / range | Consumed by |
|---|---|---|---|---|
| `window.title` | string | `""` | Window title; empty means the app default `AgentWorkbench` | `ShellController::windowTitle()`, `MainWindow.title` |
| `window.width` | int | `1440` | Window width; clamped `[400, 16384]` | `ShellController::windowWidth()`, `MainWindow.width` |
| `window.height` | int | `900` | Window height; clamped `[300, 16384]` | `ShellController::windowHeight()`, `MainWindow.height` |
| `window.sidebarWidth` | int | `240` | Expanded sidebar width; clamped `[0, 1024]`; `0` falls back to `theme.sidebarWidth` | `Sidebar.implicitWidth` via `shell.sidebarWidth` |
| `window.sidebarCollapsed` | bool | `false` | Sidebar collapsed state | `ShellController`, `Sidebar.collapsed`, `Ctrl+B` |
| `window.lastPageId` | string | `"agents"` | Last visited page id, restored at startup | `BuiltinPages::wirePagePersistence()` |
| `appearance.theme` | string | `"mocha-dark"` | Active theme id; an unknown id falls back to `mocha-dark` with a warning on load, but `Theme::applyTheme()` refuses unknown ids | `Theme::loadCurrent()`, `Theme::themeId()` |
| `appearance.followSystem` | bool | `false` | Follow the system light/dark preference | (read and written; no consumer today) |
| `appearance.fontFamily` | string | `"Microsoft YaHei"` | Global UI font family; empty follows the theme/system default | `main.cpp` (`QGuiApplication::setFont`), `Theme::family()`, `MainWindow.font.family` |
| `locale.override` | string | `""` | Forced locale; empty follows the system locale | `main.cpp` (translator load), `PluginServices` context |
| `launcher.healthCheckIntervalMs` | int | `3000` | Agent health-check interval; clamped `[100, 600000]` | `AgentsFacade` → `AgentHealthMonitor` |
| `launcher.startupVersionCheck` | bool | `true` | Run a version check at startup | (read and written; no consumer today) |
| `web.surface` | string | `"embedded"` | `embedded` \| `external`; any other value warns and resets to the default | `WebTabsFacade`, `ShellController::webSurface()`, `SettingsWebPage` |
| `web.freezeInactiveTabs` | bool | `false` | Freeze non-active tabs (Chromium already throttles them; freezing also suspends JS/websockets) | `WebTabsFacade` (`policyChanged`), `WebEngineSurface.qml` |
| `web.maxLiveTabs` | int | `8` | Maximum live tabs before LRU release; clamped `[1, 64]` (and re-clamped to `>= 1` at use) | `WebTabsFacade::applyMemoryPolicy()` |
| `web.downloadDir` | string | `""` | Download directory; empty means the platform default (`~/Downloads`) | `WebTabsFacade` (`policyChanged`), `WebEngineSurface.qml` download |
| `web.chromiumFlags` | string | `""` | Chromium command-line flags, injected as `QTWEBENGINE_CHROMIUM_FLAGS`; applies after restart | `main.cpp`, `ShellController::webChromiumFlags()` |
| `web.homeUrl` | string | `""` | Home URL for new tabs | (read and written; no consumer today) |
| `skills.roots` | array | `[]` | Skill scan roots as `{id?,label?,path,kind?,enabled?}`; empty means the built-in defaults | `SkillScanner`, `SkillsFacade`, `SettingsSkillsPage` |
| `skills.includePluginCaches` | bool | `true` | Whether plugin-cache roots are scanned | `SkillScanner` → `SkillScanParams` |
| `skills.maxDepth` | int | `6` | Directory traversal depth; clamped `[1, 32]` | `SkillScanner` → `SkillScanParams` |
| `logging.maxFileSize` | number (qint64) | `5242880` (5 MiB) | Log rotation size; must be `>= 1024` and within 2^53 | `main.cpp` → `Logging::install()` (restart) |
| `logging.maxFiles` | int | `3` | Number of rotated log files; clamped `[1, 20]` | `main.cpp` → `Logging::install()` (restart) |
| `logging.level` | string | `"debug"` | Minimum level: `debug` \| `info` \| `warning` \| `critical` \| `off`; anything else warns and resets to the default | `main.cpp` → `Logging::install()` (restart) |
| `logging.mirrorToStderr` | bool | `true` | Mirror logs to stderr | `main.cpp` → `Logging::install()` (restart) |
| `plugins.enabled` | bool | `false` | Plugin master switch | `main.cpp` plugin load (next start), `WorkbenchContext` |
| `plugins.disabledIds` | string array | `[]` | Individually disabled plugin ids | `main.cpp` plugin load (next start) |

The schema's root sections are exactly `window`, `appearance`, `locale`,
`launcher`, `web`, `skills`, `logging`, `plugins`; any other root key warns.

## Business logic

### Why only `core::Settings` may touch the file

`core::Settings` is the one place that knows the key names, types, defaults and
clamps, and the one place that can emit `valueChanged`. Reading `settings.json`
directly anywhere else duplicates the schema and misses the change signal, so
that is forbidden. Consumers hold a `core::Settings *` and go through it.
`Settings::save()` is atomic (`core::JsonStore` writes a temp file and replaces
it), and a failed write logs a warning and returns the `OpResult` to the caller.

### Missing, unknown and foreign keys

A missing key takes the struct's inline default. An unknown key inside a known
section, or an unknown root section, logs a warning and is ignored. There is no
migration code and none should be added: adding a key means adding a default.

### `valueChanged(key)` subscribers

`Settings::valueChanged(key)` carries a dotted key. The subscribers today:

| Subscriber | Keys it reacts to | Effect |
|---|---|---|
| `ShellController` | `window.sidebarCollapsed`, `window.sidebarWidth`, `window.width`, `window.height`, `window.title`, `web.surface`, `web.chromiumFlags` | Re-broadcasts the corresponding property so external edits stay in sync |
| `Theme` | `appearance.theme` → `loadCurrent()`; `appearance.fontFamily` → `changed()` | Reloads the theme or just re-binds the font token |
| `WebTabsFacade` | `web.freezeInactiveTabs` and `web.downloadDir` → `policyChanged()`; `web.maxLiveTabs` → `applyMemoryPolicy()` | `policyChanged` re-evaluates QML bindings; lowering the live-tab cap takes effect immediately rather than at the next tab open |

The last row is the one worth remembering: `web.maxLiveTabs` is special-cased so
that reducing it releases tabs at once.

### Keys that need a restart, and why

`plugins.*`, `web.chromiumFlags` and `logging.*` are consumed during startup
assembly, before the settings UI can take effect mid-session:

- `web.chromiumFlags` is read before `QtWebEngineQuick::initialize()` and pushed
  into `QTWEBENGINE_CHROMIUM_FLAGS`; the engine reads it when it initialises.
- `plugins.enabled` / `plugins.disabledIds` decide which shared libraries are
  loaded at startup.
- `logging.*` configures `core::Logging::install()`, and `main.cpp` only
  re-installs when a value differs from the compiled default (the first install
  must run with defaults so warnings emitted while `Settings` is constructed
  still reach disk).

Everything else (theme, font, sidebar state, last page, web surface, skill
roots, per-plugin switches stored in `settings.json`) takes effect at once or on
the next relevant action.

### Read-only and settable keys

Not every key has a setter path from the UI. `window.sidebarWidth` has a getter
in `ShellController` but no setter, so the sidebar width is whatever the file
says; there is no drag handle writing it back. `appearance.followSystem`,
`web.homeUrl` and `launcher.startupVersionCheck` likewise have no UI control.

## Pitfalls and conventions

- **An unknown theme id must be refused, not fall back.** `Theme::applyTheme()`
  validates against the registry and logs a warning instead of persisting an
  unknown id; the load-time fallback in `Theme::loadCurrent()` exists only for a
  hand-edited `settings.json`, and triggering it on every start would hide the
  mistake.
- **An empty `appearance.fontFamily` follows the theme/system.** Do not coerce it
  to a concrete family; `main.cpp` and `MainWindow.qml` both treat empty as
  "inherit".
- **Clamps live in `Settings::load()`.** A value outside its range is not clamped
  into range; it is discarded and the default is used (with a warning). If a key
  needs a different bound, change the constant in `load()`, not at the use site.
- **Consumer-side clamps still exist.** `WebTabsFacade` re-applies
  `qMax(1, maxLiveTabs)` at use time, so a hand-edited value cannot produce a
  zero-live-tab policy.
- **Some keys are deliberately inert today.** `appearance.followSystem`,
  `web.homeUrl` and `launcher.startupVersionCheck` are read and written but have
  no consumer; documenting them as "follows the system" would be wrong.

## Adding a settings key

1. **`src/core/Settings.h`** — add the member with its default to the right
   struct, a getter if needed, and a setter that emits `valueChanged("section.key")`.
2. **`src/core/Settings.cpp`** — add the key to the section's known-key set, read
   it in `load()` with the correct type helper and clamp (and a validity check if
   it is an enumerated string), and write it in `save()`.
3. **The UI** — add the control to the relevant `Settings*Page.qml` (or decide
   deliberately that the key has no UI yet).
4. **`docs/configuration.md` and `docs/zh/configuration.md`** — add the field to
   the configuration reference.
5. **`docs/guide/settings.md`** (English and Chinese) — describe the control from
   the user's perspective.
6. **This document** — add the row to the key table, and note whether it needs a
   restart.
7. If the string is user-facing and lives in C++, the source string is English and
   `translations/` is updated with `scripts/update-ts.sh`.
8. `bash scripts/build.sh --test` is green, including `check_architecture`.

## Related

- [Configuration reference](../configuration.md) — the user-facing field tables
  for `settings.json` and `agents.json`.
- [Settings (guide)](../guide/settings.md) — the end-user description of every
  section.
- [Shell and navigation](shell-and-navigation.md) — the `ShellController` behind
  the window keys and the `system`-section registration of the page.
- [Skills browser](skill-browser.md) — the consumer of `skills.*`.
- [Agent Tools](agent-tools.md) — a consumer of the shared `A*` form controls.
- [Layers and dependencies](../architecture/layers-and-dependencies.md) — why
  `core::Settings` is the sole access layer.
