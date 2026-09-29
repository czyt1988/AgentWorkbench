# Settings

Every option in AgentWorkbench lives under **Settings**, the icon pinned at the
bottom of the sidebar (or `Ctrl+,`). The page has a list of sections on the left
and the chosen section on the right.

The version of the app you are running is shown at the bottom of the section
list.

## Appearance

Theme and font for the whole application.

- **Theme** - the color scheme. Two are built in: **Dark** and **Light**. The
  change applies at once.
- **Font** - the font used everywhere. The first entry, **Theme default**,
  follows the theme or your system.

Both take effect immediately; no restart needed. The full story, including how
to make your own theme, is in [Appearance](appearance.md).

## Launchers

The agents shown on the launcher page: add, edit and delete them, and see
whether each is running.

- **Add Launcher** - opens the form for a new agent.
- **Edit** on a row - opens the same form, prefilled.
- **Delete** on a row - removes the agent after a confirmation.
- Each row also shows the agent's start command and a running/stopped dot.

Deleting an agent does not stop it if it is running; stop it with its own
command. Deleting a built-in agent can be undone with **Restore default
launchers** under **Advanced** (or **Restore Defaults** on the launcher page).
Details and the important limitation on editing built-in agents are in
[Agent launcher](agent-launcher.md).

## Environment

Shows the Python and Node.js runtimes found on your computer. Several agents use
these to install or run, so a missing runtime is worth knowing about.

- **Python ...** or **Python not found**.
- **Node.js ...** or **Node.js not found**.
- **Re-detect** - checks again. Click it after installing or updating a runtime,
  or after changing your system's command path.

The badges in the bottom status bar show the same information. A red cross means
the runtime was not found. If you just installed one, restart AgentWorkbench so
it sees the updated system path.

## Skills

The folders scanned for `SKILL.md` files.

- Each source has a switch to turn it on or off, and an × to remove it.
- The field at the bottom adds a folder of your own; click **Add**.
- **Rescan** runs a fresh scan.

Paths may use `~` for your home folder and `%NAME%` for an environment variable.
Adding your own source replaces the built-in list - see [Skills](skills.md) for
that warning and the default folders.

## Web

How the agents' web interfaces are shown.

- **Surface** - **Embedded (in-app)** opens pages in tabs inside the app;
  **External (system browser)** opens them in your normal browser. The embedded
  option is the default and is only listed if this build supports it.
- **Chromium flags** - extra start-up options for the built-in browser engine.
  This is an advanced field; leave it empty normally. If embedded pages fail to
  start because of graphics-driver trouble, enter `--disable-gpu` here. Flags
  apply **after a restart**.

Choose **External (system browser)** if the embedded views will not work on your
machine, or if you simply prefer your own browser. **Open in browser** always
remains available from the launcher and the web page, whatever you pick here.
See [Embedded web interface](web-ui.md).

## Plugins

Third-party extensions that can add pages to the app. This is **off by default**
and is an advanced, experimental feature.

- **Enable plugins (experimental)** - the master switch.
- Each found plugin has its own switch.
- Plugins load at start-up, so **changes take effect after a restart**.

Plugins run inside the application with no isolation: a plugin can do anything
the application can do. Only enable plugins you trust. See
[Plugins](plugins.md).

## Advanced

Storage and reset actions.

- The path of the launchers configuration file is shown here.
- **Open data folder** - opens the folder that holds all of the app's data.
- **Restore default launchers** - puts the launcher list back to factory state.
  It re-adds any built-in agents you deleted and resets any edits you made to
  built-in agents. Agents you added yourself are kept. This acts immediately, so
  use it deliberately.

## Where settings are stored

All of the app's settings live in one plain-text file, `settings.json`, in your
data folder. **You normally never need to edit it by hand** - the settings page
writes it for you. If you do edit it, restart the app for the change to take
effect. Any entry you leave out automatically keeps its default value, and an
entry the app does not recognize is ignored.

The data folder also holds the launcher list (`agents.json`), a record of which
agents have finished their one-time setup, your themes, the agents' saved web
login state, and the log files.

## Backing up and moving to another computer

To back up your setup, or move it to a new computer, **copy the whole data
folder**. It contains:

- your launcher list,
- your settings,
- your themes,
- the saved login state of the agents' web interfaces,
- the log files.

Paste the copied folder into the same place on the new machine (see the
[User guide](index.md) for the folder location) and start the app. Because it is
just a folder of files, copying it is enough - there is nothing else to install.

Curious how it works inside? See [Shell and navigation](../development/shell-and-navigation.md).
