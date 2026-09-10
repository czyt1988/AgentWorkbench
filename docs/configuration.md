# Configuration

AgentLauncher is config-driven. All agent definitions live in a single
`agents.json` file, copied into `~/.AgentLauncher/` on first run:

| OS | Path |
|---|---|
| Windows | `%USERPROFILE%\.AgentLauncher\agents.json` |
| Linux | `~/.AgentLauncher/agents.json` |
| macOS | `~/.AgentLauncher/agents.json` |

All user data lives in that one directory: `agents.json`, `agent_state.json`
(the one-time setup state) and `log/agentlauncher.log`. Builds before 0.3.0
stored the config under `%LOCALAPPDATA%\AgentLauncher\` / `~/.config/AgentLauncher/`;
on its first start a newer build copies those files into `~/.AgentLauncher/`
(leaving the old copies in place) unless a config is already there.

A bundled default is shipped inside the app (from `config/default_agents.json`).

## Agent entry

Each agent is a JSON object inside the `agents` array:

```json
{
    "id": "kimi-code",
    "name": "Kimi Code",
    "command": "kimi web",
    "webUrl": "http://127.0.0.1:58627",
    "configDir": "%USERPROFILE%/.kimi-code",
    "icon": "qrc:/icons/kimi-code.svg",
    "color": "#FF6B35",
    "cardColor": "",
    "installCommand": "npm install -g @kimi-code/cli",
    "updateCommand": "npm update -g @kimi-code/cli",
    "versionCommand": "kimi --version",
    "setupCommand": ""
}
```

### Field reference

| Field | Required | Purpose |
|---|---|---|
| `id` | Yes | Stable identifier used by the UI to locate an agent |
| `name` | Yes | Card title |
| `command` | Yes | Shell command run (detached) by the **Start** button |
| `webUrl` | Yes | URL health-checked every 3 s and opened in the browser when the card is clicked |
| `configDir` | No | Directory opened by the **Open** button on the Configure page; supports `%VAR%` expansion |
| `icon` | No | Icon path — see [Icon configuration](#icon-configuration) below |
| `color` | No | Accent color: running-state border, tinted background, buttons, status text. Empty = auto-assigned from the built-in palette (see below) |
| `cardColor` | No | Card background color when not running. Empty = default `#313244` |
| `installCommand` | No | Command run by the **Install** menu action |
| `updateCommand` | No | Command run by the **Update** menu action |
| `versionCommand` | No | Command run on startup to detect if the agent is installed and parse its version |
| `setupCommand` | No | One-time command run before the first launch (see [Setup command](#setup-command)) |

## Icon configuration

The `icon` field accepts three kinds of values:

### 1. Built-in resource path

Use `qrc:/icons/<name>.svg` to reference an icon bundled with the app:

```json
"icon": "qrc:/icons/kimi-code.svg"
```

**Built-in icons:**

| Path | Description |
|---|---|
| `qrc:/icons/default.svg` | Neutral terminal-prompt icon (also used when `icon` is empty) |
| `qrc:/icons/terminal.svg` | Terminal window icon |
| `qrc:/icons/cube.svg` | 3D cube icon |
| `qrc:/icons/bot.svg` | Robot face icon |
| `qrc:/icons/kimi-code.svg` | Kimi Code branded icon |
| `qrc:/icons/opencode.svg` | OpenCode branded icon |
| `qrc:/icons/qwen-code.svg` | Qwen Code branded icon |
| `qrc:/icons/openclaw.svg` | OpenClaw branded icon |
| `qrc:/icons/deepseek-harness.svg` | DeepSeek Harness branded icon |

### 2. Local file path

Point to any SVG or PNG file on disk. Environment variables (`%VAR%`) and `~`
are expanded automatically:

```json
"icon": "C:/Users/me/icons/my-agent.svg"
"icon": "%USERPROFILE%/icons/my-agent.png"
"icon": "~/Pictures/agent-logo.svg"
```

If the file does not exist, the card falls back to the default icon.

### 3. Empty / omitted

Leave `icon` empty or omit the field entirely to use the built-in default:

```json
"icon": ""
```

This is equivalent to `"icon": "qrc:/icons/default.svg"`.

### Remote URL

HTTP/HTTPS URLs are also accepted:

```json
"icon": "https://example.com/icon.svg"
```

## Color configuration

Two color fields control the card's appearance:

### `color` (accent)

The agent's primary accent color. Used for:

- Border and tinted background while running
- Start/Open button background
- Status indicator dot
- Status text when active

```json
"color": "#FF6B35"
```

**Optional.** If `color` is empty or omitted, a color is automatically assigned
from a built-in palette by cycling through it based on the agent's position in
the list. The palette uses the Catppuccin Mocha color scheme:

| Index | Color | Name |
|---|---|---|
| 0 | `#f38ba8` | Red |
| 1 | `#fab387` | Peach |
| 2 | `#f9e2af` | Yellow |
| 3 | `#a6e3a1` | Green |
| 4 | `#94e2d5` | Teal |
| 5 | `#89b4fa` | Blue |
| 6 | `#cba6f7` | Mauve |
| 7 | `#f5c2e7` | Pink |

The first agent without a color gets Red, the second gets Peach, and so on.
After Pink the cycle repeats. The assigned color is persisted to `agents.json`
on the first run, so it stays stable across restarts.

### `cardColor` (card background)

Optional. Controls the card's background color when the agent is **not** running.
When empty or omitted, the default `#313244` (Catppuccin Mocha Surface1) is used.

```json
"cardColor": "#1a1a2e"
```

While running, the background switches to a tinted version of `color` (16% alpha)
regardless of `cardColor` — this provides a clear visual signal.

### Example: custom-themed card

```json
{
    "id": "my-agent",
    "name": "My Agent",
    "command": "my-agent serve --port 3000",
    "webUrl": "http://127.0.0.1:3000",
    "icon": "qrc:/icons/cube.svg",
    "color": "#00d4aa",
    "cardColor": "#0d2818"
}
```

## Environment variable expansion

The `configDir` and `icon` fields support environment variable expansion:

- `%VAR%` — Windows-style (e.g. `%USERPROFILE%`, `%LOCALAPPDATA%`)
- `~/` — expanded to the home directory

```json
"configDir": "%USERPROFILE%/.my-agent",
"icon": "%USERPROFILE%/icons/my-agent.svg"
```

## Setup command

The `setupCommand` field is optional. It holds a one-time command that runs
before the first launch of an agent (e.g. generating a bearer token for
`qwen serve`).

- If `setupCommand` exits with code 0, the result is persisted to
  `~/.AgentLauncher/agent_state.json` and the command is never re-run
  unless the user picks **Re-initialize** from the card's context menu.
- If `setupCommand` exits with a non-zero code, `launchFailed` is emitted with
  the captured output and the agent does not launch.
- An empty `setupCommand` means no prerequisite — the agent launches directly.

## Running detection

Each `webUrl` is polled via HTTP every 3 seconds. **Any** HTTP response (even a
`401`/`404`) means the server is up → the card is marked **Running**. A
connection refusal or timeout means it is **Stopped**.

## Migration

On load, `AgentConfig::load()` merges the bundled default into the on-disk
config: any field that is empty on disk is filled from the default, and any
agent present in the default but missing on disk is appended. This ensures older
configs automatically get new fields populated. The updated config is persisted
back to disk if anything changed.

## Default agents

| Agent | Launch command | Web URL | Config directory |
|---|---|---|---|
| Kimi Code | `kimi web` | `http://127.0.0.1:58627` | `%USERPROFILE%/.kimi-code` |
| OpenCode | `opencode web --port 4096` | `http://127.0.0.1:4096` | `%USERPROFILE%/.config/opencode` |
| Qwen Code | `qwen serve ...` | `http://127.0.0.1:4170` | `%USERPROFILE%/.qwen` |
| OpenClaw | `openclaw gateway --port 18789` | `http://127.0.0.1:18789` | `%USERPROFILE%/.openclaw` |
| DeepSeek Harness | `dsh web` | `http://127.0.0.1:3080` | `%USERPROFILE%/.dsh` |

!!! note "OpenCode uses a random port by default"
    OpenCode picks a random free port each run, which makes health-checking and
    "open in browser" unreliable. The default config pins it to `4096` (both the
    `--port` flag and the Web URL). Change it on the Configure page if `4096` is
    taken.

## Adding a new agent

1. Open `~/.AgentLauncher/agents.json`.
2. Append a new object to the `agents` array with at least `id`, `name`,
   `command`, `webUrl`, and `color` filled in.
3. Restart AgentLauncher (or it will pick up changes on next launch).

No recompilation needed.

### Full example

```json
{
    "id": "my-agent",
    "name": "My Agent",
    "command": "my-agent serve --port 3000",
    "webUrl": "http://127.0.0.1:3000",
    "configDir": "%USERPROFILE%/.my-agent",
    "icon": "C:/Users/me/icons/my-agent.svg",
    "color": "#00d4aa",
    "cardColor": "#0d2818",
    "installCommand": "npm install -g my-agent",
    "updateCommand": "npm update -g my-agent",
    "versionCommand": "my-agent --version",
    "setupCommand": ""
}
```
