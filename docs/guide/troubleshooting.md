# Troubleshooting

This page is organized by the symptom you are seeing. For each one, it lists the
likely cause and what you can do.

## An agent will not start, or its card flashes red

- **The tool is not installed.** Use the **Install** action on the card, or
  install the agent the way its own documentation describes.
- **The tool is installed but not on your system's command path.** A terminal
  that can run the command, but the app that cannot find it, points at this.
  Reinstall the tool, or check that its install folder is on your system's path,
  then restart the app.
- **The command is wrong.** Open the agent's **Configure** form and check the
  start command against the agent's own instructions.

For the exact reason, look at the card: a red border and a short message appear
in place of the status word, a popup shows the full message, and the card's
console panel keeps the command's output. All three are worth reading.

## The card says Running, but the page will not open

The app decides "running" by getting any reply from the agent's web address -
even an error reply counts. So a card can say Running while the page itself is
not usable.

- **It is still starting up.** Some agents take a few tens of seconds before
  their page is ready. Wait and reload.
- **Something else is using that port.** Another program may have taken the
  port, and the app is talking to it instead. See "Port conflicts" below.
- **The address is wrong.** Open **Configure** and compare the web address with
  the port in the start command.

## The embedded page is blank or spins forever

Try these in order:

1. **Open in browser** (from the web page's toolbar or a card's right-click
   menu). If the page works in your browser, the problem is with the built-in
   browser.
2. **Add `--disable-gpu`** to the browser start-up options under **Settings →
   Web**, then restart the app. This often fixes blank pages on machines with
   graphics-driver trouble.
3. **Switch to your system browser**: set **Surface** to **External (system
   browser)** under **Settings → Web**. Then agent pages always open in your own
   browser.
4. If all of the above fail, the page may simply be too new for the built-in
   browser. Some builds of the app use an older built-in browser engine, and a
   few very modern web pages cannot run in it. This is not something you can fix
   with a setting - use **Open in browser** for that agent.

## I cannot stop an agent

The app can stop only the agents it started **in the current session**. If it
was started earlier, or from another terminal, it does not know the process.

- Use **Force Stop** in the card's right-click menu. This looks for whatever is
  serving the agent's web address and terminates it, even if the app did not
  start it.
- Or stop the agent with its own command (for example close its terminal
  window). This is the safest option when you are not sure what a Force Stop
  would hit.

## Port conflicts

Two agents, or an agent and another program, may try to use the same port.

- The default OpenCode launcher uses port `4096`. If something else is already
  on `4096`, change **both** the port in OpenCode's start command and its web
  address to a free one, using **Configure**.
- In general, to move an agent to a different port, edit its **Command** and
  **Web URL** together so both use the new port. They must match.

(OpenCode chooses a random port by default, which is why the app pins it to a
fixed one - a fixed port is what makes the running check and "open in browser"
dependable.)

## Starting the same agent twice

Opening an agent that already has a tab brings that tab forward instead of
starting a second copy. But if the agent was started **from somewhere else** (a
terminal you opened yourself), the app cannot see that process, and clicking
**Start** may launch a second instance.

Before starting an agent, check whether its card already says **Running**. If it
does, click **Open** instead.

## The interface is hard to read or the colors look wrong

- Try the other built-in theme under **Settings → Appearance**. Both are meant
  to be readable; comparing helps you describe the problem precisely.
- If a specific element is unreadable, note which theme and which element, and
  report it - it is most likely a bug.

## I cannot find the configuration, or I want to reset something

- **Where is everything?** **Settings → Advanced → Open data folder**.
- **Restore the agent list:** **Settings → Advanced → Restore default
  launchers**, or **Restore Defaults** on the launcher page. This brings back
  deleted built-in agents and resets your edits to built-in agents; agents you
  added yourself are kept.
- **Remove a custom theme:** delete its file from the `themes` subfolder of your
  data folder.
- **Disable plugins:** turn them off under **Settings → Plugins** and restart.

## The logs

The app writes a log file under the `log` subfolder of your data folder. It
records what the app does: the commands it runs, their results, configuration
changes, which agents it detected as running, and plugin loading. The file
rotates when it reaches 5 MB and keeps the three most recent files, so it never
grows without bound.

The log does **not** contain your login tokens.

When reporting a problem, attach the log file - it is usually the fastest way to
find the cause.

## Starting completely fresh

If you want a brand-new setup:

1. Quit AgentWorkbench.
2. **Back up** your data folder first if there is anything in it you might want
   later (for example copy it somewhere safe).
3. Delete the data folder.
4. Start the app. It recreates the folder with fresh defaults.

Be aware that this loses your launcher list, your settings, and the saved login
state of the agents' web interfaces.

## What to include when reporting a problem

- **The app version.** It is shown at the bottom of the settings page.
- **Your operating system.**
- **What you did, step by step**, and what happened instead.
- **The log file** from your data folder.

Curious how it works inside? See [Development](../development/index.md).
