# AGENTS.md

Guidance for AI coding agents working in this repository.

## What this project is

AgentLauncher is a Qt6/QML + C++ desktop app that launches the web UI of AI
coding agents from a card grid. It is **config-driven**: agent definitions
(commands, web URLs, config directories, colors) live in `agents.json`, not
in C++.

## Build

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

- Qt 6.5+ required (modules: Core, Gui, Qml, Quick, Network, LinguistTools).
- CMake 3.16+, C++17.
- Generator: Ninja (preferred) or MSBuild. On Windows with MSVC, run from a
  developer command prompt or call `vcvars64.bat` first.

## Layout

```
src/           C++ backend: AgentConfig, AgentModel, AgentLauncher, main.cpp
qml/           QML UI: main.qml, AgentCard.qml, AgentEditPage.qml, SettingsPage.qml
config/        default_agents.json (bundled as a Qt resource)
icons/         SVG icons (bundled as Qt resources)
translations/  .ts translation sources (compiled to .qm at build time, embedded as :/i18n/)
docs/          MkDocs site (English + zh/)
tests/         QtTest unit tests (tst_core.cpp)
```

## Config schema (agents.json)

The root object has an optional `title` field (string). When set, it
overrides the application window title; when empty or absent, the default
`AgentLauncher` title is used. All other root-level content is the `agents`
array described below.

An optional root `removed` array lists ids of built-in agents the user
deleted in the Settings page. `AgentConfig::migrate()` skips these ids when
merging defaults, so deleted built-ins stay deleted across restarts. The
"Restore default launchers" button in Settings clears this list and
re-appends the missing defaults.

Agents can also be added, edited, and deleted from the Settings page
(bottom-right gear button); the UI writes the same `agents.json` via
`AgentLauncher::addAgent()` / `updateAgentFull()` / `removeAgent()`.

Each agent object has: `id`, `name`, `command`, `webUrl`, `configDir`, `icon`,
`color`, `cardColor`, `installCommand`, `updateCommand`, `versionCommand`,
`setupCommand`. See `config/default_agents.json`. New agents are added by
editing this file (and the on-disk copy at `~/.AgentLauncher/agents.json`).
Do **not** hard-code agent entries in C++.

The `icon` field accepts: `qrc:/icons/<name>.svg` (built-in), a local file path
(env vars `%VAR%` and `~` expanded), an `http(s)://` URL, or empty (falls back
to `qrc:/icons/default.svg`). Built-in neutral icons: `default`, `terminal`,
`cube`, `bot`. `AgentConfig::resolveIcon()` handles the resolution at parse
time; `AgentConfig::expandEnv()` does the env-var expansion (also used by
`configDir`).

The `cardColor` field is optional — it sets the card's non-running background
color. Empty = default `#313244`.

The `color` field is optional — if empty, a color is auto-assigned from a
built-in Catppuccin Mocha palette by cycling through it based on the agent's
position in the list. The assigned color is persisted on first run.

The `setupCommand` field is optional — it holds a one-time command that runs
before the first launch of an agent (e.g. generating a bearer token for
`qwen serve`). If the command exits with code 0, the result is persisted to
`~/.AgentLauncher/agent_state.json` and the command is never re-run unless
the user picks "Re-initialize" from the card's context menu. An empty
`setupCommand` means no prerequisite — the agent launches directly.

`AgentConfig::userDataDir()` is the single source of truth for the user data
directory (`~/.AgentLauncher/`): `agents.json`, `agent_state.json` and the
logs all live there. `AgentConfig::migrateLegacyUserData()`, called from
`load()` before anything reads the config, copies files left behind by builds
that used `QStandardPaths::AppConfigLocation`; a file already present in the
new location always wins, and the old copies are left in place. Unit tests run
under `QStandardPaths::setTestModeEnabled(true)`, and because test mode does
not redirect `HomeLocation`, `userDataDir()` honours the test-mode location
instead — without that, running the tests rewrites the developer's real
config.

On load, `AgentConfig::load()` merges the bundled default into the on-disk
config: any field that is empty on disk is filled from the default, and any
agent present in the default but missing on disk is appended. This migration
ensures older configs (created before `installCommand`/`updateCommand`/
`versionCommand` existed) get the new fields automatically. The updated config
is persisted back to disk if anything changed.

## Conventions

- All agent state (commands, URLs) comes from `AgentConfig`. The launcher
  reads/writes via the model; the UI never holds its own copy of agent data.
- Running state is detected by HTTP health check to `webUrl` (any HTTP
  response = running; connection refused/timeout = stopped). Do not add
  process-sniffing logic — keep it HTTP-based for cross-tool consistency.
- Python and Node.js versions are detected at startup via
  `AgentLauncher::detectRuntimeVersions()` (runs `python --version` /
  `node --version` through `cmd /c`). Results are exposed to QML via
  `Q_PROPERTY` (`pythonVersion`, `pythonInstalled`, `nodeVersion`,
  `nodeInstalled`) and shown as badges in the top-right corner of the home
  page. If a runtime is not found on PATH, the badge shows a red × with a
  tooltip explaining that agents requiring it may not work.
- Launch uses `QProcess::startDetached` so agents survive the launcher closing.
- If `setupCommand` is set and hasn't been run yet (tracked in
  `agent_state.json`), `launch()` runs it first via `cmd /c` (no visible
  window) and only proceeds to the actual launch command on exit code 0.
  Failures emit `launchFailed(id, message)` with the captured output.
- Environment variables in `configDir` use `%VAR%` (Windows) form; the launcher
  expands them. `~` is also expanded to the home dir.
- When changing QML, keep the dark theme colors (Catppuccin Mocha palette).

## Internationalization (i18n)

This is an international open-source project. All user-visible strings must be
translatable.

- **Source language is English.** Never write Chinese (or any other non-English
  language) inside `tr()` or `qsTr()` — the source string must be English.
  Chinese and other translations belong in `.ts` files under `translations/`.
- **C++**: wrap every user-visible string in `tr()`.
- **QML**: wrap every user-visible string in `qsTr()`.
- Translation files live in `translations/`. The build runs `lupdate` (syncs
  `.ts` from source) and `lrelease` (compiles `.qm`) via
  `qt6_create_translation` in CMakeLists.txt. Compiled `.qm` files are
  embedded as Qt resources under `:/i18n/`.
- `main.cpp` installs a `QTranslator` that auto-loads based on system locale.
- To add a new language: create `translations/agentlauncher_<locale>.ts`,
  add it to the `TS_FILES` list in CMakeLists.txt, then build (lupdate will
  populate it). Fill in translations and rebuild.
- Comments, identifiers, and log messages should also be in English.
- Localized documentation (`docs/zh/`, `README-zh.md`) and language-name
  labels in `mkdocs.yml` are **not** source code — they are legitimate
  localized content and are exempt from this rule.

## Don't

- Don't hard-code agent definitions in C++.
- Don't write non-English source strings inside `tr()`/`qsTr()`.
- Don't run `git commit`/`git push` unless explicitly asked.

## Stopping agents

Cards show a subtle × in the top-right corner while an agent is running.
Clicking it terminates the process tree **this launcher started this
session** — `launch()` records the PID returned by `startDetached`, and
`stop()` runs `taskkill /F /T /PID …` (Windows) so the whole
`cmd → .cmd → node` tree dies. PIDs are in-memory only, so:

- An agent detected as running via the HTTP health check but **not started
  from this launcher** has no PID; `stop()` shows a message instead of
  killing anything. Stop those via their own command.
- After the launcher restarts, PIDs are lost — running agents detected by
  health check can't be stopped from the card until re-launched here.

Force-kill is intentional for a dev-server launcher; agents that need
graceful shutdown should still be stopped via their own command.

## Launching on Windows

`launch()` resolves the bare program via `QStandardPaths::findExecutable`
(which applies PATHEXT), then runs `.cmd`/`.bat` shims through `cmd /c` —
`CreateProcess` alone won't find npm-style shims like `qwen.cmd`. Launch
failures emit `launchFailed(id, message)`; the UI shows an at-place red flash
on the card plus a popup with the reason.
