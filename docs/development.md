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
wrapper for you (`build/.build-agentlauncher.bat`).

Building by hand still works, but you have to set up the MSVC environment
yourself first — for example from an "x64 Native Tools Command Prompt":

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

`bash scripts/package.sh` builds the release binary, deploys it with
`windeployqt` and produces `dist/AgentLauncher-<version>-win64-Portable.zip`.

## Project layout

```
src/         C++ backend
  main.cpp            registers model + launcher, loads QML
  AgentConfig         loads/saves agents.json (built-ins come from the bundled default)
  AgentModel          QAbstractListModel exposed to QML
  AgentLauncher       launch (QProcess), health-check (HTTP), open browser/dir
qml/         QML UI
  main.qml            home card grid + StackView
  AgentCard.qml       single card (start/configure + running highlight)
  AgentEditPage.qml   per-agent settings (command, web URL, config dir)
  SettingsPage.qml    launcher management (add/edit/delete launchers)
config/      default_agents.json (bundled as Qt resource)
icons/       SVG icons (bundled as Qt resources)
docs/        MkDocs site (English + zh/)
```

## Architecture

- **Config-driven**: `AgentConfig` owns the list of agents; the UI never
  hard-codes agent entries.
- **Model**: `AgentModel` (a `QAbstractListModel`) exposes agent fields as QML
  roles (`agentId`, `name`, `command`, `webUrl`, `configDir`, `icon`, `color`,
  `running`).
- **Launcher**: `AgentLauncher` handles launching (`QProcess::startDetached`),
  periodic HTTP health checks (`QNetworkAccessManager`), and opening the web UI
  / config directory (`QDesktopServices`).
- **Running state** is pushed back into the model via `dataChanged`, which the
  card reacts to with a color animation.

## Documentation site

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

Open `http://127.0.0.1:8000`. The site is bilingual (English default, 中文 under
`/zh/`) using the folder-based i18n plugin.

## Conventions

- Add new agents via `agents.json`, never by hard-coding in C++.
- Keep the dark theme palette (Catppuccin Mocha) when editing QML.
- Running state is detected via HTTP health check to `webUrl`; do not add
  process-sniffing logic.
- The stop button terminates only the process tree that this launcher started
  in the current session; agents detected as running but started elsewhere are
  left to their own lifecycle.
