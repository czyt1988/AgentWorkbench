# Coding standard

This document defines what AgentWorkbench code looks like: **C++ / Qt / QML conventions, naming, file organisation, and the Doxygen comment rules**.

Together with two other documents it forms the full set of constraints:

| Document | Scope |
|---|---|
| `AGENTS.md` | How to build, test, commit and use worktrees; hard module contracts and known traps |
| `designs.md` | Window skeleton, sidebar rules, component reuse catalogue, visual language |
| **This document** | Files and naming, C++/Qt/QML style, **comment rules** |

## 0. Scope and force

- Applies to every C++, QML and CMake file under `src/`, `app/`, `tests/` and `examples/`.
- **New code must follow this document in full.** Part of it is enforced at build time by `scripts/check-architecture.sh` (the `check_architecture` ctest): no literal colours in QML, module dependency direction, ASCII-only `tr()`/`qsTr()` source strings, no UI types in `src/core/` or `src/theme/`, every method QML calls must be `Q_INVOKABLE`, every property QML assigns must have `WRITE`.
- When you edit existing code, bring **only the classes and functions you touch** up to the standard. Do not start whole-file or repo-wide mechanical rewrites — this repository is routinely developed in several worktrees at once, and full-file rewrites create conflicts that nobody can review.
- Where this document disagrees with a pattern that happens to exist in the code, this document wins.

## 1. Files and basic formatting

- Source files are **UTF-8 without BOM**, **LF** line endings, **4-space** indentation, no tabs. On MSVC, Qt6's CMake targets inject `/utf-8`, so the compiler reads sources as UTF-8 and the Chinese comments are safe.
- Lines stay **under 100 characters**, preferably under 90. When an expression wraps, align the continuation with the first argument.
- A file is named after the class it wraps, in PascalCase: `AgentHealthMonitor.h` / `AgentHealthMonitor.cpp`. One main class per file; do not put several unrelated classes in one header.
- **No file-header comment blocks**: no author, date, copyright or change log — git records all of that. A file's or type's responsibility belongs in that class's own Doxygen comment.
- Headers must be self-contained: including one on its own must compile. Prefer forward declarations over includes to stop headers from spreading (`AgentsFacade.h` is the model); Qt value types and containers (`QString`, `QVariantMap`) are included directly.
- Include order is fixed, in three blocks separated by blank lines:

  ```cpp
  #include "agentcatalog/AgentsFacade.h"   // 1. this file's own header, always first

  #include "agentcatalog/AgentModel.h"     // 2. other project headers, alphabetical by path
  #include "core/Settings.h"

  #include <QDir>                          // 3. Qt and the standard library, alphabetical
  #include <QFile>
  ```

- The namespace is always `awb::<module directory>`: `awb::core`, `awb::theme`, `awb::agentcatalog`, `awb::skillcatalog`, `awb::shell`, `awb::web`, `awb::workbench`, `awb::plugin`. Use the C++17 nested form `namespace awb::core {`, and always close with the comment: `} // namespace awb::core`.
- Header guards are `AWB_<MODULE>_<FILE>_H` in capitals, e.g. `AWB_CORE_PATHS_H`. When a module is renamed, do **not** chase the existing guards — it buys nothing and creates diff noise.
- **`using namespace` is forbidden in headers** — it leaks into every translation unit that includes the header. Do not use it in `.cpp` files either; to shorten a name, add a using-declaration inside the `.cpp`: `using awb::core::EnvExpander;`.
- Private helpers in a `.cpp` go into an anonymous namespace (`namespace { ... }`), not into `static` functions.

## 2. Naming

| Entity | Rule | Examples |
|---|---|---|
| Class / struct / enum type | PascalCase | `AgentModel`, `OpResult`, `WebProfilePaths` |
| Enumerator | PascalCase | `IdRole`, `NameRole` (model roles always end in `Role`) |
| Function / method / local / parameter | camelCase | `sessionUrlFromOutput`, `dataRoot` |
| Member variable | `m_` + camelCase | `m_definitions`, `m_health` |
| Static member | `s_` prefix | `s_testRoot` |
| Compile-time constant | `k` + PascalCase, `constexpr` | `kSessionUrlTickMs`, `kMaxVisible` |
| Macro / header guard / build option | capitals with `AWB_` prefix | `AWB_TEST`, `AWB_ENABLE_WEBENGINE` |
| Boolean | starts with `is` / `has` / `can` / `should` | `isDataRootOverridden`, `hasLaunchedAgents` |
| Shared QML component | `A` prefix, in `src/shell/qml/components/` | `AButton`, `ACard` |
| QML page | PascalCase + `Page`, inside its module | `AgentGridPage.qml`, `SettingsPage.qml` |

More conventions:

- Getters carry no `get` prefix: `model()`, `sessionUrl()`, `configFilePath()`.
- A signal states a fact that already happened, named in the past tense or as a state transition: `runningChanged`, `launchFailed`, `installFinished`, `sessionUrlChanged`. No `on` prefixes, no imperative verbs.
- Slots and `Q_INVOKABLE` methods are requests for work, named with a verb: `launch`, `stop`, `refresh`, `addAgent`.
- No pinyin, no meaningless abbreviations. Names like `ctx`, `cfg`, `mgr` must be spelled out once they cross a file boundary.

## 3. C++ rules

The language baseline is **C++17**. The rules below are ordered the way you meet them while writing code.

### 3.1 Basics

- Pointers use `nullptr`, never `0` or `NULL`.
- Overrides must be marked `override`; do not keep the old `virtual`-prefix style on overrides.
- Single-argument constructors are `explicit` (copy/move constructors excepted).
- No raw `new` / `delete`: `QObject`-derived objects use parent ownership, everything else uses value semantics or `std::unique_ptr`.
- Casts are `static_cast` / `qobject_cast` only. No C-style casts.
- `auto` only where the concrete type does not matter or is too long to read (iterators, `std::make_unique`); write the type out when it carries meaning.
- Members get in-class defaults (`bool ok = true;`) instead of being repeated in a constructor initialiser list.
- Parameters: pass `QString`, containers and custom types by `const &`; pass by value for small types or when the function needs its own copy. Rely on RVO — never `return std::move(x)`.
- `const` wherever it applies: a member function that does not modify the object is a `const` member function.
- Return early and keep nesting shallow: handle failures and edge cases first, write the happy path last. An `if` nested more than three deep is a function that wants to be extracted.
- **A single-statement `if` / `for` / `while` always gets braces**, no exceptions. When editing existing code, add braces to the statements you touch; do not sweep whole files mechanically.
- String literals: user-visible text goes through `tr()`, everything else through `QStringLiteral`. Path fragments use `QStringLiteral("/log")`, not `QLatin1String` or a bare literal.
- Enums: expose plain `enum` + `Q_ENUM` when QML or meta-objects need them (see `AgentModel::Roles`); use `enum class` for purely internal state.

### 3.2 Error handling and cross-module contracts

- **Nothing throws across a module boundary**: fallible synchronous operations return `core::OpResult` (`{ ok, error }`, readable from QML as `.ok` / `.error`); asynchronous work reports through signals. Exceptions cannot cross the C++/QML boundary, so for anything QML touches this is a hard constraint.
- **Use exceptions, not `std::optional`**: inside a module (pure C++), a fallible operation or a possibly-absent value is expressed by throwing; do not introduce `std::optional` returns — callers forget the `has_value()` check and the failure propagates silently as an empty value. Division of labour with the rule above: `OpResult` / signals at module boundaries (especially towards QML), exceptions inside a module.
- A failure carries a readable reason (in English). Returning `false` and leaving the caller to guess is not acceptable.
- Do not swallow errors: when you cannot handle and cannot fix a problem, log a `qWarning()` and propagate the failure.
- Modules talk only through their public interfaces. The domain modules (`agentcatalog`, `skillcatalog`, `web`) have zero dependencies on each other; cross-domain behaviour lives in `awb_workbench`.

### 3.3 The header / implementation boundary

- A header is a **contract**: type definitions, member declarations, inline getters, `Q_PROPERTY`, signals. Implementations live in the `.cpp`.
- Inside a header, only single-line inline getters/setters, templates, and genuinely necessary small inline functions are allowed.
- Do not explain implementation details in header comments — that text belongs in the `.cpp` (section 6).

## 4. Qt / QObject rules

### 4.1 Class layout

Members are declared in a fixed order so a reader can see the contract top-down:

```cpp
class AgentsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)

public:
    // 1. type aliases, enums, constructor
    // 2. plain methods, Q_INVOKABLE methods
Q_SIGNALS:
    // 3. signals
public Q_SLOTS:
    // 4. public slots
private:
    // 5. private methods
    // 6. member variables (m_ prefix)
};
```

- **Every `QObject`-derived class carries the `Q_OBJECT` macro**, without exception: without it moc generates no meta-object code for the class, and signals/slots, `Q_PROPERTY` and `qobject_cast` stop working — silently in most cases.
- **Use the capitalised Qt macros throughout**: `Q_OBJECT`, `Q_PROPERTY`, `Q_ENUM`, `Q_INVOKABLE`, `Q_SIGNALS`, `Q_SLOTS`, `Q_EMIT`. **The lowercase `signals`, `slots` and `emit` keywords are forbidden** — they only exist when `QT_NO_KEYWORDS` is not defined, while the capitalised macros compile identically under any Qt configuration. This is also what the official Qt best practices recommend.
- `Q_OBJECT` is the first line after the class name, `Q_PROPERTY` / `Q_ENUM` follow immediately.
- Constructors take `QObject *parent = nullptr` as the last parameter with a default; injected dependencies (`Settings *`, `Theme *`) come before it.
- Write a destructor only when non-QObject resources need cleanup; otherwise `= default` the base destructor.

### 4.2 The QML interface

- **Every method QML calls must be `Q_INVOKABLE` (or a slot/signal), and every property QML assigns must have `WRITE`.** A plain method is absent from the meta-object tables; QML throws "is not a function" at the call site, which only a click would reveal. `check_architecture` rule 5 catches it at build time.
- Expose properties with `Q_PROPERTY`, ordered `READ` / `WRITE` / `NOTIFY`; immutable ones are `CONSTANT`. Emit `xxxChanged()` whenever the value changes.
- APIs aimed at QML are **asynchronous**: `refresh()` returns immediately and the result arrives via a `refreshFinished`-style signal. That way moving the work to a thread later does not change the QML.
- Models consumed by QML derive from `QAbstractListModel` and expose `roleNames()`; role names and their order are a stable contract with QML — check every page before changing them.
- Global objects are registered through `qmlRegisterSingletonInstance` at the assembly point, on the pure C++ URI `AgentWorkbench.App`, with a **capitalised type name**. Never use `setContextProperty`, never hand-register singletons on the `AgentWorkbench` URI. The lowercase contract names in QML (`theme`, `agents`, `web`, …) are aliases declared at the root of `MainWindow.qml`.

### 4.3 Signals and slots

- `connect` always uses the member-pointer or lambda form:

  ```cpp
  connect(m_runtime, &AgentRuntime::launchFailed, this, &AgentsFacade::launchFailed);
  connect(&m_timer, &QTimer::timeout, this, [this]() { poll(); });
  ```

  `SIGNAL()` / `SLOT()` string macros are forbidden — they give up compile-time checking and fail silently after a rename.

- **A signal and its slot take literally the same parameter types, including the value form**: do not pair a by-value signal parameter with a `const &` slot parameter (signal `void changed(Foo foo)` with slot `void onChanged(const Foo &foo)`). Qt fails to recognise such a connection in some situations — it compiles yet never connects, or drops arguments at runtime — and the failure leaves almost no trail, which makes it extremely expensive to debug.
- **Passing a pointer to a custom class through a signal/slot needs the complete type at the `connect` site**: with only a forward declaration in the header this is an "incomplete type" compile error. Keep the existing split — headers forward-declare wherever possible, and the `.cpp` includes the class's full header; do not move the include into the header for the sake of a `connect`.
- **Emit a signal only when the state actually changes (edge-triggered).** Re-emitting the same state makes subscribers reload and replay animations; `AgentHealthMonitor::runningChanged` is the reference implementation of this rule.
- One signal states one fact. When "who + result + reason" must travel together, use several parameters or an `OpResult` — do not invent a catch-all signal.
- Long operations (network, processes, disk scans) must not block the UI thread, and synchronous waits such as `waitForFinished()` are not allowed; use signals and callbacks.

### 4.4 Threads and the GUI

- **Never manipulate GUI controls directly from a thread**: Qt's GUI objects (windows, widgets, QML elements, `QQuickItem`) may only be accessed from the main thread. Cross-thread access is undefined behaviour — it shows up as sporadic crashes and a garbled screen, not as a reproducible error.
- A worker thread hands results back to the main thread in exactly two ways: **signals and slots** (a cross-thread connection is queued onto the receiver's thread automatically) or an explicit `QMetaObject::invokeMethod(receiver, ...)`. The thread only emits the signal / calls `invokeMethod`; every line that touches the UI lives in the slot (or lambda) that runs on the main thread.
- The QML-facing APIs of this project are asynchronous by design (`refresh()` returns immediately + an `xxxFinished` signal, see 4.2) precisely to leave room for moving work into a thread: results always arrive via signals, and the UI is only ever updated in a main-thread slot.

### 4.5 Container iteration and high DPI

- **Never range-iterate a non-const Qt container directly.** Qt containers are implicitly shared (COW): a range-`for` over a non-const container — whether written as `for (T &v : container)` or `for (const T &v : container)` — calls the non-const `begin()` and triggers a detaching deep copy; `const T &` does not save you. Two correct shapes: declare the container itself `const`, or wrap it with `std::as_const()`:

  ```cpp
  for (const AgentDefinition &def : std::as_const(m_definitions)) {
      ...
  }
  ```

  `std::as_const` comes from `<utility>` (C++17) and works on both the Qt 5 and the Qt 6 route; do not use `qAsConst` (Qt 5-only, deprecated in Qt 6).

- **Under high DPI / scaling ≠ 100%, a `QPixmap`'s physical size is not its logical size**: `width()` / `height()` return physical pixels, and painting or positioning computed from them is offset — **divide by `devicePixelRatio()` to get the logical size**. The Qt 6 route can call `deviceIndependentSize()` directly; the Qt 5 compatibility branch divides by hand.

### 4.6 User-facing strings and logging

- User-visible strings are wrapped in `tr()` and the source string must be English (`check_architecture` rule 3); translations live in `translations/`, with `%1` placeholders filled by `.arg()`.
- Internal literals use `QStringLiteral`; do not wrap things that must not be translated (command lines, JSON keys, URL fragments) in `tr()`.
- Log with `qInfo()` / `qWarning()`, in English, prefixed with `[app]` / `[cmd]` plus the operation and the object id (see `logPrefix` in `AgentsFacade.cpp`). Do not commit `qDebug()`.
- Application-level event logs use the `AWB_DEBUG` / `AWB_INFO` / `AWB_WARNING` / `AWB_CRITICAL` macros from `core/Logging.h`: the level comes from the macro name and the category is fixed to `awb.event` (it lands in the line prefix and is what a future UI log view would filter on). Module-internal logs stay on `qInfo()` / `qWarning()` with a `[module]` prefix. Writing is asynchronous (a background thread drains the queue), so never assume a line reached disk by the time `qInfo()` returns.
- The language split matters: **identifiers, user-facing strings, logs and commit messages are English; only code comments are Chinese** (section 6). Never put Chinese inside `tr()`, and never put it in a log line.
- Never log a URL that carries a token — `redactedUrl` strips both `?token=` and `#token=`.

## 5. QML rules

The layout skeleton, sidebar rules and the component reuse catalogue are `designs.md`'s job. This section covers how the code itself is written.

### 5.1 File structure

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench          // shared components (A*, PageHeader, …)
import AgentWorkbench.App      // C++ singletons (theme, agents, workbench, …)

// Component note: what problem it solves and what its contract is (required, in Chinese)
Item {
    id: page

    // property declarations
    property string filterText: ""

    // complex logic goes into functions
    function recountShown() { ... }

    // children
}
```

- Import order: `QtQuick` → `QtQuick.Controls` → `QtQuick.Layouts` → `AgentWorkbench` → `AgentWorkbench.App`.
- The root `id` is `page` (pages), `control` (controls) or `root` (anything else) — matching the existing code.
- The top of a component file carries a comment explaining what the component is for and what it guarantees (in Chinese); properties, functions and non-obvious children get the same treatment.

### 5.2 The C++ boundary

- QML talks to facades and models only: agent operations through `agents.*`, cross-domain actions through `workbench.*`, notifications through `workbench.notify`, environment state through `environment.*`, theme tokens through `theme.*`.
- QML does not touch files, start processes, or call `Qt.openUrlExternally` — each of those needs a `Q_INVOKABLE` entry point.
- Before calling a C++ property or method from QML, confirm it is callable (4.2). A missing `Q_INVOKABLE` does not fail at load time; it fails silently at the call site.

### 5.3 Visuals and theming

- Use `theme.*` semantic tokens only; **never a literal colour** (`#rrggbb`, `Qt.rgba(0.2, 0.3, …)` are both rejected by the gate). Everything must be readable in both the dark and the light theme.
- Spacing, radii, font sizes and animation durations come from tokens (`theme.spacingM`, `theme.radiusCard`, `theme.durationFast`). Do not invent numbers.
- Reuse first: buttons, text fields, cards, dialogs and status dots always come from the `A*` components in `components/`; never hand-write a bare `Button` with a custom `background`. The catalogue and when to use which is `designs.md` section 3.
- State is never colour alone: pair it with a tooltip or text.

### 5.4 Layout and interaction

- Prefer `ColumnLayout` / `RowLayout` / `GridLayout`. Size children through `Layout.fillWidth` / `Layout.preferredWidth`; do not put a `width` binding on a layout child — assigning `width` does not flow back into the layout, so it "works" while its siblings never reflow (this is where the sidebar-collapse gap came from). To animate, bind `implicitWidth` / `implicitHeight` and attach a `Behavior` to them.
- Slot-like properties (`default property`, named slots) use a layout container such as `RowLayout`, not a bare `Item`.
- Declare a `MouseArea` before the region it must cover; a full-rect `MouseArea` declared later swallows clicks meant for the buttons in front of it (an unclickable close button is the classic symptom).
- Attached `ToolTip`s must set `delay: 300` and `timeout: 10000`; when the host is a delegate, add `Component.onDestruction: ToolTip.hide()` — otherwise the tooltip freezes on screen after the host is destroyed.
- `WebEngineView`'s `LifecycleState` is a scoped enum: write `WebEngineView.LifecycleState.Active`. The bare `WebEngineView.Active` is `undefined` and the assignment silently does nothing.
- Put multi-step logic in a `function` with `let` / `const` locals instead of a one-line binding expression, and watch for binding loops (`onXChanged` assigning back into `x`).

### 5.5 Adding a file

After adding a `.qml` under a module's `qml/`, two lists must be updated (miss one and you get a missing component at runtime, or strings that never reach the translators):

1. the matching area list in `app/CMakeLists.txt` — `_shell_qml`, `_component_qml`, `_agentcatalog_qml`, `_skillcatalog_qml`, `_web_qml`, `_webengine_qml`. The `QT_RESOURCE_ALIAS` set there decides the component URL, `qrc:/qt/qml/AgentWorkbench/<area>/<Name>.qml`;
2. `cmake/AwbTranslations.cmake`, in `AWB_TS_SOURCES`, when the file contains `qsTr()`.

QML resources are never put into a static library (the linker drops qrc initialisers), so the manifests live at the executable level: a module's own `CMakeLists.txt` lists C++ only, never `.qml`.

## 6. Comment rules (Doxygen)

This is the section most often written badly. Comments here are not personal notes: they are the project's **code documentation**. All comments are written in Chinese, as thoroughly as the subject deserves, and **never with sentences that carry no information**.

The division of labour in one sentence: **member functions in headers carry a short plain comment only, and every function implementation in the `.cpp` carries the complete Doxygen comment**; entities with no `.cpp` to go to — signals, enums, member variables, header-only types — are the exception, and their complete comments stay in the header.

### 6.1 General rules

1. **Style**: follow **Doxygen** comment style, but where and in what format is written follows the division of labour of this section — complete Doxygen blocks (`/** ... */`) go above function definitions in the `.cpp` and above signal declarations in headers; a member function in a header carries a single short **plain** comment (`//`) whose content equals a `@brief`.
2. **Language**: Chinese. Technical terms and type/function names stay in English (`Q_INVOKABLE`, facade, role); do not force a translation.
3. **Why this split**: a header is included by many translation units, so complete documentation in the header means every doc edit recompiles every dependent; complete documentation in the `.cpp` keeps a doc change to a single translation unit (details in 6.6). The header therefore keeps only the "what does this do at a glance" sentence.
4. **Keep it in sync**: changing behaviour, parameters, return values or failure conditions means changing the comments in the same commit (both the header and the `.cpp`). A comment that contradicts the code is a defect.
5. **No filler**: do not restate what the code already says. `// set the name`, `// loop over the list`, `// constructor` — none of these. The brief comment states the contract — when to use it, what it guarantees — not a translation of the function name.
6. **No mechanical migration**: do not rewrite files you are not otherwise changing just to unify comment syntax (section 0).

Comments inside a `.cpp` are of two kinds and live in different places:

- **Documentation comments** (Doxygen, `/** ... */`): the function's contract — `@brief`, parameter meaning, return value, side effects, failure conditions. Placed above the definition, **separated from the preceding code by a blank line**.
- **Soft comments** (plain `//`): why this implementation is written this way, what constrains it, what went wrong before. Placed above the code they explain.

Implementation-level explanation appears in the `.cpp` only. Headers carry none of it.

The examples in the following sections are taken from real code in this repository and show the **target style of this standard**. Where existing files have not caught up, apply the migration rule from section 0: bring the class you are editing up to the standard, and do not rewrite the repository.

### 6.2 Headers: short plain comments for member functions

What gets commented in a header, and how:

**Classes / structs** — a `///` paragraph saying what it is, what it owns, and where its boundaries are:

```cpp
/// 按固定间隔探测每个 agent 的 webUrl，把「运行中 / 已停止」的变化广播出去。
///
/// 只上报状态变化（边沿触发），不做进程嗅探；探测细节见 AgentHealthMonitor.cpp。
class AgentHealthMonitor : public QObject
{
    ...
};
```

The **detailed** class description (design trade-offs, how it collaborates with other modules) goes at the top of the `.cpp` implementation file, as a comment block.

**Member functions** — **no complete Doxygen comment** (a documentation change in a header would trigger a full rebuild of every dependent): keep a short **plain** comment (`//`) whose content equals a `@brief`, and let a **`get` / `set` pair share one comment**:

```cpp
public:
    // 启动该 agent 并开始轮询它的会话 URL；已在运行或 command 为空时直接返回，不报错
    Q_INVOKABLE void launch(const QString &id);

    // agent 的显示名
    QString name() const;
    void setName(const QString &name);
```

Everything deeper (per-parameter meaning, failure paths, return value) belongs at the definition in the `.cpp` (6.3), never piled up in the header.

**Signals** — a signal has **no `.cpp` implementation** and is the exception to the split: its complete Doxygen comment, `@brief` and `@param` included, is written at the signal declaration in the header:

```cpp
Q_SIGNALS:
    /**
     * @brief 启动或停止失败时发射，界面据此在卡片上闪红并弹出原因
     * @param id 出错的 agent id
     * @param message 可直接展示给用户的失败原因（英文 `tr()` 源串）
     */
    void launchFailed(const QString &id, const QString &message);
```

**Enums** — one `///` above the type, one trailing `///<` per enumerator saying what it means:

```cpp
/// 列表模型的 role。名字与顺序是对 QML 的契约，改动前先确认所有页面。
enum Roles {
    IdRole = Qt::UserRole + 1,  ///< agent 的 id（agents.json 中的键）
    NameRole,                   ///< 显示名
    RunningRole,                ///< 健康检查判定为运行中
    ...
};
Q_ENUM(Roles)
```

**Member variables** — commented with a trailing `///<`; a group of related members may also share one comment above the group instead of one per line:

```cpp
private:
    QTimer m_timer;                 ///< 探测定时器
    QHash<QString, qint64> m_pids;  ///< 本次会话中由本启动器启动过的进程，键为 agent id
```

**Header-only entities** — structs, templates and inline functions have no `.cpp` definition, so the complete comment (brief + detail + tags) stays in the header:

```cpp
/// 可失败同步操作的结果：跨模块边界不抛异常，失败以 { ok = false, error = ... } 传递。
///
/// Q_GADGET 让 QML 能直接读 Q_INVOKABLE 返回值的 .ok 与 .error。
struct OpResult
{
    ...
};
```

### 6.3 .cpp: a complete Doxygen comment above every implementation

**Every function implementation** gets a complete Doxygen comment above it: `/** ... */` format, **separated from the preceding code by a blank line**, using tags such as `@brief`, `@param`, `@return` and `@sa`, with the text in Chinese:

```cpp

/**
 * @brief 从 agent 的启动输出里挑出会话 URL
 *
 * 带 token 门禁的 harness（dsh 一类）把每进程随机的带 token URL 打到 stdout，
 * 而不是写 token 文件，所以这里扫日志内容而不是读文件。只取第一条指向
 * 同一服务器（协议 + 主机 + 端口相同）的 URL，避免把文档链接误当会话入口。
 *
 * @param output 启动输出（已做 token 脱敏）
 * @param webUrl agent 配置的 web 地址，用于比对服务器
 * @return 找到的会话 URL；没有匹配时返回空字符串
 * @sa finalUrl
 */
QString AgentUrls::sessionUrlFromOutput(const QString &output, const QString &webUrl)
{
    ...
}
```

Rules:

- `@brief` says what the function does in one sentence; the motivation, constraints and past pitfalls go into the body paragraphs below it — do not cram them into the `@brief` line.
- Even a trivial function gets the complete block — pure forwarders and one-line getter implementations included (a single `@brief` line is fine): the documentation tool can then extract something for every implementation, and the style stays uniform.
- Write `@param` whenever there is anything to say: ranges, units, what an empty string or `nullptr` does, ownership, failure semantics. Omit it only when the signature already says everything (e.g. `const QString &id` with no extra convention).
- Write `@return` for what the value means: what an empty string stands for, what `-1` stands for, in what order a container comes back, what is returned on failure. Tautologies like "returns true on success" are the exception.
- `@note` for usage caveats, `@warning` for constraints with consequences, `@sa` (or `@see`) for related functions — a getter/setter pair cross-references each other with `@sa` — and `@deprecated` for the replacement. When there is nothing to say, write no tag — do not fill in empty ones to look complete.
- Helpers in an anonymous namespace and file-level constants exist only in the `.cpp` and have no declaration, so their complete comment goes above the definition as well.

### 6.4 .cpp: soft comments

A soft comment is a plain `//` explaining "why", never "what". The test: if the next line already says it, delete the comment.

```cpp
// QStandardPaths 的测试模式只重定向 App* 位置，不动 HomeLocation，
// 因此单元测试会写到开发者真实的配置目录。测试模式下必须换用被重定向的位置。
if (QStandardPaths::isTestModeEnabled()) {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}
```

What makes this kind of comment valuable is that it records **the reasoning and the cost**:

- why the more intuitive approach does not work (above: why not just use `HomeLocation`);
- what a seemingly redundant check is actually guarding (concurrency, re-entrancy, out-of-range input, non-ASCII paths);
- what happens if you write it the other way — especially bugs that really did happen (e.g. why the card visibility counter cannot read `Item.visible`).

For a long stretch of implementation, separate stages with a blank line and a short `//` so a reader can skim; do not comment every line.

### 6.5 Language and tone

- Chinese, with Chinese punctuation. A sentence-final period is optional, but stay consistent within a file.
- Plain language: no "we", no pleasantries, no "NOTE!!!". To emphasise something, state the consequence.
- Numbers, units and type names stay verbatim: `60 s`, `QHash<QString, qint64>`, `QQuickWebEngineProfile`.
- Do not write "to be optimised" or "temporary for now"; if something is genuinely pending, use `@todo` and state the condition and the alternative.
- No personal names and no external links (they rot). To cite background material, point at a document in this repository, e.g. `docs/research/webengine-embedding.md`.

### 6.6 Why this split works

- **Rebuild cost**: a header is included by many translation units, so complete documentation in the header means every doc revision triggers a full rebuild of all dependents; complete documentation in the `.cpp` recompiles exactly one translation unit. That is why member functions in a header carry no complete Doxygen comment.
- **The exceptions must be complete**: signals, enums, member variables and header-only types have no `.cpp` definition, so their complete comments can only live in the header — otherwise the information appears in no generated documentation at all.
- **The plain comment in a header serves code readers only**: Doxygen does not extract it, so it stays at the one "what does this do at a glance" sentence (equal to the `@brief`), and everything else belongs to the `.cpp`. Where the two overlap, the complete comment in the `.cpp` wins, kept in sync per rule 4 of 6.1.

### 6.7 Anti-patterns

```cpp
// 错：复述代码，零信息
// 设置 agent 的名称
def.setName(name);

// 错：没有结论的注释
// 这里可能有问题，先这样
return m_cache.value(id);

// 错：把实现细节写进头文件，头文件应该只留简略说明
// 先查 QHash，未命中再扫目录，扫的时候跳过 .tmp 后缀……
QVariantMap load(const QString &id);

// 错：头文件的成员函数写完整 Doxygen 注释——文档一改，所有依赖该头的翻译单元全部重编
/**
 * @brief 启动该 agent 并开始轮询它的会话 URL
 * @param id agent 的 id
 */
Q_INVOKABLE void launch(const QString &id);

// 错：.cpp 的函数实现只写普通注释，文档工具什么都提取不到
// 启动 agent 并轮询会话 URL
void AgentsFacade::launch(const QString &id) { ... }

// 错：信号没有 .cpp 实现，头文件里又不写注释——它在任何文档里都不会出现
Q_SIGNALS:
    void launchFailed(const QString &id, const QString &message);
```

## 7. Test rules

How tests are registered, the "cases must be in `private Q_SLOTS:`" rule, and the ban on network access and the real data directory are all in `AGENTS.md` (the "测试" section). Style-level rules:

- One `QObject`-derived class per unit under test; case names say `testXxx` and describe **the behaviour verified**, not the implementation (`testTildeExpansion`, not `testExpand2`).
- Assert with `QCOMPARE` / `QVERIFY2`; give `QVERIFY2` a human-readable reason (`qPrintable(actual)`).
- Verify signals with `QSignalSpy`, including that a signal did *not* fire when it should not.
- Every case prepares and cleans up its own data and does not depend on execution order; when a fixed directory is needed, inject a `QTemporaryDir` via `Paths::setDataRootForTesting()`.
- Where a case covers a non-obvious boundary, add a soft comment on **why that boundary matters** (same style as `tst_envexpander.cpp`).

## 8. Pre-commit checklist

- [ ] `bash scripts/build.sh --test` is green (8 test targets + `check_architecture`).
- [ ] Any added or moved `.qml` is listed in the matching area list in `app/CMakeLists.txt`, and (when it contains `qsTr()`) in `AWB_TS_SOURCES`.
- [ ] Every C++ method/property used from QML is callable (`Q_INVOKABLE` / `WRITE`).
- [ ] New code uses the capitalised Qt macros (`Q_SIGNALS` / `Q_SLOTS` / `Q_EMIT`), every `QObject`-derived class has `Q_OBJECT`, and each signal/slot pair matches literally in parameter types and value form.
- [ ] Single-statement `if` / `for` / `while` bodies have braces; range-iteration over non-const Qt containers goes through `std::as_const()` or a `const`-declared container.
- [ ] New panels/pages were read in both the dark and the light theme, with no literal colours.
- [ ] The classes and functions you touched have comments that follow the section 6 split (brief plain comments in headers, complete `/** ... */` Doxygen blocks in the `.cpp`); no new "restates the code" comments were introduced.
- [ ] No non-ASCII text inside `tr()` / `qsTr()`; comments, identifiers, logs and commit messages each use their prescribed language.
- [ ] No `qDebug()`, no temporary code, no commented-out old implementations left behind.
