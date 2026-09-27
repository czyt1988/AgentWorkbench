# Configuration

AgentWorkbench is config-driven. Everything the app writes lives in one data
directory, created on first run. On the first start after upgrading the app
**copies** the legacy `~/.AgentLauncher` data (config and logs) into the new
directory — the old directory is never deleted:

| OS | Path |
|---|---|
| Windows | `%USERPROFILE%\.AgentWorkbench\` |
| Linux | `~/.AgentWorkbench/` |
| macOS | `~/.AgentWorkbench/` |

Files in the data directory:

| File | Written by | Content |
|---|---|---|
| `agents.json` | launcher page / settings | user agents + the `removed` list |
| `agent_state.json` | one-time setup | which agents finished their setup |
| `settings.json` | settings page | application settings (see below) |
| `themes/*.json` | you | custom themes (same `id` overrides a built-in) |
| `plugins/*/` | you | plugin manifests + libraries ([Plugins](plugins.md)) |
| `webprofiles/<agentId>/` | Web tabs | per-agent cookies + localStorage |
| `log/agentworkbench.log` | the app | rotating log (5 MB × 3 files) |

The log records what the app does: every command it runs — the real command
line, the exit code, how long it took and the command's own output, failures
included — plus configuration writes, one-time setup state, agent health
transitions and plugin loading. It rotates at 5 MB and keeps three files
(`agentworkbench.log`, `agentworkbench.log.1`, `agentworkbench.log.2`),
deleting the oldest, so it never grows past 15 MB.

Writing happens on a background thread (an async queue drained by a single
worker): the thread that emits a log line only formats it and enqueues it,
so chatty logging never stalls the UI. Warnings and above are flushed as
they arrive, everything else at least once a second — an abnormal exit can
therefore lose at most the last second of below-warning lines.


## settings.json

All application settings in one file, grouped exactly as written below.
Missing keys take their defaults in place — there is no migration code:

```json
{
  "window":  { "title": "", "width": 1440, "height": 900,
               "sidebarWidth": 240, "sidebarCollapsed": false,
               "lastPageId": "agents" },
  "appearance": { "theme": "mocha-dark", "followSystem": false },
  "locale":  { "override": "" },
  "launcher": { "healthCheckIntervalMs": 3000, "startupVersionCheck": true },
  "web":     { "surface": "embedded", "freezeInactiveTabs": false,
               "maxLiveTabs": 8, "downloadDir": "", "chromiumFlags": "",
               "homeUrl": "" },
  "skills":  { "roots": [], "includePluginCaches": true, "maxDepth": 6 },
  "logging": { "maxFileSize": 5242880, "maxFiles": 3,
               "level": "debug", "mirrorToStderr": true },
  "plugins": { "enabled": false, "disabledIds": [] }
}
```

- `window.title` empty = the brand title `AgentWorkbench`. The old root
  `title` field of `agents.json` is retired; a leftover value is reported
  in the log once.
- `appearance.theme` references a theme `id`; unknown ids fall back to
  `mocha-dark` with a warning.
- `web.surface` is `embedded` or `external`. Without a WebEngine build the
  embedded surface degrades to `external` automatically. `chromiumFlags`
  are injected before WebEngine initialization — add e.g. `--disable-gpu`
  there if embedded views fail to start (applies after restart).
- `web.maxLiveTabs` caps simultaneous live views (each costs roughly
  250–350 MB); excess tabs are released to a restorable state, oldest
  inactive first. `web.freezeInactiveTabs` (off by default) additionally
  suspends JS in switched-away tabs to save CPU — resuming a frozen agent
  WebUI visibly repaints it, and a load that was still in flight when the
  tab was switched away stays stalled until you come back, which is why
  the default is off. Chromium already throttles hidden views either way.
- `skills.roots` empty = the platform default roots (`~/.agents/skills`,
  `~/.claude/skills`, `~/.codex/skills`, the ZCode plugin cache, the
  project's `.agents`/`.claude` skills). A non-empty array **completely
  replaces** the defaults; entries are
  `{ "id", "label", "path", "kind", "enabled" }`.
- `logging.level` is the minimum level that reaches the file — `debug`
  (default, logs everything), `info`, `warning`, `critical` or `off`; an
  unknown value falls back to `debug` with a warning. `logging.mirrorToStderr`
  (on by default) additionally echoes every line to the console the app was
  started from. Both apply from the next start.
- `plugins.enabled` is the master switch; `disabledIds` lists per-plugin
  opt-outs. Plugins load at startup, so toggles take effect after a
  restart. See [Plugins](plugins.md).

## Themes

| Location | Purpose |
|---|---|
| `:/themes/*.json` (compiled in) | built-ins: `mocha-dark.json`, `latte-light.json` |
| `<dataRoot>/themes/*.json` | yours; the same `id` overrides the built-in |

Minimal workflow for a new theme: copy a built-in file to
`<dataRoot>/themes/<your-id>.json`, change `id`/`name`/`variant` and the
colors, save — the UI reloads immediately (hot reload) — then pick it in
**Settings → Appearance**. A file whose name does not equal its `id` is
skipped with a warning; unknown tokens are ignored; missing tokens fall
back to the built-in theme of the same variant. The full token list is the
one the built-in theme files use — the QML only ever references
`theme.<token>`.

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
| `color` | No | Accent color: running-state border, tinted background, buttons, status text. Empty = auto-assigned from the current theme palette (see below) |
| `cardColor` | No | Card background color when not running. Empty = the theme surface color |
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
  `~/.AgentWorkbench/agent_state.json` and the command is never re-run
  unless the user picks **Re-initialize** from the card's context menu.
- If `setupCommand` exits with a non-zero code, `launchFailed` is emitted with
  the captured output and the agent does not launch.
- An empty `setupCommand` means no prerequisite — the agent launches directly.

## Running detection

Each `webUrl` is polled via HTTP every 3 seconds. **Any** HTTP response (even a
`401`/`404`) means the server is up → the card is marked **Running**. A
connection refusal or timeout means it is **Stopped**.

## Built-in agents and the bundled default

On load, `AgentRepository::load()` applies the bundled default to every built-in
agent: an entry sharing a built-in id is replaced wholesale by the definition in
`config/default_agents.json` (unless its id is in the `removed` array), and
agents you added yourself are kept as they are, after the built-ins. There is
therefore no compatibility layer for older configs and no user-data migration:
changing a built-in launcher means editing `config/default_agents.json` and
rebuilding. The updated config is written back to disk when anything changed.

That comes with one trade-off: editing a **built-in** agent in the Settings page
(a port, say) only lasts for the current run — the next start overwrites it with
the bundled definition. Make the change in `config/default_agents.json` instead
if it should stick.

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

1. Open `~/.AgentWorkbench/agents.json`.
2. Append a new object to the `agents` array with at least `id`, `name`,
   `command`, `webUrl`, and `color` filled in.
3. Restart AgentWorkbench (or it will pick up changes on next launch).

No recompilation needed.

!!! note "New ids only"
    Entries added or removed by hand here must use an `id` that is not a
    built-in. An entry sharing a built-in id is overwritten by
    `config/default_agents.json` on every start (see
    [Built-in agents and the bundled default](#built-in-agents-and-the-bundled-default));
    to change a built-in, edit that default file and rebuild. To delete a
    built-in, use **Delete** in the Settings page — it records the id in the
    `removed` array.

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
