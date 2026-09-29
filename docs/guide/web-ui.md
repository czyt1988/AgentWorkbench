# Embedded web interface

The **Agent Web UI** page shows the agents' web interfaces as tabs inside
AgentWorkbench, so you can keep an eye on several agents without leaving the app
or juggling browser windows.

## What it is

Each tab hosts one agent's web page. The pages run in a built-in browser that is
part of the app, with one private browsing profile per agent.

## When to use it

- To work with an agent's web interface without switching to a browser.
- To keep several agents' interfaces open side by side.
- To reach a "restart" button when an agent's page says the agent is not running.

## Opening a view

- From the **Agent Launcher** page, click a running agent's card or its **Open**
  button.
- From this page's empty state, pick a running agent from the list and click
  **Open**. The empty state is also reachable later through the **Home** button
  in the toolbar.

Each agent has at most one tab. Opening an agent that already has a tab brings
that tab forward instead of creating a second one.

## Working with tabs

The strip along the top lists every open tab.

- **Switch tabs:** click a tab. You can also press `Ctrl+Tab` / `Ctrl+Shift+Tab`.
- **Close a tab:** click its ×, press `Ctrl+W`, middle-click the tab, or pick
  **Close tab** from the **More actions** menu.
- **Reload:** press `F5`, double-click the tab, or use the toolbar's refresh
  button. While a page is loading, that same button becomes a stop button.
- **Copy the address:** **More actions → Copy URL**.
- **Zoom:** **More actions → Zoom in / Zoom out / Reset zoom**, or the keyboard
  shortcuts listed in the [User guide](index.md).
- **Open in your browser:** click the external-link button in the toolbar. This
  is always available.
- **Go back to the agent list:** click **Home** in the toolbar. It does not close
  any tabs.

Each tab shows its agent's name, a small status dot and, while loading, a
progress line. Hovering a tab tells you its state in words.

### Tabs that are "released"

To keep memory use reasonable, the app keeps only a limited number of web pages
alive at once. When you open more tabs than that limit, the least recently used
ones are **released**: their tab grays out and shows **View released to free
memory** with a **Restore view** button.

"Released" does not mean closed. The tab is still there and the site is not
logged out; only the page is unloaded to free memory. Click **Restore view** and
the page comes back.

### Full screen

If a web page asks to go full screen (for example a game or a preview), the app
hides its own tab bar and lets the page fill the window. Press `Esc` to leave
full screen and bring the tab bar back.

## What the overlays mean

When a tab cannot show its page, an overlay appears over the tab with a short
explanation and one or more buttons.

| What you see | What it means | What to do |
|---|---|---|
| **Loading ...** with a spinner | The page is still loading. | Wait, or press **Cancel** to stop. |
| **This agent is not running** | The app could not reach the agent's web server. | Click **Restart agent** to start it again, or **Retry** to try the page once more. |
| **The page crashed** | The page's rendering process stopped. | Click **Reload** to bring it back, or **Open in browser**. |
| **Failed to load the page** | The address could not be loaded; a short reason is shown. | Click **Retry** or **Reload**, or **Open in browser**. |
| **View released to free memory** | The tab was unloaded to save memory. | Click **Restore view**. |

## Downloads

Files you download from an agent's web page go to your system's usual Downloads
folder. A small message appears when a download starts, finishes or is
interrupted.

To save downloads somewhere else, you would edit the settings file (the
download-folder entry is `web.downloadDir`); this is an advanced change that is
normally unnecessary. See [Settings](settings.md) for where that file lives.

## Logins and sessions

**Each agent keeps its own login state, separately from the others.** If you log
in to one agent's web interface, that login is remembered the next time you open
the app, and it does not affect any other agent. Two agents running on the same
computer never see each other's cookies or saved sessions.

This is why the app never shares one browser session between agents: it is the
only way to keep their logins from interfering with each other.

## When to use an external browser

The built-in browser is convenient, but it is not a full replacement for your
normal browser. Use **Open in browser** when:

- A page stays blank or never finishes loading inside the app.
- A page needs to open a pop-up or an outside service in a new window.
- The agent's page simply will not work in the built-in browser.

The same login/session separation does not apply to your external browser, but
its own tabs and windows behave normally.

## Troubleshooting

**The page will not open at all.** Check that the agent is actually running
(see the launcher page), then try the toolbar's refresh button, then **Open in
browser**. If the external browser works and the embedded one does not, the
problem is with the built-in browser - see [Troubleshooting](troubleshooting.md).

**The page is blank white.** Wait a few seconds first; some interfaces take a
while to appear. If it stays blank, use **Open in browser**. Very new web pages
may not run in the built-in browser at all (see the troubleshooting page for
details).

**It spins forever.** Click the stop button (the same toolbar button that was
refresh), then reload. If the agent was just started, give it a few tens of
seconds - some agents take a while before their page is ready.

**It shows a login or token prompt.** Use the login dialog the app shows, or
open the page in your browser. If the app's page keeps asking, the agent may need
its one-time setup - see [Agent launcher](agent-launcher.md).

Curious how it works inside? See [Web tabs](../development/web-tabs.md).
