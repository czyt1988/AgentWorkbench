# Agent launcher

The **Agent Launcher** page is where you manage the AI coding agents installed on
your computer: start them, see whether they are running, and open their web
interfaces. Each agent is a card.

## What it is

The launcher page is a grid of cards, one card per agent. It knows how to start
each agent's local web server and how to check whether that server is up, but it
does not install or run the agent's logic itself - it starts the tool you already
have.

## When to use it

- To start an agent and get to its web interface.
- To see at a glance which agents are currently running.
- To install, update or re-configure an agent.
- To add a new agent that is not in the built-in list.

## Reading a card

Each card shows:

- **The agent's icon.** If no custom icon is set, a neutral placeholder is used.
- **The name** in the middle of the card.
- **A status word** under the name: **Running**, **Stopped**, **Starting...**,
  **Stopping...**, **Setting up...** or **Installing...**. A short error message
  can appear here instead, and the card border turns red.
- **A colored dot** beside the icon. It is filled while the agent is running.
- **A version label** in the top-left corner once the app has detected the
  installed version (for example `1.2.3`, or **Installed** if the version could
  not be read). If the agent does not seem to be installed, a download icon
  appears there instead - click it to run the install command. This label and
  icon need the start-up version check; turn it off in
  **Settings → Launchers** and cards show neither.
- **The main button** at the bottom: **Start** when stopped, **Open** when
  running. Clicking the card anywhere does the same thing.
- **A Configure button** beside it, which opens the edit form for that agent.
- **A small ×** in the top-right corner, visible only while the agent is running.
  Click it to stop the agent (see below).
- **A console panel** between the status line and the buttons, shown while an
  install, update or setup command is producing output. You can close it with its
  own × and reopen it from the right-click menu.

## Starting and stopping

**To start an agent:** click its card, or its **Start** button. The card switches
to **Starting...** and the agent's web server is launched in the background,
without a visible terminal window. Once the app can reach the agent's web
address, the card shows **Running** and the button becomes **Open**.

**To open its web interface:** click **Open** (or the card). See
[Embedded web interface](web-ui.md).

**To stop an agent,** there are two different actions, and they are not the same:

- **The × on the card.** This closes the agent only if *this app started it in
  the current session*. If the app was restarted after you started the agent, it
  no longer knows the process, and this action only tells you so - it stops
  nothing.
- **Force Stop** in the right-click menu. This looks for whatever program is
  serving the agent's web address and terminates it, no matter who started it.
  Because it can end a program you started elsewhere, it asks for confirmation
  first.

If neither is appropriate, the safest option is always to stop the agent with
its own command (for example by closing its terminal window).

## The right-click menu

Right-click any card:

| Item | What it does |
|---|---|
| **Start** / **Close** | Starts the agent, or stops the one this app started this session. |
| **Force Stop** | Terminates whatever is serving the agent's web address. Asks for confirmation. Only available while the agent is running. |
| **Open in browser** | Opens the agent's web address in your normal browser instead of a tab. Only available while running. |
| **Install** / **Update** | Runs the agent's install or update command. Only one of the two is shown, depending on whether the agent is installed. Disabled while the agent is running. |
| **Show output** | Reopens the console panel with the last captured output. |
| **Configure** | Opens the edit form for this agent. |
| **Open config folder** | Opens the agent's own configuration folder in your file manager. |
| **Re-initialize** | Forgets that the one-time setup command already ran, so it runs again on the next start. Only available if the agent has a setup command. |

## Adding an agent

There are two ways.

### From the page

Click **Add Launcher** in the page header. A form opens with these fields:

- **Name** (required) - the title shown on the card.
- **Command** (required) - the command line that starts the agent, for example
  `kimi web --port 58628`. It runs in the background with no visible window.
- **Web URL** (required) - the address of the agent's web interface, for example
  `http://127.0.0.1:4096`. Keep the port the same as in the command.
- **ID** - an internal, unique name. Leave it empty and one is generated from
  the name. It cannot be changed later.
- **Config directory** - the agent's own configuration folder. Used by **Open
  config folder**.
- **Icon** - see "Icons and colors" below.
- **Color** and **Card color** - see "Icons and colors" below.
- **Install command**, **Update command**, **Version command** - optional
  commands used by the **Install**, **Update** and version-detection features.
- **First-run setup command** (under **Advanced**) - a one-time command to run
  before the agent's first launch, for example to create a token file.
- **Token file** (under **Advanced**) - points at a file whose contents are
  handed to the agent on launch and appended to the web address when it is
  opened. Leave it empty unless the agent needs it.

Click **Save**. The card appears immediately.

### By editing the configuration file

Advanced users can add an agent by editing the configuration file directly,
though for most people the form above is easier. The file is `agents.json` in
your data folder (see [User guide](index.md) for the folder location). It is a
plain text configuration file; if you are not comfortable editing it, use the
form instead.

Each agent is one block in the `agents` list. The smallest useful entry has four
fields:

```json
{
  "id": "my-agent",
  "name": "My Agent",
  "command": "my-agent serve --port 3000",
  "webUrl": "http://127.0.0.1:3000"
}
```

- `id` - a short unique name for the agent, using letters, digits, `-` or `_`.
  It must not match any built-in agent.
- `name` - what appears on the card.
- `command` - the command that starts the agent.
- `webUrl` - the address of its web interface, including the port.

Save the file and restart the app to see the new card. Using an `id` that
already belongs to a built-in agent will not work - built-in agents are restored
from the app's own defaults on every start.

## Changing and removing agents

Open **Settings → Launchers**. You get a list of every agent with:

- **Edit** - opens the same form as **Add Launcher**, prefilled. The window also
  tells you whether changes take effect now or on the next launch.
- **Delete** - removes the agent after a confirmation. If the agent is running,
  deleting it does not stop it; use its own command for that. If you deleted a
  built-in agent, you can bring it back later.

To restore the built-in agents, click **Restore Defaults** on the launcher page,
or **Restore default launchers** under **Settings → Advanced**. This adds back
any built-in agents you deleted and resets any built-in agent you had edited.

!!! warning "Editing a built-in agent only lasts until you restart"
    Changes you make to a **built-in** agent (a new port, say) apply only for the
    current run of the app. On the next start, that agent is reset to the app's
    built-in definition. If you want a change to stick, add your **own** agent
    instead, or ask whoever builds the app to change the built-in default. Agents
    you added yourself are never touched by this reset.

## Icons and colors

The **Icon** field accepts three kinds of values:

- A **built-in icon**, chosen from the row of small icons just below the field.
  Click one to use it.
- A **file on your computer** - any `.svg` or `.png` file. You can write `~` for
  your home folder, or `%NAME%` for an environment variable, for example
  `%USERPROFILE%/icons/my-agent.svg`.
- A **web address** starting with `http://` or `https://`.

Leave **Icon** empty for the default placeholder.

The **Color** field is the agent's accent color in the form `#RRGGBB` (six
hexadecimal digits, for example `#89B4FA`). It tints the card while the agent is
running. **Leave it empty and a color is assigned automatically** from the
current theme's palette.

The **Card color** field sets the card's background while the agent is not
running. Leave it empty for the normal surface color.

## Installing and updating

If an agent is not installed, the launcher can run its install command for you:

- The **Install** action (the download icon, or **Install** in the right-click
  menu) runs the command you configured in **Install command**.
- The **Update** action (the ↻ icon next to the version, or **Update** in the
  menu) runs **Update command**.

Both require the agent's install/update command to be filled in, and the agent
must be stopped first - if it is running, the card asks you to close it before
installing. The command's output appears live in the card's console panel. If a
command fails, the panel stays open for a few seconds so you can read the error,
and the full message also appears in a popup.

## One-time setup

Some agents need a small preparation step before their very first launch, such
as generating a login token. If a **First-run setup command** is configured, it
runs automatically before the agent's first start. If it succeeds, the app
remembers it and will not run it again.

To run it again - for example after the token was lost - use **Re-initialize**
in the card's right-click menu.

## How "running" is judged

The app does not watch processes to decide whether an agent is running. Instead,
it visits the agent's web address every few seconds and treats **any response**
as "running". A refused connection or a timeout means "stopped".

One consequence: a web address that can be reached but does not show a working
page still counts as **Running**. If a card says **Running** but its page will
not open, see [Troubleshooting](troubleshooting.md).

## Tips

- Click the card itself when it says **Open** - it is the fastest way into the
  web interface.
- Give each agent a distinct color so you can tell the cards apart at a glance.
- Use **Open config folder** to find an agent's own settings and logs.
- Use the search box on the launcher page to filter by name, command or address,
  and the **All** / **Running** / **Not installed** buttons to narrow the list.

## Troubleshooting

**The card flashes red and a "Launch failed" message appears.** The agent's
command could not be started - usually because the tool is not installed, or is
installed but not on your system's command path. Try the **Install** action; if
it is already installed, open a terminal and run the agent's own command to see
the error. The card's console panel and the popup both show the reason.

**The card says Running but the page will not load.** The address responded,
which is all the check asks for. The page itself may still be starting up (give
it a few seconds), the port may be used by something else, or the address may be
wrong. See [Troubleshooting](troubleshooting.md).

**A built-in agent keeps forgetting my changes.** That is expected - see the
warning above. Add your own agent instead.

**Install or update seems to do nothing.** Open the card's console panel: the
command's output is shown there. A common cause is a command that needs a tool
such as Node.js which is not installed - check the badges in the status bar.

Curious how it works inside? See [Agent launcher](../development/agent-launcher.md).
