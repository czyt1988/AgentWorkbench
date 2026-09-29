# Plugins (user view)

Plugins let third parties add their own pages to AgentWorkbench. A plugin can
add a new entry to the sidebar, next to the built-in pages.

## What it is

A plugin is a folder you drop into the app's data folder. The app reads what it
finds there and, if you enable it, adds the page the plugin provides.

## Off by default, and why

**Plugins are disabled until you turn them on.** This is deliberate: a plugin
runs inside the application itself, with no separation between the two. A plugin
can do anything the application can do. You should only enable plugins from
sources you trust, and only the ones you actually want.

## Enabling plugins

1. Open **Settings → Plugins**.
2. Turn on **Enable plugins (experimental)**.
3. Turn on the switch next to each plugin you want to use.
4. **Restart AgentWorkbench.** Plugins are loaded when the app starts, so a
   switch only takes effect after a restart.

After the restart, the plugin's page appears in the sidebar.

## Installing a plugin someone gave you

1. If the app is running, close it (or you can add the files first and restart
   later).
2. Open your data folder (**Settings → Advanced → Open data folder**). If there
   is no `plugins` folder, create it.
3. Copy the plugin's folder into it. The folder should contain the plugin's
   description file and its program files.
4. Start the app, open **Settings → Plugins**, and turn the plugin on.
5. Restart the app. The new page appears in the sidebar.

The settings page lists every plugin it finds, even while plugins are off, so
you can see what is available before enabling anything.

## Disabling or removing a plugin

- **Disable it** by turning its switch off (and restarting). It stays installed
  but is not loaded.
- **Remove it** by deleting its folder from the `plugins` folder. Restart the
  app to confirm it is gone.

Either way, a plugin's page disappears from the sidebar after the restart.

## What a plugin can and cannot do

- It can **add pages** to the sidebar.
- It can **read** a small part of your settings and your theme colors.
- It **cannot** change your launcher list or your settings - that access is
  read-only.
- But because it runs **inside the application's own process**, with no
  isolation, it can in principle do anything the application can do. Treat a
  plugin like a program you are choosing to run, not like a sandboxed web page.

## Troubleshooting

**A plugin does not appear after I restart.** Check that its folder is inside the
`plugins` folder of your data folder and that the folder has the plugin's files
in it, not just a zip. Then check that **Enable plugins (experimental)** and the
plugin's own switch are both on.

**I turned it on but nothing changed.** Restart the app - plugin switches only
apply at start-up.

**A plugin acts up or the app misbehaves.** Turn the plugin off in **Settings →
Plugins**, restart, and see whether the problem goes away. If it does, the plugin
is the cause; contact whoever provided it. Error details are written to the log
files in your data folder (see [Troubleshooting](troubleshooting.md)).

Curious how it works inside? See [Plugin host](../development/plugin-host.md).
