# Plugins

AgentWorkbench can be extended with plugins. This page explains what a
plugin is, how to enable one, and how to write your own.

!!! warning "Trust level"
    Plugins run **inside the application's process** — there is no
    sandbox. A plugin can do whatever the application itself can do. Only
    enable plugins you trust. This matches the trust model of the embedded
    web views (see [WebEngine embedding](research/webengine-embedding.md)).

## Enabling plugins

Plugins are **disabled by default**. To use them:

1. Open **Settings → Plugins**.
2. Switch on **Enable plugins (experimental)**.
3. Toggle the individual plugin you want.
4. Restart AgentWorkbench — plugin libraries are loaded once at startup.

The settings page lists every plugin found in the plugins directory
(`<your data directory>/plugins/`), even while disabled — reading a
manifest never loads code.

## Anatomy of a plugin

```
<dataRoot>/plugins/
  hello/
    plugin.json     manifest (id, name, version, apiVersion, entry, pages)
    hello.dll       the compiled plugin
```

`plugin.json`:

```json
{
  "id": "hello",
  "name": "Hello",
  "version": "0.1.0",
  "apiVersion": 1,
  "description": "The smallest example plugin.",
  "author": "you",
  "entry": "hello.dll",
  "pages": [
    { "id": "hello", "title": "Hello",
      "icon": "qrc:/icons/bot.svg",
      "source": "qrc:/hello/HelloPage.qml",
      "section": "extensions", "order": 50 }
  ]
}
```

- `apiVersion` must match the host's plugin API version (`1` for 0.4.0);
  a mismatch logs a warning and the plugin is refused.
- `pages` are registered through **the same registration path as the
  built-in pages** — a plugin page is an ordinary sidebar entry in the
  `extensions` section.

## Writing a plugin

A plugin is a shared library that includes
`src/plugin_api/PluginApi.h` and exports two C symbols:

```cpp
#include "plugin_api/PluginApi.h"

extern "C" {

AWB_PLUGIN_EXPORT int awb_plugin_api_version()
{
    return awb::plugin::ApiVersion;
}

AWB_PLUGIN_EXPORT int awb_plugin_register(awb::plugin::Services *services)
{
    awb::plugin::PageDescriptor page;
    page.id = "hello";
    page.title = "Hello";
    page.source = "qrc:/hello/HelloPage.qml";
    page.section = "extensions";
    services->registerPage(page);
    return 0; // non-zero = the host logs and ignores the plugin
}

} // extern "C"
```

The `Services` interface is the only way a plugin touches the host:

| Service | Purpose |
| --- | --- |
| `registerPage` / `unregisterPage` | sidebar pages (same path as built-ins) |
| `addWebSurface` | contribute an additional Web surface kind |
| `dataDir` | a private writable directory for the plugin |
| `log` / `notify` | application log and toast notifications |
| `themeColor` | read-only theme token access (`#rrggbb`) |
| `settingsValue` | read-only access to a small set of settings keys |

Rules of the ABI:

- only Qt types cross the boundary — never a host C++ class;
- a broken manifest, a version mismatch or a failed load is **logged and
  skipped** — a plugin can never keep the application from starting;
- plugins cannot write `agents.json` or settings; theme and settings
  access is read-only.

## The example plugin

`examples/plugins/hello/` is a complete, minimal plugin. Build it with:

```bash
bash scripts/build.sh -- -DAWB_BUILD_PLUGIN_EXAMPLES=ON
```

The build copies `hello.dll` and `plugin.json` into
`<dataRoot>/plugins/hello/`, where the host discovers it. Enable it in
Settings → Plugins and restart — the **Hello** page appears in the
sidebar's extensions section.

## Development notes

- The QML for a page ships as a **plugin-side resource** (`qrc:/…`);
  resources in shared libraries register when the library loads.
- Page QML can `import AgentWorkbench.App` for the shared tokens and
  globals (`theme`, `workbench`, `ui`, …) exactly like built-in pages.
- The API is versioned: bump `awb::plugin::ApiVersion` on any breaking
  change to `Services` or `PageDescriptor`.
