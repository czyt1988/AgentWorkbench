# User Guide

AgentWorkbench is a single launcher and browser workspace for your AI coding
agents. Instead of remembering a different start command, port and config
folder for every tool, you get one window: a list of the agents installed on
your computer, a button to start each one, and a place to open their web
interfaces in tabs.

Think of it as a remote control plus a browser: it does not implement any agent
itself, it starts the ones you already have and puts their web pages side by
side.

## What problem it solves

- **Different start commands.** Each agent starts its own local web server with
  its own command and default port. You only click **Start**.
- **Different config folders.** A card can open the agent's own config folder
  for you.
- **Browser tab overload.** Each agent's web interface opens in a tab inside the
  app, so you do not have to hunt through browser windows.
- **Prompts that get sent too early.** The built-in prompt editor gives you a
  scratchpad where pressing Enter only starts a new line.

## Installing and starting

The project is currently distributed as source you build yourself, or as a
portable package. See [Building](../development/index.md) for the build and
packaging steps. This guide starts from the moment the application window is
open.

> The app follows your system language automatically. If your system is set to
> Chinese, the Chinese interface is used.

## A tour of the window

The window has three parts.

**The sidebar (left).** Every page of the app lives here.

- **Agent Launcher** - the cards for the agents on your computer: start, stop,
  install and configure them.
- **Agent Web UI** - the tabs that show the agents' web interfaces inside the
  app.
- **Skills** - a searchable list of the "skill" instruction files found on your
  computer, with their locations.
- **Agent Tools** - a prompt-writing scratchpad with a file tree beside it.
- **Settings** - pinned at the very bottom as an icon. Themes, launchers,
  environment checks, skill folders, browser options, plugins and reset
  actions.

The sidebar can be collapsed to a narrow strip of icons to give the workspace
more room, and its width can be adjusted: move the pointer to the sidebar's
right edge (the cursor turns into a left-right arrow) and drag.

**The workspace (right).** Shows the page you selected in the sidebar. Most
pages have their own header with the page title and any actions that belong to
that page.

**The status bar (bottom).** Shows how many agents are currently
running, and badges for the Python and Node.js runtimes found on your computer.
A red cross on a badge means that runtime was not found.

The last page you were on, your window size, the sidebar state (including the
width you dragged it to) are remembered the next time you open the app.

## Keyboard shortcuts

These work anywhere in the window:

| Shortcut | What it does |
|---|---|
| `Ctrl+1` … `Ctrl+9` | Jump to the 1st, 2nd, … page in sidebar order |
| `Ctrl+B` | Collapse or expand the sidebar |
| `Ctrl+,` | Open **Settings** |

These work while the **Agent Web UI** page is open:

| Shortcut | What it does |
|---|---|
| `Ctrl+W` | Close the current tab |
| `F5` | Reload the current tab |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Switch to the next / previous tab |
| `Ctrl+=` or `Ctrl++` | Zoom in |
| `Ctrl+-` | Zoom out |
| `Ctrl+0` | Reset the zoom level |
| `F12` | Open the developer tools (debug builds only) |
| `Esc` | Leave full screen |

In the **Agent Tools** prompt editor, the usual text shortcuts apply: `Ctrl+Z`
to undo, `Ctrl+Y` to redo, `Ctrl+X` / `Ctrl+C` / `Ctrl+V` to cut, copy and
paste.

## Where your data lives

Everything the app writes lives in one folder:

| System | Folder |
|---|---|
| Windows | `%USERPROFILE%\.AgentWorkbench\` |
| Linux / macOS | `~/.AgentWorkbench/` |

That folder holds your launcher list, your settings, your themes, the saved
login state of the agents' web interfaces, and the log files. When you first run
this version of the app on a computer that still has data from the older
*AgentLauncher*, that old data is **copied** into the new folder and the old
folder is left untouched.

You can open the folder at any time from **Settings → Advanced → Open data
folder**.

## Where to go next

| Page | What you will find |
|---|---|
| [Agent launcher](agent-launcher.md) | Starting and stopping agents, adding your own |
| [Embedded web interface](web-ui.md) | Tabs, full screen, downloads, saved logins |
| [Skills](skills.md) | Finding skill files and choosing where to look |
| [Agent Tools](agent-tools.md) | The prompt scratchpad and its file tree |
| [Appearance](appearance.md) | Themes and fonts |
| [Settings](settings.md) | Every settings section explained |
| [Plugins](plugins.md) | Third-party extensions, in plain language |
| [Troubleshooting](troubleshooting.md) | What to do when something misbehaves |

Curious how it works inside? See [Development](../development/index.md).
