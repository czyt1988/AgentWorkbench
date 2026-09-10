# AgentLauncher

A small Qt6/QML + C++ desktop app that lets you launch the **web UI** of
several AI coding agents (Kimi Code, OpenCode, Qwen Code, DeepSeek Harness)
from a single card grid, and quickly open their config directories. Everything
is **config-driven** — new agents are added by editing a JSON file, no code
changes required.

![Platform: Windows · Linux · macOS](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue)
![License: MIT](https://img.shields.io/badge/license-MIT-green)
![Qt6](https://img.shields.io/badge/Qt-6.5%2B-41cd52)

![AgentLauncher main window](docs/pic/screenshot-main-page.png)

> The screenshot above shows the UI running in Chinese locale; the app follows
> the system language automatically.

## Features

- **Card grid** of configured agents on the home screen.
- Each card has a **Start** button (launches the agent's web server) and a
  **Configure** button (opens the agent's settings page).
- **Running state** is detected by an HTTP health check; running cards are
  highlighted with the agent's own color and a colored border.
- Click a **running** card (or its **Open** button) to launch the agent's web
  UI in your default browser.
- **Stop** button (×) on running cards for agents that were started from this
  session.
- **Runtime version badges** in the top-right corner show detected Python and
  Node.js versions; missing runtimes are highlighted so you know which agents
  may not work.
- **Version labels** on each card show the installed agent version when the
  `versionCommand` is configured.
- **Configure page**: edit the startup command (e.g. change the `--port`),
  edit the web URL, and open the agent's config directory in the file manager.
- **Config-driven**: all agents, commands, URLs and config directories live in
  `agents.json`. Add a new agent by adding one object to the file.

## Supported agents (defaults)

| Agent | Launch command | Web URL | Config directory |
|---|---|---|---|
| Kimi Code | `kimi web` | `http://127.0.0.1:58627` | `%USERPROFILE%/.kimi-code` |
| OpenCode | `opencode web --port 4096` | `http://127.0.0.1:4096` | `%USERPROFILE%/.config/opencode` |
| Qwen Code | `qwen serve` | `http://127.0.0.1:4170` | `%USERPROFILE%/.qwen` |
| OpenClaw | `openclaw gateway --port 18789` | `http://127.0.0.1:18789` | `%USERPROFILE%/.openclaw` |
| DeepSeek Harness | `dsh web` | `http://127.0.0.1:3080` | `%USERPROFILE%/.dsh` |

> OpenCode uses a random port by default, so AgentLauncher pins it to `4096`
> (both the `--port` flag and the Web URL) so that health-checking and
> "open in browser" work reliably. Change it in the Configure page if you like.

## Build

Requirements: **Qt 6.5+** (with `Core`, `Gui`, `Qml`, `Quick`, `Network`),
**CMake 3.16+**, and a C++17 compiler (MSVC / GCC / Clang).

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

Then run `build/AgentLauncher` (or `build/AgentLauncher.exe` on Windows).

## Configuration

On first run AgentLauncher copies a bundled default config to
`~/.AgentLauncher/`:

- Windows: `%USERPROFILE%\.AgentLauncher\agents.json`
- Linux: `~/.AgentLauncher/agents.json`
- macOS: `~/.AgentLauncher/agents.json`

That directory also holds `agent_state.json` and `log/agentlauncher.log`.
Upgrading from a build before 0.3.0 migrates the old config automatically.

Each agent entry looks like:

```json
{
    "id": "kimi-code",
    "name": "Kimi Code",
    "command": "kimi web",
    "webUrl": "http://127.0.0.1:58627",
    "configDir": "%USERPROFILE%/.kimi-code",
    "icon": "qrc:/icons/kimi-code.svg",
    "color": "#FF6B35"
}
```

| Field | Purpose |
|---|---|
| `id` | Stable identifier |
| `name` | Card title |
| `command` | Shell command run (detached) by **Start** |
| `webUrl` | URL health-checked and opened in the browser |
| `configDir` | Directory opened by the **Open** button on the Configure page (supports `%VAR%`) |
| `icon` | Icon resource path |
| `color` | Highlight color used when the agent is running |

See the [configuration guide](https://agentlauncher.dev/configuration/) for details.

## Documentation

Full docs (English + 中文) are built with [MkDocs](https://www.mkdocs.org/) and
the [Material](https://squidfunk.github.io/mkdocs-material/) theme:

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

## Contributing

Pull requests welcome. Keep agent definitions in `agents.json` rather than
hard-coding them in C++. See [AGENTS.md](AGENTS.md) for build commands and
project conventions.

## License

[MIT](LICENSE) © AgentLauncher Contributors
