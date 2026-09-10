# Changelog

All notable changes to **AgentLauncher** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2026-09-10

This release adds in-app launcher management, makes the portable build
self-contained, and puts all user data in one directory.

### Added

- **Settings page with in-app launcher management**: the bottom-right gear button opens a Settings page listing every launcher with its running state. Launchers can be added, edited, and deleted there — built-ins included — with a **Restore default launchers** action, so `agents.json` no longer has to be hand-edited.
- **Launcher editor** (`AgentEditPage.qml`, replacing `ConfigPage.qml`): a full add/edit form covering every field, with required-field markers, per-field tooltips, live icon and color previews, and validation.
- **Deleted built-ins stay deleted**: `agents.json` gained an optional root `removed` array recording the ids of built-in agents deleted in the Settings page, so the default-merge migration does not resurrect them.
- **UI-failure diagnostics**: when the QML interface cannot be loaded, the app logs the import search paths and whether the bundled QML modules are present on disk, and shows a native message box naming the log file instead of exiting silently.

### Changed

- **User data moved to `~/.AgentLauncher/`** (Windows: `%USERPROFILE%\.AgentLauncher\`): `agents.json`, `agent_state.json`, and the log directory now live together, instead of being split between the platform config directory and the home directory.
- **Self-contained portable build**: `scripts/package.sh` writes a `qt.conf` pinning Qt's prefix to the executable's own folder, and the app prefers the QML modules deployed next to the executable over any Qt installation found on the machine. This fixes "module … is not installed" failures on machines that have their own Qt.
- **Packaging script**: the archive version is read from `CMakeLists.txt` (still overridable via `VERSION`), the zip is named `AgentLauncher-<version>-win64-Portable.zip`, and a stale CMake cache left behind by a moved or renamed project folder is detected and cleared automatically.
- **Agent install commands**: Kimi Code now installs `@moonshot-ai/kimi-code` (was `@kimi-code/cli`); the other bundled agents' install commands are pinned to `@latest`.

### Fixed

- **Existing configs migrate automatically**: a config or state file left in the old platform config directory is copied to `~/.AgentLauncher/` on first start, so user-added launchers and deletion records survive the upgrade. A file already present in the new location wins, and the old copies stay in place in case you downgrade.
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

[0.3.0]: https://github.com/czyt1988/AgentLauncher/releases/tag/v0.3.0
[0.2.0]: https://github.com/czyt1988/AgentLauncher/releases/tag/v0.2.0
