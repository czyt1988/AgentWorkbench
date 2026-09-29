# Development

This section is for people **changing the code**. Each user-visible feature has its own page describing its front end, its back end and its business logic, and naming the files and classes involved. Read the [architecture section](../architecture/index.md) first if you need the mental model; this page covers how to build, how to test, and where each feature's page is.

## Feature documentation index

| Feature | Development page | Related user guide |
|---|---|---|
| Agent launcher (card grid, process lifecycle, health) | [Agent launcher](agent-launcher.md) | [Agent launcher](../guide/agent-launcher.md) |
| Embedded web tabs (tab model, memory policy) | [Web tabs](web-tabs.md) | [Web interface](../guide/web-ui.md) |
| WebEngine adapter (engine quirks, Qt 5/6 compatibility) | [WebEngine adapter](webengine-adapter.md) | [Web interface](../guide/web-ui.md) |
| Skill browser | [Skill browser](skill-browser.md) | [Skills](../guide/skills.md) |
| Agent Tools page (prompt workbench, file tree) | [Agent tools](agent-tools.md) | [Agent tools](../guide/agent-tools.md) |
| Shell, navigation, components | [Shell and navigation](shell-and-navigation.md) | [Getting started](../guide/index.md) |
| Settings pages and the settings file | [Settings](settings.md) | [Settings](../guide/settings.md) |
| Theme engine | [Theme engine](theme-engine.md) | [Appearance](../guide/appearance.md) |
| Cross-domain wiring and page registration | [Workbench and pages](workbench-and-pages.md) | — |
| Plugin host | [Plugin host](plugin-host.md) | [Plugins](../guide/plugins.md) |
| Infrastructure layer (paths, JSON, processes, logging…) | [Core infrastructure](core-infrastructure.md) | — |
| Translations | [Internationalisation](i18n.md) | — |

## Prerequisites

- **Qt 6.5+** with the modules `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Network`, `Concurrent`, `LinguistTools` — or Qt 5.15.16 LTS as the fallback toolchain (see [Qt 5 support](#qt-5-support)).
- **CMake 3.16+**
- **C++17** compiler (MSVC 2019+, GCC 9+, or Clang 10+)
- (Optional) **Ninja** generator for faster builds.
- `third_party/spdlog` as a submodule — initialized automatically when you create a worktree with `scripts/worktree-add.sh`.

## Build

`scripts/build.sh` configures and compiles in one step. It auto-detects Qt and the MSVC toolchain, reuses the generator and Qt prefix of an existing build directory, clears a stale CMake cache left behind by a moved project folder, and writes `compile_commands.json` for editor tooling:

```bash
bash scripts/build.sh              # Debug build in build/
bash scripts/build.sh --test       # build, then run the unit tests
bash scripts/build.sh --release    # Release build in build-release/
bash scripts/build.sh --help       # all options
```

Other options worth knowing: `--target NAME` builds a single target, `--no-tests` configures with `-DBUILD_TESTING=OFF` (what packaging uses), `--run` starts the application after building, `--clean` deletes and reconfigures the build directory, and `--print-exe` prints the executable path.

Qt is resolved in this order: `QT_PREFIX` (or `--qt`), the prefix recorded in the build directory's CMake cache, the usual install locations (including the Qt online installer's nested layout), a `qmake`/`qtpaths` on `PATH`, and finally a time-capped scan of each drive (`AWB_QT_DEEP_SEARCH=0` skips it, `QT_DEEP_TIMEOUT` sets the per-drive budget in seconds). An installation older than the required version is rejected rather than used silently. When nothing usable is found, the script lists every Qt it saw and why it cannot be used, and on a terminal asks for the prefix — an unattended run (agent, CI) fails with that report instead of blocking.

On Windows with MSVC, driving the compiler from Git Bash needs a `.bat` wrapper that loads `vcvars64.bat`: importing that environment into Git Bash with `eval "$(cmd /c ... set)"` does not work, because `cmd` receives the escaped quotes literally and `cl.exe` never reaches `PATH`. The script generates the wrapper for you (`build/.build-agentworkbench.bat`) — read it when you need to see the command that actually ran.

Building by hand still works, but you have to set up the MSVC environment yourself first — for example from an "x64 Native Tools Command Prompt":

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

## Project layout

```
app/          executable: assembly (main.cpp, QML module, resources)
src/
  core/         L0 infrastructure: Paths, JsonStore, Settings, Logging,
                ProcessRunner, ScriptRunner, HttpProbe, PluginHost,
                LegacyImport, IconResolver, EnvExpander, TextUtils, OpResult
  plugin_api/   L0 plugin ABI (header-only; external repos link this)
  theme/        L1 theme engine: JSON themes -> semantic tokens -> QML
  agentcatalog/ L2: external-agent catalog (definitions, persistence,
                processes, health, CRUD) + QML
  shell/        L2 UI framework: navigation, window skeleton, toasts, A* components
  skillcatalog/ L2: local-skill catalog (SKILL.md frontmatter, scanner, model, facade) + QML
  tools/        L2: Agent Tools page (workspace memory, lazy file tree, icon map, prompt draft) + QML
  web/          L2: tabs, surfaces, memory policy + QML
    webengine/  L2 adapter (the only target linking Qt WebEngine)
  workbench/    L3: cross-domain intents, built-in pages, environment, plugin services
cmake/        shared build options (AwbOptions.cmake, AwbTranslations.cmake)
resources/    built-in theme JSON files
config/       default_agents.json, default_file_icons.json (bundled as Qt resources)
icons/        SVG icons (bundled as a Qt resource)
translations/ agentworkbench_zh_CN.ts -> :/i18n/agentworkbench_zh_CN.qm
docs/         this documentation site (English + zh/)
tests/        one test target per module + check_architecture
scripts/      build.sh, package.sh, check-architecture.sh, update-ts.sh, worktree-add.sh
```

Each module owns its QML under `src/<module>/qml/`; the shared `A*` components live in `src/shell/qml/components/`. The executable's `app/CMakeLists.txt` holds the single resource manifest that decides every QML file's URL — see [Shell and navigation](shell-and-navigation.md) for the registration path and [Frontend design](../architecture/frontend-design.md) for the rules around adding a file.

## Build options

| Option | Default | Meaning |
|---|---|---|
| `AWB_ENABLE_WEBENGINE` | `ON` | embedded web views (MSVC only; MinGW + ON fails at configure time with a readable error) |
| `BUILD_TESTING` | `ON` | unit test targets (needs the Qt Test module) |

`build.sh` sets `BUILD_TESTING` explicitly for both of its test flags — `--test` configures it `ON`, `--no-tests` (used by `package.sh`) configures it `OFF` — so one build directory keeps working after either was used on it.

Pass extra configure arguments after `--`, e.g. `bash scripts/build.sh -- -DAWB_ENABLE_WEBENGINE=OFF`.

## Running the tests

```bash
bash scripts/build.sh --test
```

ctest runs one executable per module plus the architecture gate:

| Test | Covers |
|---|---|
| `check_architecture` | no literal colors in QML (hex or numeric `Qt.rgba`), no reverse/sideways module includes, English-only source strings, core/theme stay UI-free, every QML singleton method call is `Q_INVOKABLE` and every property write has a `WRITE` accessor |
| `tst_core` | paths, JSON store, settings, logging, process runner, script runner, HTTP probe, plugin manifest parsing, legacy import |
| `tst_agentcatalog` | repository sync semantics, model roles, facade CRUD, script logging, URLs, runtime launch/stop/force-stop |
| `tst_theme` | loader validation rules, registry override behaviour |
| `tst_shell` | navigation registration, badges, window persistence, clipboard results, QML-callable method surface |
| `tst_web` | tab reuse, close semantics, offline/online transitions, LRU release (no WebEngine needed) |
| `tst_webengine` | QML surface load smoke test (only built when `AWB_ENABLE_WEBENGINE=ON`) |
| `tst_workbench` | cross-domain intents: `openWeb` navigation, external-surface no-tab path, browser-open URL handoff |
| `tst_skillcatalog` | frontmatter parsing, scanning, plugin version dedup, filtering |
| `tst_tools` | workspace store semantics, lazy tree model (roles/fetch/incremental refresh), icon mapping, facade wiring, QML invokable surface |

A single case can be run by name, e.g. `./build/tst_core testRoundTrip`.

Two conventions that have bitten this repository:

- **Test cases must be declared under `private Q_SLOTS:`** (the uppercase macro). A case written after a trailing lowercase `private:` still compiles and the suite still reports 100% pass — it simply never runs, with no warning. After adding a case, confirm it is registered with `./build/tst_core -functions`.
- **Tests must not touch the developer's real data directory.** They run with `QStandardPaths::setTestModeEnabled(true)`, and tests that need a fixed directory inject a `QTemporaryDir` through `Paths::setDataRootForTesting()`. No test may depend on the network, on installed agent tools, or on a real data directory.

## Qt 5 support

The project builds against **Qt 6 (6.5+, main line)** and **Qt 5.15.16 LTS (fallback)**. The verified floor on the Qt 5 side is 5.15.16 specifically: the WebEngine backports the embedded surface relies on live in the LTS patch releases.

Version differences are deliberately concentrated in two places:

- **Build-time differences** (renamed components, `qt_add_qml_module`, qrc aliases, per-major compiler flags) live in the wrapper functions of `cmake/AwbQtCompat.cmake`.
- **Compile-time differences** live next to the code that needs them, behind `QT_VERSION_MAJOR` / `#if QT_VERSION`.

Do not route around this with `setContextProperty` or version checks inside QML. Two traps worth knowing before writing cross-version code: the Qt 5 route fails by **blanking a whole page or silently doing nothing**, while the Qt 6 build and every C++ test stay green; and "compatible" does not mean "downgraded" — where Qt 6 offers something better, Qt 6 uses it and Qt 5 gets its own explicit fallback branch. The full policy is in [C++ library design](../architecture/cpp-design.md).

## Working on this repository

- **Never develop on `dev` directly.** Create a worktree with `bash scripts/worktree-add.sh <branch> [base]` (for example `bash scripts/worktree-add.sh feat/web-xyz`), develop on the branch, get the tests green, merge back into `dev`, then delete the branch and the worktree. Worktrees live under `.worktree/`. "There is only one session running" is not a reason to skip this — parallel worktrees are the normal mode here.
- **One logical change per commit**, with a Conventional Commits message. Stage only the files your change is about; the working tree often carries other in-flight edits.
- **Code style, naming and comment rules** are in the [coding standard](../standards/coding-standard.md) (Chinese: `docs/zh/standards/coding-standard.md`). New code and any function or class you touch must follow it.
- **Interface design rules** are in `designs.md` at the repository root — read it before any UI change.
- **Documentation is part of the definition of done.** After a change, walk the "what must be updated" table in `docs/AGENTS.md`: feature behaviour, settings keys, UI strings, components, build options, plugin ABI and module dependencies all have documentation consequences. Add your feature to the index above when you add its page.

## Documentation site

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

Open `http://127.0.0.1:8000`. The site is bilingual (English default, 中文 under `/zh/`) using the folder-based i18n plugin, and renders Mermaid diagrams through `pymdownx.superfences` custom fences. Page structure, translation rules and writing conventions are in [docs/AGENTS.md](../AGENTS.md).

## Related

- [Architecture overview](../architecture/index.md) — the layers, the runtime assembly, and one interaction end to end.
- [Layers and dependencies](../architecture/layers-and-dependencies.md) — the rules the build enforces.
- [State and persistence](../architecture/state-and-persistence.md) — what lives on disk and what only lives in memory.
