# AgentWorkbench

A Qt6/QML + C++ desktop **workbench for AI coding agents** (formerly *AgentLauncher*): a sidebar + workspace shell that launches the **web UI** of several AI coding agents (Kimi Code, OpenCode, Qwen Code), embeds their interfaces as tabs, browses local Skills and themes everything from JSON files. Everything is **config-driven** — new agents are added by editing a JSON file, no code changes required.

![AgentWorkbench main window](pic/screenshot-main-page.png)

> The app follows the system language automatically.

## Why

Each AI coding agent has its own CLI and its own way of starting a local web server, with different default ports and different config directory layouts. Remembering every command is tedious. AgentWorkbench gives you one place to start any of them and jump straight into its web UI — in a tab inside the app, or in your browser.

## Features

- **Workbench shell**: sidebar navigation with badges and keyboard shortcuts (`Ctrl+1…9`, `Ctrl+B`, `Ctrl+,`), workspace pages and a status bar with Python/Node runtime badges.
- **Card grid launcher** with start/install/update actions, version labels, HTTP-detected running state and per-session stop.
- **Embedded Web tabs** (Qt WebEngine): one persistent profile per agent, frozen inactive tabs, restorable released views, crash/offline/error overlays — with **Open in browser** always available as the fallback.
- **Skill browser** over the usual `SKILL.md` locations with search, facets, hover details and click-to-copy paths.
- **Themes** driven by JSON files: two built-in Catppuccin variants, custom themes hot-reload on save, and a build gate that rejects literal colors in QML.
- **Fully config-driven** via `agents.json` and `settings.json`.
- **Experimental plugins** with a versioned ABI (disabled by default).

## Quick start

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
./build/AgentWorkbench
```

See [Configuration](configuration.md) for adding your own agents, [Plugins](plugins.md) for extending the app, and [Development](development.md) for build details.
