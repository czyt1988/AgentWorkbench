# Markdown edit module

`awb_markdownedit` (`src/markdownedit/`) is the Markdown editing engine: the formatting actions behind the command bar and the context menu, the syntax highlighter, and the find/replace primitives. It is a domain module like any other, with one extra property that the others do not have: **it is deliberately self-contained enough to be copied into another project** and consumed there together with nothing but `awb_theme`. This page documents its boundary, its public surface, and the integration steps for a host project; how the workbench page drives it is described in [Markdown workbench](markdown-workbench.md), and the layering rules that keep it a tree leaf are in [Layers and dependencies](../architecture/layers-and-dependencies.md).

There is no user guide page for this module on its own: the user-visible behaviour (buttons, shortcuts, the context menu) belongs to the workbench page, so see [the workbench guide](../guide/markdown-workbench.md) for that perspective.

## What is in the module

| File | Class / component | Responsibility |
|---|---|---|
| `src/markdownedit/MarkdownEdit.{h,cpp}` | `MarkdownEdit` | The QML-facing entry: `attach()` installs highlighting on a `TextArea`'s document, the `apply*`/`insert*` family performs the formatting actions, the `is*At`/`headingLevelAt` queries feed checkable buttons, and `find` / `replaceAndFindNext` / `replaceAll` are the find/replace primitives. Every action is a static `QTextCursor` operation wrapped in a `Q_INVOKABLE` method, one edit block each |
| `src/markdownedit/MarkdownHighlighter.{h,cpp}` | `MarkdownHighlighter`, `MarkdownColors` | A lightweight per-block highlighter: fenced code blocks (a cross-block state machine), headings, dividers, quotes, list markers, and inline code/bold/italic/strikethrough/links. Deliberately not CommonMark-complete. Colours arrive through `MarkdownColors` injection — the highlighter does not know `Theme` |
| `src/markdownedit/qml/MarkdownContextMenu.qml` | `MarkdownContextMenu` | The editor's right-click menu: a formatting toolbar row plus undo/redo/cut/copy/paste entries. It is host UI chrome, not engine logic — it only forwards to the singleton and the editor's own members |

The engine half (`MarkdownEdit`, `MarkdownHighlighter`) has no dependency on `awb_core`, on any other domain, or on the application: its link line is `awb_theme` + Qt Core/Gui, with Qt Quick a `PRIVATE` link that exists only for `MarkdownEdit`'s `QQuickTextDocument` read. The highlighter alone is pure Qt Gui — tests construct a `MarkdownColors` directly, no theme involved.

Two design properties matter for reuse:

- **The actions are static cursor operations.** `applyBoldToCursor()` and its siblings take a `QTextCursor` and nothing else, so they are testable against a bare `QTextDocument`, work without focus (which is why a menu can take focus and still act on the editor's selection), and are independent of QQuick types. The `Q_INVOKABLE` instance methods are a thin adapter that reads the `TextArea`'s document and selection and writes the result back.
- **Preferences are parameters, not state.** Case sensitivity and whole-word matching are arguments to `find` / `replaceAll`; the module persists nothing and reads no configuration. Where a host wants to remember such preferences is the host's business (this application stores them in `tools.json`'s `editor` section).

## Tests

`tst_markdownedit` drives the engine at the `QTextDocument` level: the cursor semantics of every action (empty selection, single-line selection, multi-line selection, the boundary discipline around trailing newlines), the highlighter's block state machine, and the find/replace primitives including the wrap-around behaviour. It links only `awb_markdownedit` and Qt — no theme, no QML engine, so it runs in every build configuration including Qt 5 and WebEngine-OFF.

## Using the module in another project

The module is consumed by source inclusion — copy or subtree/checkout `src/markdownedit/` and `src/theme/` (the theme engine it links), then wire four things:

1. **Build**: add both directories with `add_subdirectory` (or compile the sources into your own target). The module's `CMakeLists.txt` expects `${CMAKE_SOURCE_DIR}/src` as an include root; adjust `target_include_directories` if your layout differs. `awb_theme` further links `awb_core` — bring `src/core/` along, or replace the theme dependency by constructing `MarkdownHighlighter` with your own `MarkdownColors` and skipping `MarkdownEdit`'s `attach()` colour plumbing.
2. **Construct and register**: `MarkdownEdit` is a plain `QObject` singleton in the hands of its host. Construct it with a `theme::Theme *` and register it on whatever URI your QML uses:

```cpp
awb::theme::Theme theme; // your theme instance
awb::markdownedit::MarkdownEdit markdownEdit(&theme);
qmlRegisterSingletonInstance("YourApp.App", 1, 0, "MarkdownEdit", &markdownEdit);
```

3. **Attach the editors**: in each `TextArea` that should highlight, call `MarkdownEdit.attach(textArea)` once (repeat calls are no-ops). Route your toolbar/menu actions to the `apply*`/`insert*` methods, and `Keys.onTabPressed`/`Keys.onBacktabPressed` to `handleTab`/`handleBacktab` if you want VSCode-style multi-line indent/outdent.
4. **The context menu**: `MarkdownContextMenu.qml` depends on this application's shell components (`AMenu`, `AIconButton`, …) and its `theme.*` tokens, so it does not travel as-is. Either write your own menu over the same `Q_INVOKABLE` surface (all it needs is the editor reference), or port the component with your own menu primitives.

Everything the singleton exposes is `Q_INVOKABLE` or a signal, so `check_architecture`-style verification (every QML-called method must be callable) applies in the host too; the rule that caught three silent breakages in this application is documented in [Layers and dependencies](../architecture/layers-and-dependencies.md).

## Related

- [Markdown workbench](markdown-workbench.md) — how the page drives this module: the command bar, the find field, the editors.
- [Layers and dependencies](../architecture/layers-and-dependencies.md) — why domain modules may not depend on each other, and where `awb_markdownedit` sits in the graph.
- [Theme engine](theme-engine.md) — the one dependency besides Qt: where the highlighter colours come from.
