# Development

## Prerequisites

- **Qt 6.5+** with modules: `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`,
  `Network`.
- **CMake 3.16+**
- **C++17** compiler (MSVC 2019+, GCC 9+, or Clang 10+)
- (Optional) **Ninja** generator for faster builds.

## Build

`scripts/build.sh` configures and compiles in one step. It auto-detects Qt and
the MSVC toolchain, reuses the generator and Qt prefix of an existing build
directory, clears a stale CMake cache left behind by a moved project folder,
and writes `compile_commands.json` for editor tooling:

```bash
bash scripts/build.sh              # Debug build in build/
bash scripts/build.sh --test       # build, then run the unit tests
bash scripts/build.sh --release    # Release build in build-release/
bash scripts/build.sh --help       # all options
```

On Windows with MSVC, driving the compiler from Git Bash needs a `.bat` wrapper
that loads `vcvars64.bat`: importing that environment into Git Bash with
`eval "$(cmd /c ... set)"` does not work, because `cmd` receives the escaped
quotes literally and `cl.exe` never reaches `PATH`. The script generates the
wrapper for you (`build/.build-agentworkbench.bat`).

Building by hand still works, but you have to set up the MSVC environment
yourself first — for example from an "x64 Native Tools Command Prompt":

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

`bash scripts/package.sh` builds the release binary (without the test targets —
a package only needs the application), deploys it with `windeployqt` and
produces `dist/AgentWorkbench-<version>-win64-Portable.zip`.

## Project layout

```
app/          executable: assembly only (main.cpp, QML module, resources)
src/
  core/         L0 infrastructure: Paths, JsonStore, Settings, Logging,
                ProcessRunner, ScriptRunner, HttpProbe, PluginHost, LegacyImport
  plugin_api/   L0 plugin ABI (header-only; external repos link this)
  theme/        L1 theme engine: JSON themes -> semantic tokens -> QML
  agentcatalog/ L2: external-agent catalog (definitions, persistence, processes, health, CRUD) + QML
  shell/        L2 UI framework: navigation, window skeleton, toasts, A* components
  skillcatalog/ L2: local-skill catalog (SKILL.md frontmatter, scanner, model, facade) + QML
  tools/        L2: Agent Tools page (workspace memory, lazy file tree, icon map, prompt draft) + QML
  web/          L2: tabs, surfaces, memory policy + QML
    webengine/  L2 adapter (the only target linking Qt WebEngine)
  workbench/    L3: cross-domain intents, built-in pages, environment, plugin services
cmake/        shared build options (AwbOptions.cmake, AwbTranslations.cmake)
resources/    built-in theme JSON files
config/       default_agents.json (bundled as a Qt resource)
icons/        SVG icons (bundled as a Qt resource)
tests/        one test target per module + check_architecture
scripts/      build.sh, package.sh, check-architecture.sh
docs/         MkDocs site (English + zh/)
```

## Build options

| Option | Default | Meaning |
|---|---|---|
| `AWB_ENABLE_WEBENGINE` | `ON` | embedded Web views (MSVC only; MinGW + ON fails at configure time with a readable error) |
| `BUILD_TESTING` | `ON` | unit test targets (needs the Qt Test module) |

`build.sh` sets `BUILD_TESTING` explicitly for both of its test flags —
`--test` configures it `ON`, `--no-tests` (used by `package.sh`) configures it
`OFF` — so one build directory keeps working after either was used on it.

Pass extra configure arguments after `--`, e.g.:

```bash
bash scripts/build.sh -- -DAWB_ENABLE_WEBENGINE=OFF
```

## Running the tests

```bash
bash scripts/build.sh --test
```

ctest runs one executable per module plus the architecture gate:

| Test | Covers |
|---|---|
| `check_architecture` | no literal colors in QML (hex or numeric `Qt.rgba`), no reverse/sideways module includes, English-only source strings, core/theme stay UI-free, every QML singleton method call is `Q_INVOKABLE` and every property write has a `WRITE` accessor |
| `tst_core` | paths, JSON store, settings, logging, process runner, script runner, HTTP probe, frontmatter of plugins, legacy import |
| `tst_agentcatalog` | repository sync semantics, model roles, facade CRUD, script logging, URLs, runtime launch/stop/force-stop |
| `tst_theme` | loader validation rules, registry override behaviour |
| `tst_shell` | navigation registration, badges, window persistence, clipboard results |
| `tst_web` | tab reuse, close semantics, offline/online transitions, LRU release (no WebEngine needed) |
| `tst_workbench` | cross-domain intents: `openWeb` navigation, external-surface no-tab path, browser-open URL handoff |
| `tst_skillcatalog` | frontmatter parsing, scanning, plugin version dedup, filtering |
| `tst_tools` | workspace store semantics, lazy tree model (roles/fetch/incremental refresh), icon mapping, facade wiring, QML invokable surface |

A single case can be run by name, e.g. `./build/tst_core testRoundTrip`.

## Architecture

The layering and dependency rules, in short:

- **Layers**: `app → workbench → {shell, agents, skills, tools, web, theme} → core`.
  Domain modules never depend on each other; cross-domain behaviour lives in
  `awb_workbench`.
- **Config-driven**: `agents.json` and `settings.json` own the state; the UI
  never hard-codes entries and never writes files itself.
- **QML contract**: pages use semantic tokens only (`theme.surfaceBg`, …) —
  literal colors are rejected by `check_architecture`. C++ globals are
  registered on the `AgentWorkbench.App` URI with uppercase type names and
  exposed to QML through lowercase root aliases (`theme`, `nav`, `agents`, …).
- **Health**: running state comes from an HTTP probe of `webUrl` (any HTTP
  response = running). Do not add process sniffing.
- **Logging**: `core::Logging` writes through spdlog's async backend (an
  8192-slot MPMC queue drained by one worker thread) and rotates at 5 MB × 3
  files; the emitting thread only formats the line and enqueues it. Commands
  are logged with the command line actually executed.

## Documentation site

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

Open `http://127.0.0.1:8000`. The site is bilingual (English default, 中文 under
`/zh/`) using the folder-based i18n plugin.

## Conventions

- Add new agents via `agents.json`, never by hard-coding in C++.
- QML uses theme tokens only (`theme.*`) — never a literal color; the
  `check_architecture` test rejects them.
- Domain modules never include each other (or shell/workbench); cross-domain
  behaviour goes through `awb_workbench`.
- Running state is detected via HTTP health check to `webUrl`; do not add
  process-sniffing logic.
- The stop button terminates only the process tree that this launcher started
  in the current session; agents detected as running but started elsewhere are
  left to their own lifecycle.
- Code style, naming and comment rules: see the [Coding standard](standards/coding-standard.md).
