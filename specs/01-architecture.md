# AgentWorkbench 架构规格（一）：分层、模块与依赖

> 状态：**设计基线，待实施**（2026-09-26，对应仓库版本 0.3.0 → 0.4.0）
> 读者：在本仓库工作的 AI agent 与人类维护者
> 三份规格的阅读顺序：本文（架构与模块）→ `02-ui-specification.md`（界面与主题）→ `03-migration-plan.md`（分阶段实施计划）
> 权威性：本文中的**模块名、CMake 目标名、目录路径、公开类型名、配置键名、信号/方法名**都是契约，实施时必须一致；私有函数、内部数据结构、实现顺序可以自由决定。确有必要改契约时，**先改本文并写清理由，再改代码**，不要在代码里留下与本文不一致的实现。

---

## 0. 术语表（统一语言）

| 术语 | 含义 |
| --- | --- |
| Workbench（工作台） | 整个应用。可扩展的宿主，Agent 启动器只是它当前的一个功能页。 |
| Shell（外壳） | 窗口骨架与通用 UI 设施：侧边栏、工作区宿主、状态栏、通知、剪贴板。 |
| Page（页面） | 工作区里可切换的一个顶层视图（Agents / Web / Skills / Settings）。 |
| Feature（功能） | 一个领域模块对外提供的一组能力（可能包含多个页面、模型、服务）。 |
| Surface（表面） | Agent WebUI 的承载方式。`embedded` = 内嵌 WebEngine 视图；`external` = 交给系统浏览器。 |
| Facade（门面） | 面向 QML 的控制器对象，聚合该模块内部的多个服务，是 QML 能看到的唯一入口。 |
| Definition / State | 持久化定义（写进配置文件）与运行期状态（只存在于内存）的分离。 |
| Token（令牌） | 主题里的语义化取值键，如 `surfaceBg`、`radiusCard`。QML 只允许用令牌，不允许写字面颜色。 |
| Intent（意图） | 跨领域的一次请求，例如「为 agent X 打开 Web 表面」。由 `workbench` 层实现，避免领域模块互相依赖。 |
| Adapter（适配器） | 领域模块中依赖重型框架（Qt WebEngine）的实现层，与领域模型分离。 |

---

## 1. 目标与非目标

### 1.1 目标

1. 项目从「Agent 启动器」升级为「Agent 工作台」：保留全部启动器能力，新增侧边栏 + 工作区外壳、内嵌 Web 标签页、Skill 浏览、可配置主题。
2. 把 0.3.0 的扁平 `src/` 拆成有明确边界的模块，每个模块可单独编译、单独测试。
3. 用**依赖方向**而不是文档纪律来约束模块：领域模块之间不许互相依赖，跨领域行为只在应用层发生。
4. 为插件化留出真实的缝：内置功能和未来插件走**同一条注册路径**，插件接口是独立目标，可被外部仓库链接。
5. 所有面向用户的取值（颜色、尺寸、字体、行为开关）都由配置文件驱动，代码里不写字面量。

### 1.2 非目标（明确不做，实施时不要顺手做）

| 不做 | 理由 |
| --- | --- |
| agent 定义的配置迁移 | 保持既有设计：内置项每次启动由 `config/default_agents.json` 重新生成，用户自建项原样保留。代码里没有、也不要加迁移逻辑。 |
| 插件沙箱 | 插件在宿主进程内运行（同 Chromium 内嵌模型的信任级别）。文档里必须写明这一安全前提，由用户在设置页显式启用。 |
| v1 动态加载第三方插件 | 只落**接口 + 宿主 + 示例插件**，默认关闭。动态加载的开关在设置页，且必须是实验性标记。 |
| XML 主题格式 | 只做 JSON。同一份 schema 用两种格式解析会让校验面翻倍，没有对应的用户收益。 |
| 进程嗅探式健康检查 | 保持 HTTP 探测语义（任何 HTTP 响应 = 运行中）。`forceStop` 基于端口的 PID 查找是显式用户操作，保留。 |
| 引入日志库（spdlog 等） | 沿用自研的滚动文件 `Logger`（已处理非 ASCII 路径）。 |
| Linux/macOS 功能适配 | 代码保持可移植（不写死路径分隔符），但验收只在 Windows x64 + MSVC 上做。 |

---

## 2. 现状盘点（0.3.0）

| 文件 | 行数 | 当前职责 | 问题 |
| --- | --- | --- | --- |
| `src/AgentLauncher.cpp/.h` | 1479 / 160 | 启动/停止/强制停止、HTTP 健康检查、版本检查、安装、更新、一次性 setup、Python/Node 探测、agent CRUD、配置落盘、端口→PID 查找 | 一个类 8 种职责；无法在不启动 Qt Quick 的情况下测任何一块；`AgentConfig` 与 `AgentModel` 被直接操作 |
| `src/AgentConfig.cpp/.h` | 314 / 109 | agents.json 读写、内置默认同步、removed 列表、调色板分配、图标解析、环境变量展开、`userDataDir()` | 持久化、展示逻辑（调色板）、路径工具（env 展开）混在一起；`userDataDir()` 是全局静态，测试只能靠 `QStandardPaths` 测试模式 |
| `src/AgentModel.cpp/.h` | 250 / 72 | 角色映射 + 9 个 setter | 定义与运行状态混在同一个结构体里 |
| `src/Logger.cpp/.h` | 180 / 73 | 滚动文件日志 | 位置正确，但被当作全局单例直接调用 |
| `qml/main.qml` 等 4 个文件 | 1846 | 首页卡片网格、设置页、编辑页、若干弹窗 | 15 个硬编码色值、170+ 处使用；没有导航抽象；设置页同时管 agent CRUD 与应用设置 |
| `CMakeLists.txt` | 128 | 单目标 + 测试目标直接编译源码 | 源文件清单写三处（APP_SOURCES、测试、lupdate），加模块要改 4 处 |
| `src/main.cpp` | 99 | 组装 + 引擎启动 | 组装逻辑与 WebEngine 初始化顺序要求冲突（`QtWebEngineQuick::initialize()` 必须在 `QGuiApplication` 之前） |

已有资产必须保留并继续被测试覆盖：内置默认同步语义、removed 列表、调色板自动分配、图标解析（qrc/http/file/回退）、`formatCommandLine`/`clampOutput`/日志滚动、setup 状态机、安装命令日志。

---

## 3. 分层与模块清单

### 3.1 分层图

```
L4  可执行            ┌───────────────────────────────────────────────┐
    （资源 + 组装）     │  AgentWorkbench (exe)：main + QML 模块 + 资源  │
                       └───────────────────────┬───────────────────────┘
                                               │
L3  应用层             ┌───────────────────────▼───────────────────────┐
    （组合根 + 意图）   │  awb_workbench：跨域意图、内置页面注册、环境探测 │
                       └──┬──────────┬──────────┬──────────┬───────────┘
                          │          │          │          │
L2  领域层 / UI 框架   ┌──▼───┐ ┌────▼───┐ ┌────▼───┐ ┌────▼─────┐ ┌──────────────┐
                       │agents│ │ skills │ │  web   │ │webengine │ │ awb_shell    │
                       │      │ │        │ │(+外部) │ │(适配器)  │ │(侧边栏/宿主) │
                       └──┬───┘ └───┬────┘ └───┬────┘ └────┬─────┘ └──────┬───────┘
                          │         │          │           │              │
L1  表现基础           ┌──▼─────────▼──────────▼───────────▼──────────────▼───────┐
                       │  awb_theme：主题文件加载、语义令牌、热重载               │
                       └──────────────────────────┬───────────────────────────────┘
                                                  │
L0  基础层             ┌──────────────────────────▼───────────────────────────────┐
    （无 UI 依赖）      │  awb_core：路径 / 日志 / JSON 存储 / 进程 / HTTP / 插件宿主│
                       └──────────────────────────────────────────────────────────┘
                       ┌──────────────────────────────────────────────────────────┐
                       │  awb_plugin_api：插件 ABI 头文件（只依赖 Qt Core）        │
                       └──────────────────────────────────────────────────────────┘
```

### 3.2 类别划分（回答「哪些是核心、哪些是领域」）

| 类别 | 模块 | 判据 |
| --- | --- | --- |
| **核心（基础设施）** | `awb_core`、`awb_theme` | 与「Agent」「Skill」这些业务概念无关；换成另一个产品也能原样复用。 |
| **核心（UI 框架）** | `awb_shell` | 只提供窗口骨架与通用交互设施，不含任何业务概念。领域模块**不许**依赖它。 |
| **领域** | `awb_agents`、`awb_skills`、`awb_web` | 每个对应一个业务能力，拥有自己的配置、模型、页面。彼此**不许**互相依赖。 |
| **领域适配** | `awb_web_webengine` | 依赖重型框架（Qt WebEngine）的具体实现层；关掉它程序仍能运行。 |
| **应用** | `awb_workbench`、`AgentWorkbench`(exe) | 组装、跨域编排、进程入口。除了它们，没有模块可以依赖它们。 |
| **契约** | `awb_plugin_api` | 头文件接口目标，是插件的 ABI 边界。 |

### 3.3 模块清单

| CMake 目标 | 层级 | 源码目录 | 一句话职责 | 公开契约（对外类型） |
| --- | --- | --- | --- | --- |
| `awb_core` | L0 | `src/core/` | 与业务无关的基础设施 | `Paths`、`JsonStore`、`Settings`、`OpResult`、`ProcessRunner`、`ScriptRunner`、`HttpProbe`、`EnvExpander`、`IconResolver`、`TextUtils`、`Logging`、`LegacyImport`、`PluginHost` |
| `awb_plugin_api` | L0 | `src/plugin_api/` | 插件 ABI（只有头文件） | `awb::plugin::ApiVersion`、`PluginManifest`、`PageDescriptor`、`Services`、`Entry` |
| `awb_theme` | L1 | `src/theme/` | 主题文件 → 语义令牌 → QML 属性 | `Theme`、`ThemeFile`、`ThemeLoader`、`ThemeRegistry` |
| `awb_agents` | L2 | `src/agents/` | Agent 的定义、持久化、进程、健康检查 | `AgentDefinition`、`AgentState`、`AgentRepository`、`AgentModel`、`AgentRuntime`、`AgentScripts`、`AgentHealthMonitor`、`AgentStateStore`、`AgentsFacade` |
| `awb_skills` | L2 | `src/skills/` | Skill 目录扫描与展示模型 | `SkillRoot`、`SkillDefinition`、`SkillFrontmatter`、`SkillScanner`、`SkillModel`、`SkillsFacade` |
| `awb_web` | L2 | `src/web/` | Web 标签页模型与表面注册 | `WebTab`、`WebTabsModel`、`WebSurfaceRegistry`、`WebTabsFacade` |
| `awb_web_webengine` | L2 | `src/web/webengine/` | WebEngine 表面实现 + 每 agent profile | `WebEngineSurfaceProvider`、`WebEngineProfileStore`、`WebEngineSurface.qml` |
| `awb_shell` | L2 | `src/shell/` | 导航注册表、窗口骨架、通知、剪贴板 | `PageDescriptor`、`NavigationModel`、`ShellController`、`UiServices`、`Notifications` |
| `awb_workbench` | L3 | `src/workbench/` | 组合根、跨域意图、内置页面注册、环境探测 | `WorkbenchContext`、`BuiltinPages`、`EnvironmentService` |
| `AgentWorkbench` | L4 | `app/` | 可执行、QML 模块、资源、翻译、打包 | —（可执行文件目标） |
| `AgentWorkbenchTests` 等 | — | `tests/<module>/` | 每模块一个测试可执行 | — |

**模块数量上限**：新增能力时，只允许「新增一个领域模块（`src/<feature>/`）」或「加入既有模块」。不要新建中间层、不要建 `utils` 之类的杂物模块。

---

## 4. 各模块职责细则

### 4.1 `awb_core`（L0）

职责：与 AgentWorkbench 的业务概念无关的一切基础设施。链接 `Qt6::Core`、`Qt6::Network`（HTTP 探测需要）；**不链接** `Qt6::Gui/Quick/Qml`。

| 类型 | 职责 |
| --- | --- |
| `core::Paths` | 数据目录的唯一来源。`dataRoot()` 默认 `~/.AgentWorkbench`；`themesDir()`、`pluginsDir()`、`logsDir()`、`webProfilesDir()`、`downloadsDir()`。支持 `setDataRootForTesting(dir)` 覆盖（供测试注入 `QTemporaryDir`）；测试模式下回退到 `QStandardPaths` 测试位置，避免改写开发者真实数据。支持用 `QFile`/`QDir` 打开（非 ASCII 用户名路径必须走 Qt，不许用窄字符 `std::string`）。 |
| `core::JsonStore` | JSON 文件读写：原子写（`QSaveFile`）、缩进格式、读失败返回空对象并记日志。所有配置文件的落盘都经过它，保证换行/编码一致。 |
| `core::Settings` | `settings.json` 的类型化访问层（见 §7.2）。提供 `themeId()`、`windowTitle()`、`skillsRoots()`、`webOptions()`、`launcherOptions()`、`loggingOptions()` 等访问器 + `valueChanged(key)` 信号 + `save()`。禁止在别处直接读 `settings.json`。 |
| `core::OpResult` | `{ bool ok; QString error; }`。跨模块边界**不抛异常**，可失败操作返回它。 |
| `core::ProcessRunner` | 启动/运行外部命令的机制层：`startDetached(program, args)` → PID（用 `QStandardPaths::findExecutable` 先解析裸程序名，应用 PATHEXT，让 `qwen.cmd` 这类 npm 垫片能找到）；`run(program, args, timeout)` → `{exitCode, stdout, stderr}`；`killTree(pid)`（Windows 下 `taskkill /F /T /PID`）。 |
| `core::ScriptRunner` | 一次性命令的流式执行：`run(id, program, args)`，边跑边发 `outputChunk(id, text)`，结束发 `finished(id, ok, exitCode, message)`。负责输出行合并、`clampOutput` 截断、epoch 机制（避免上一次运行的收尾逻辑清掉新一次运行的状态）。 |
| `core::HttpProbe` | 异步 HTTP 探测：`probe(url)` → `finished(url, reachable)`。语义固定：**任何 HTTP 响应（含 4xx/5xx）= 可达；连接被拒绝/超时 = 不可达**。附带 `portFromUrl(QUrl)`。 |
| `core::EnvExpander` | `%VAR%`（Windows 形式）与 `~` 展开。 |
| `core::IconResolver` | 图标字符串解析：`qrc:/`、`http(s)://`、`file://` 原样通过；存在的本地文件转 `file:///` 绝对路径；其它（含空）回退到调用方传入的 fallback（默认 `qrc:/icons/default.svg`）。fallback 由调用方给出，core 不写死应用资源路径。 |
| `core::TextUtils` | `extractVersion(output)`、`formatCommandLine(program, args)`、`clampOutput(text, limit)`。 |
| `core::Logging` | 现 `Logger` 改名迁入。滚动文件日志（5 MB × 3 个文件，参数来自 settings）、`install()/uninstall()/logFilePath()/formatCommandLine()/clampOutput()`。增加分类前缀（`awb.agents`、`awb.web`、`awb.skills`、`awb.theme`），便于按模块过滤。 |
| `core::LegacyImport` | 一次性接管旧数据目录（见 §7.3）。 |
| `core::PluginHost` | 插件发现与加载（S7 阶段）。扫描 `pluginsDir()/*/plugin.json`，校验 `apiVersion`，`QLibrary` 加载 `entry` 指向的动态库，调用两个 C 符号。加载失败只记日志，绝不让程序起不来。 |

### 4.2 `awb_theme`（L1）

职责：把主题 JSON 变成 QML 可绑定的语义令牌。链接 `Qt6::Core`、`Qt6::Gui`（`QColor`）；**不链接** Qt Quick。

| 类型 | 职责 |
| --- | --- |
| `theme::ThemeFile` | 值类型：`id`、`name`、`variant`（`dark`/`light`）、`colors: QHash<QString,QColor>`、`metrics: QHash<QString,double>`、`fonts`、`agentPalette`。 |
| `theme::ThemeLoader` | 解析 + 校验一份主题 JSON。规则：未知键 → 记警告并忽略；缺失 token → 回退到**同 variant 的内置基准主题**取值；颜色非法 → 记警告并用基准值；`id` 缺失或与文件名不符 → 整个文件跳过并记警告。 |
| `theme::ThemeRegistry` | 汇总可用主题：内置（`:/themes/*.json`）+ 用户（`<dataRoot>/themes/*.json`，同名覆盖内置）。用 `QFileSystemWatcher` 监听目录与当前文件，改动后重载并发 `changed()`。 |
| `theme::Theme` | QML 全局对象。把令牌暴露为**具名属性**（`theme.surfaceBg`、`theme.radiusCard`、`theme.fontSizeBody` ……完整清单见 `02-ui-specification.md` §9），另有 `color(name)` 动态取色、`alpha(color, a)`、`hover(color)`、`pressed(color)` 辅助函数、`variant`、`availableThemes`、`themeId`、`applyTheme(id)`、`changed()` 信号。所有属性可 NOTIFY，切换主题时 QML 自动重绑。 |

主题文件格式、全部令牌名与默认值、内置两套主题的取值表，由 `02-ui-specification.md` §9 定义，本文不重复。

### 4.3 `awb_agents`（L2 领域）

职责：agent 的定义、持久化、进程生命周期、健康检查、一次性命令。链接 `awb_core`、`awb_theme`（取自动配色板）；**不链接** Qt Quick，也不依赖其它领域模块。

| 类型 | 职责 |
| --- | --- |
| `agents::AgentDefinition` | **持久化**字段的值类型：`id,name,command,webUrl,configDir,icon,color,cardColor,installCommand,updateCommand,versionCommand,setupCommand,tokenFile`。 |
| `agents::AgentState` | **运行期**字段的值类型（永不落盘）：`running, launching, stopping, installed, installing, version, setupDone, setupping, checkingVersion, consoleOutput`。 |
| `agents::AgentRepository` | agents.json 的读写与内置同步。构造时注入数据根目录。`load()` 实现既有语义：内置项按 `config/default_agents.json` 整体覆盖、被删内置项按 `removed` 列表跳过、用户自建项按原顺序排在内置项之后、为空颜色分配调色板（**调色板来自当前主题的 `agentPalette`**）、与内置完全一致时逐字节写入内置文件。`save()`、`removedIds()`、`restoreDefaults()`、`isDefaultAgent(id)`、`slugFromName(name)`、`configFilePath()`、`loadDefaults()`。 |
| `agents::AgentModel` | QAbstractListModel 适配器：合并 `QList<AgentDefinition>` + `QHash<QString,AgentState>`，角色的**名字与顺序保持 0.3.0 不变**（这样卡片 QML 可以近乎原样复用）。只做映射与 `dataChanged`，无业务逻辑。 |
| `agents::AgentRuntime` | agent 自身的长驻进程：`launch(def, tokenValue)` 走 `ProcessRunner::startDetached`（`cmd /c` 跑命令、无可见窗口、展开 configDir）、记录 PID（内存内、会话级）、`stop(id)`、`forceStop(id)`、`hasLaunchedAgents()`、`stopAll()`。`tokenFile` 存在时把内容作为环境变量 `QWEN_SERVER_TOKEN` 传给子进程。 |
| `agents::AgentScripts` | 一次性命令（install / update / version / setup）；每条命令经 `core::ScriptRunner` 执行，输出经 `setConsoleOutput` 送到模型；install/update 完成发 `installFinished(id, ok, message)`；version 解析后发 `versionResolved(id, version)`；setup 成功写 `AgentStateStore`。 |
| `agents::AgentHealthMonitor` | 按 settings 的间隔（默认 3000 ms）对所有有 `webUrl` 的 agent 做 HTTP 探测，发 `runningChanged(id, bool)`。 |
| `agents::AgentStateStore` | `agent_state.json`（`setupDone` 记录），原子写，加载一次。 |
| `agents::AgentUrls` | 打开用的 URL 解析：`webUrl` + `tokenFile` → 追加 `#token=<value>` 的最终 URL（内嵌视图和外部浏览器共用同一份逻辑）。 |
| `agents::AgentsFacade` | QML 门面，聚合以上所有。**保留 0.3.0 的 Q_INVOKABLE 与信号名**（见 `03-migration-plan.md` §4 的 API 映射表），其中 `openWeb(id)` 不再是门面方法——它变成 `workbench` 层的意图 `workbench.openWeb(id)`。 |

### 4.4 `awb_skills`（L2 领域）

职责：发现并展示本机的 skill。链接 `awb_core`、`awb_theme`；不依赖其它领域模块。

| 类型 | 职责 |
| --- | --- |
| `skills::SkillRoot` | 一个扫描根：`id`、`label`、`path`、`kind`（`agents` / `claude` / `codex` / `plugin` / `project` / `custom`）、`enabled`、`recursive`、`dedupeScope`。默认根清单见 `02-ui-specification.md` §7。 |
| `skills::SkillDefinition` | 一个 skill：`name`（frontmatter `name`，缺失时用目录名）、`description`、`skillFilePath`、`dirPath`、`rootId`、`rootLabel`、`kind`、`pluginId`、`pluginVersion`、`lastModified`、`sizeBytes`、`extras`（frontmatter 其余键）。 |
| `skills::SkillFrontmatter` | 极小的 YAML 子集解析器：只处理 `SKILL.md` 开头的 `---` 块；支持 `key: value`、双/单引号、`>`/`|` 折叠标量与多行缩进续行；不支持嵌套结构（`metadata:` 下属的标量键拍平成 `metadata.key`）。带 BOM/CRLF 容忍。这是**必须单元测试**的一块。 |
| `skills::SkillScanner` | 遍历根目录：任一含 `SKILL.md` 的目录算一个 skill，不再深入该目录；插件缓存按「同一 marketplace + 插件」去重，只保留最高版本；单个根无权限/不存在 → 记警告并继续，绝不让整次扫描失败。`refresh()` 立即返回，完成后发 `scanFinished(Stats)`（当前实现可以同步，但接口按异步设计，以后移到工作线程不改调用方）。 |
| `skills::SkillModel` | skill 列表模型 + 过滤（名称/描述/路径）+ 来源分面 + 排序（名称/最近修改/来源）。 |
| `skills::SkillsFacade` | QML 门面：`refresh()`、`roots()`、`setRootEnabled(id, bool)`、`copyPath(id)`（返回 `OpResult`，由 shell 弹 toast）、`openFolder(id)`、`revealSkillFile(id)`、`stats()`。 |

### 4.5 `awb_web`（L2 领域）

职责：Web 标签页的模型、策略、表面注册。链接 `awb_core`、`awb_theme`、`Qt6::Gui`（`QDesktopServices` 打开外部浏览器）。不依赖 `awb_agents`。

| 类型 | 职责 |
| --- | --- |
| `web::WebTab` | 一个标签页的 QObject：`id`、`agentId`、`url`、`title`、`iconSource`、`color`、`surfaceKind`、`state`（`loading`/`ready`/`offline`/`crashed`/`error`/`released`）、`loadProgress`、`lastError`、`zoom`。属性均可 NOTIFY。 |
| `web::WebTabsModel` | 标签页列表模型；额外暴露 `activeTab`、`activeIndex`、`tabById(id)`、`tabForAgent(agentId)`；桥接表面的状态回写。 |
| `web::WebSurfaceRegistry` | `kind → QML 组件 URL` 的注册表。`embedded` 由 `awb_web_webengine` 注册，`external` 由本模块注册（永远存在）。 |
| `web::WebTabsFacade` | QML 门面：`openTab({agentId,url,title,icon,color})`（同 agent 已存在则激活）、`closeTab(id)`、`activateTab(id)`、`reloadTab(id)`、`openExternal(id)`、`reopen(id)`、`tabForAgent(...)`、`surfaceUrl(kind)`、`setTabState(...)`、`markOfflineForAgent(agentId)`。策略来自 `Settings::webOptions()`：`freezeInactiveTabs`、`maxLiveTabs`、`downloadDir`、`chromiumFlags`。 |
| `web::WebProfilePaths` | 每个 agent 一个持久 profile 目录：`<dataRoot>/webprofiles/<agentId>`（Cookies + localStorage 落盘）。**必须**一 agent 一 profile：Chromium 的 cookie 按 host 索引、**忽略端口**，共用 profile 会导致 `127.0.0.1:58627` 与 `127.0.0.1:4096` 互相污染（详见 `docs/research/webengine-embedding.md` §3.1）。 |

### 4.6 `awb_web_webengine`（L2 领域适配）

职责：把 Qt WebEngine 接到 `awb_web` 的表面上，并提供 profile 管理。链接 `Qt6::WebEngineQuick`、`Qt6::Quick`。构建开关 `AWB_ENABLE_WEBENGINE`（默认 ON，需要 MSVC）。

| 类型 | 职责 |
| --- | --- |
| `WebEngineSurfaceProvider` | 向 `WebSurfaceRegistry` 注册 `kind = "embedded"`，指向 `qrc:/qml/web/WebEngineSurface.qml`；提供 `createProfile(agentId)`。 |
| `WebEngineProfileStore` | 每 agent 一个 `QWebEngineProfile`（持久化 `storageName = "awb-" + agentId`，storagePath 为 `WebProfilePaths` 的目录），进程内缓存，应用退出时释放。 |
| `qml/web/WebEngineSurface.qml` | 视图本体，必须处理：`newWindowRequested`（loopback 同源 → 新标签页；否则 `QDesktopServices` 打开）、`downloadRequested`（落到 `downloadDir` + 通知）、`fullScreenRequested`、`renderProcessTerminated`（→ `crashed` 状态）、`loadFinished(ok=false)`（→ `error` 状态）、`zoomFactor`（Ctrl±/Ctrl+0）、`lifeCycleState`（失活冻结）。 |

关闭 WebEngine 时（`AWB_ENABLE_WEBENGINE=OFF` 或用户把 `web.surface` 设为 `external`）：`external` 表面接管，`openTab` 直接调用系统浏览器并记一条日志，Web 页退化为「运行中的 agent 列表 + 在浏览器打开」（见 `02-ui-specification.md` §6.7）。

### 4.7 `awb_shell`（L2，UI 框架）

职责：窗口骨架与通用交互设施。链接 `awb_core`、`awb_theme`、Qt Quick/Controls。

| 类型 | 职责 |
| --- | --- |
| `shell::PageDescriptor` | 页面描述：`id`、`title`（英文源串）、`iconSource`、`source`（QML URL）、`section`（`main` / `extensions` / `system`）、`order`、`badgeText`、`enabled`。 |
| `shell::NavigationModel` | 页面注册表 + 列表模型。`registerPage(PageDescriptor)`（id 重复则拒绝并记警告）、`unregisterPage(id)`、`page(id)`、`setBadge(id, text)`、`setCurrentPage(id)`、`currentPageId`。侧边栏与工作区都绑它。 |
| `shell::ShellController` | 窗口级状态：侧边栏折叠/宽度、窗口尺寸、上次页面、全屏表面，全部读写 `Settings`。 |
| `shell::UiServices` | 剪贴板（`copyText(text)` → `OpResult`）、打开外部 URL、在资源管理器里定位文件、打开文件夹。 |
| `shell::Notifications` | 统一的非阻塞提示（toast）队列：`notify(level, title, text)`，level ∈ `info/success/warning/error`；自动消失时长按级别；最多同屏 3 条。 |
| `qml/` | `MainWindow.qml`、`Sidebar.qml`、`Workspace.qml`、`StatusBar.qml`、`Toasts.qml`、`PageHeader.qml`、`SettingsPage.qml` + 组件集（见 `02-ui-specification.md` §10）。 |

`awb_shell` 不认识 agent、skill、web 中的任何一个概念。它只知道「有一批页面，注册进来的」和「要显示一条通知」。

### 4.8 `awb_workbench`（L3 应用层）

职责：组合根 + 跨领域意图 + 环境探测。这是唯一允许同时依赖全部领域模块的地方。

| 类型 | 职责 |
| --- | --- |
| `workbench::WorkbenchContext` | QML 全局 `workbench`。导航意图：`showPage(id)`、`currentPageId`；跨域意图：`openWeb(agentId)`（查 agent → 取 `AgentUrls` 的最终 URL → 让 `WebTabsFacade` 开标签）、`closeWeb(agentId)`、`reloadWeb(agentId)`；通用：`copyText(text)`、`notify(...)`、`openExternalUrl(url)`、`openFolder(path)`、`openConfigDir(agentId)`、`quit()`。 |
| `workbench::BuiltinPages` | 注册内置页面（Agents / Web / Skills / Settings），顺序与元数据见 `02-ui-specification.md` §5。同时把「agent 停止 → 标签页转 offline」「agent 删除 → 关闭其标签页」这两条跨域规则接到一起。 |
| `workbench::EnvironmentService` | Python / Node 探测（`python --version`、`node --version`，经 `cmd /c`），Q_PROPERTY：`pythonVersion`、`pythonInstalled`、`nodeVersion`、`nodeInstalled`、`detecting`、`refresh()`。QML 全局名 `environment`，显示在状态栏。 |

### 4.9 `AgentWorkbench`（L4 可执行，`app/`）

职责：进程入口 + 资源 + QML 模块。**不含业务逻辑**，只做组装。

`app/main.cpp` 的顺序是硬约束：

```cpp
// 1) 日志最早安装（任何后续失败都要有日志）
core::Logging::install();
// 2) WebEngine 必须在 QGuiApplication 之前初始化（Qt 文档要求），
//    且要先按用户设置注入 GPU 回退开关
qputenv("QTWEBENGINE_CHROMIUM_FLAGS", settings.webOptions().chromiumFlags);
#if AWB_ENABLE_WEBENGINE
QtWebEngineQuick::initialize();
#endif
QGuiApplication app(argc, argv);
// 3) 组装：core → theme → 各领域 → shell → workbench（依赖方向从上到下）
// 4) 注册 QML 全局（见 §8.2），加载 QML 模块，起事件循环
```

`app/` 同时是 QML 模块 `AgentWorkbench` 的宿主（`qt_add_qml_module`），所有模块的 `.qml` 在这里以资源别名编进可执行文件——**静态库里不放资源**，避免静态库的 qrc 初始化器被链接器丢掉这一类隐蔽故障。

---

## 5. 依赖规则（硬性）

### 5.1 依赖矩阵（行 = 允许依赖者）

| ↓依赖 / →被依赖 | core | plugin_api | theme | shell | agents | skills | web | web·engine | workbench |
| --- | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| **core** | — | — | — | — | — | — | — | — | — |
| **plugin_api** | — | — | — | — | — | — | — | — | — |
| **theme** | ✅ | — | — | — | — | — | — | — | — |
| **shell** | ✅ | — | ✅ | — | — | — | — | — | — |
| **agents** | ✅ | — | ✅ | ❌ | — | ❌ | ❌ | ❌ | ❌ |
| **skills** | ✅ | — | ✅ | ❌ | ❌ | — | ❌ | ❌ | ❌ |
| **web** | ✅ | — | ✅ | ❌ | ❌ | ❌ | — | ❌ | ❌ |
| **web·engine** | ✅ | — | ✅ | ❌ | ❌ | ❌ | ✅ | — | ❌ |
| **workbench** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **app(exe)** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **plugin（外部）** | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌（只经 `Services` 接口） |

### 5.2 规则条文

1. 依赖方向永远自上而下：`app → workbench → {shell, 领域模块} → theme → core`。反向依赖一律禁止。
2. **领域模块之间零依赖**。需要跨域时，把编排写进 `awb_workbench`，或给领域模块加一个只接收**值参数**的方法（例如 `WebTabsFacade::openTab({agentId,url,title,icon})` 而不是 `openTab(AgentDefinition)`）。
3. 领域模块不得依赖 `awb_shell`。领域模块的页面 QML **可以**使用 shell 提供的视觉组件（同一个 QML 模块 `AgentWorkbench` 内的 `components/`），但 C++ 侧不许链接 shell。
4. `awb_core` 只依赖 Qt Core + Network；`awb_theme` 只依赖 Qt Core + Gui。两者都不许出现 Qt Quick/Qml 的头文件。
5. 除 `awb_web_webengine` 外，任何模块不许链接 Qt WebEngine。
6. 只有 `awb_workbench` 与 `app` 可以 include `<module>/...` 之外的多个领域的头文件；领域模块的公开头文件只许 include `core/`、`theme/` 和本模块自己的头。
7. 单例只允许出现在一个地方：QML 全局门面（`theme`、`workbench`、`nav`、`ui`、`agents`、`skills`、`web`、`environment`）。其余对象一律显式构造、显式注入（构造函数参数或 setter），不许写 `Xxx::instance()`。
8. 数据目录、当前设置等「环境」必须通过构造注入（`AgentRepository(dataRoot)`），禁止在模块内部直接调用 `Paths::dataRoot()` 之外的全局可变状态。
9. QML 只与门面对象和模型交互：不许直接读写文件、不许直接调 `Qt.openUrlExternally`（走 `workbench`/`ui`）、不许自己保存数据。

### 5.3 强制执行

新建 `scripts/check-architecture.sh`（纯 shell + grep，退出码非 0 即失败），至少检查：

| 检查 | 规则 |
| --- | --- |
| 反向 include | `src/{agents,skills,web}/**/*.{h,cpp}` 中不得出现 `#include "shell/`、`#include "workbench/`，也不得出现另外两个领域模块的目录名 |
| 视觉字面量 | `src/**/qml/*.qml` 中不得出现 `#rrggbb` / `#rgb` 字面颜色（`"transparent"`、`Qt.rgba(theme.…)` 允许） |
| i18n | `tr("…")` / `qsTr("…")` 的参数不得含非 ASCII 字符 |
| core 纯净 | `src/core/` 与 `src/theme/` 中不得出现 `QtQuick`、`Qt6::Quick`、`QtWebEngine` |

把这个脚本挂到 `ctest`（一个 `check_architecture` 测试）并让 `scripts/build.sh --test` 跑到它。违反规则的构建必须失败，而不是靠人记住。

### 5.4 需要跨域时怎么做（决策流程）

1. 只是**值**的传递？→ 给领域模块加一个值参数方法，不需要新依赖。
2. 需要读另一个领域的**状态**？→ 在 `workbench` 里写意图方法，由它取值后调两个领域。
3. 两个领域需要**双向**协作？→ 说明拆分错了边界，回到本文修改模块划分，而不是加依赖。

---

## 6. 代码与命名规范

- 命名空间：`awb` 顶层，模块子命名空间 `awb::core`、`awb::theme`、`awb::agents`、`awb::skills`、`awb::web`、`awb::shell`、`awb::workbench`、`awb::plugin`。
- 头文件包含：每个模块的公开 include 根是 `src/`，因此写 `#include "agents/AgentRepository.h"`。这样一眼能看出依赖，也能被 §5.3 的脚本检查。
- 文件命名：一个公开类型一个文件，`PascalCase.h/.cpp`；QML 文件 `PascalCase.qml`；QML 放在所属模块的 `qml/`（子模块用 `qml/<area>/`）。
- QML 命名：页面 `XxxPage.qml`，对话框 `XxxDialog.qml`，可复用组件 `Xxx.qml` 放 `src/shell/qml/components/`。
- 注释与日志一律英文；面向用户的字符串一律英文源串（`tr()` / `qsTr()`），中文翻译只出现在 `translations/*.ts`。
- 信号用过去式/状态式命名（`runningChanged`、`scanFinished`），槽用祈使式（`launch`、`refresh`）。
- 跨模块边界不抛异常；同步可失败操作返回 `core::OpResult`，异步失败发信号 + 记日志。
- 面向 QML 的 API 一律「接口按异步设计」：`refresh()` 立即返回 + `xxxFinished` 信号，内部可以先同步实现。这样以后把扫描/探测挪到工作线程不需要改 QML。
- 单文件超过约 400 行就该考虑拆分；`AgentLauncher.cpp` 的 1479 行是反面教材。
- 头文件里不留实现（模板除外）；PIMPL 只在确实要藏 Qt 依赖时使用，不要为了「看起来专业」而加。

---

## 7. 数据与配置文件

### 7.1 文件清单

| 路径 | 归属模块 | 内容 | 读失败时的行为 |
| --- | --- | --- | --- |
| `<dataRoot>/agents.json` | `agents::AgentRepository` | 用户自建 agent、`removed` 列表（内置项定义不来自这里） | 空对象 + 记日志 + 用内置默认继续 |
| `<dataRoot>/agent_state.json` | `agents::AgentStateStore` | `setupDone` 记录 | 视为全部未做 setup |
| `<dataRoot>/settings.json` | `core::Settings` | 应用设置（见 §7.2） | 全部取默认值 + 记日志 |
| `<dataRoot>/themes/*.json` | `theme::ThemeRegistry` | 用户主题；同 `id` 覆盖内置 | 跳过该文件 + 记警告 |
| `<dataRoot>/plugins/*/plugin.json` | `core::PluginHost` | 插件清单（S7） | 跳过该插件 + 记警告 |
| `<dataRoot>/log/agentworkbench.log` | `core::Logging` | 滚动日志（`agentworkbench.log.1/.2`） | 仅记到 stderr |
| `<dataRoot>/webprofiles/<agentId>/` | `awb_web_webengine` | 每 agent 的 Cookies/localStorage | 重建目录 |
| `~/Downloads`（可配置） | `awb_web_webengine` | WebEngine 下载落盘 | 通知失败原因 |

`<dataRoot>` 默认 `%USERPROFILE%/.AgentWorkbench`。

### 7.2 `settings.json` 键表（v1）

未列出的键一律忽略并记警告；缺失的键取默认值。**没有配置迁移代码**：加键就加默认值，删键就删默认值。

```json
{
  "window":  { "title": "", "width": 1440, "height": 900,
               "sidebarWidth": 240, "sidebarCollapsed": false, "lastPageId": "agents" },
  "appearance": { "theme": "mocha-dark", "followSystem": false },
  "locale":  { "override": "" },
  "launcher": { "healthCheckIntervalMs": 3000, "startupVersionCheck": true },
  "web":     { "surface": "embedded", "freezeInactiveTabs": true, "maxLiveTabs": 8,
               "downloadDir": "", "chromiumFlags": "", "homeUrl": "" },
  "skills":  { "roots": [], "includePluginCaches": true, "maxDepth": 6 },
  "logging": { "maxFileSize": 5242880, "maxFiles": 3 },
  "plugins": { "enabled": false, "disabledIds": [] }
}
```

- `window.title` 为空 = 用应用默认标题 `AgentWorkbench`。
- `skills.roots` 为空 = 用平台默认根清单（`02-ui-specification.md` §7.2）。非空即完全取代默认（不是追加）。
- `web.surface` ∈ `embedded` | `external`；`embedded` 在未编译 WebEngine 时自动降级为 `external` 并记警告。
- `appearance.theme` 指向主题 `id`；不存在时回退 `mocha-dark` 并记警告。
- 旧 `agents.json` 根级的 `title` 字段**不再使用**：若检测到有值而 `settings.json` 没有，记一条 INFO 提示迁移到设置页。不做双源读取。

### 7.3 旧数据目录接管（唯一的一次性迁移）

`core::LegacyImport::runOnce()` 在 `Paths` 初始化后、其它模块构造前执行：

1. 若 `<dataRoot>` 已存在且非空 → 什么都不做（一次也不做）。
2. 否则若 `~/.AgentLauncher/` 存在 → **复制**（不是移动）`agents.json`、`agent_state.json`、`log/` 到新目录，写一条 INFO 日志，并在 UI 上弹一次 toast：「已接管旧版 AgentLauncher 的配置」。
3. 旧目录永不删除。

这条例外只针对**数据目录位置**；agent 定义本身仍然按 §1.2 的规则重新生成，不要顺手做别的迁移。

---

## 8. QML 层契约

### 8.1 QML 模块与资源

- 可执行目标用 `qt_add_qml_module(AgentWorkbench URI AgentWorkbench ...)` 承载**所有** `.qml`，文件来自各模块的 `qml/` 目录，用 `QT_RESOURCE_ALIAS` 给出稳定别名，最终 URL 形如 `qrc:/qt/qml/AgentWorkbench/agents/AgentCard.qml`。
- 页面 QML 用 `import AgentWorkbench` 拿共享组件；资源文件（图标、内置主题、默认配置）另用普通 `qt_add_resources`，前缀 `/icons`、`/themes`、`/config`。
- 各模块的 `.qml` 由**模块自己拥有**（改页面 = 改该模块目录），但编译进 exe 的资源清单集中在 `app/CMakeLists.txt`——一份可 review 的「发什么」清单。

### 8.2 C++ 与 QML 的绑定方式（已踩过的坑，照做）

| 对象 | 注册方式 | QML 里的名字 |
| --- | --- | --- |
| 共享 QML 组件（`components/`） | `qt_add_qml_module` 的 `QML_FILES` | `import AgentWorkbench` |
| `Theme` | `qmlRegisterSingletonInstance("AgentWorkbench.App", 1, 0, "theme", &theme)` | `theme`（`import AgentWorkbench.App`） |
| `NavigationModel`、`ShellController`、`UiServices`、`Notifications` | 同上，URI `AgentWorkbench.App` | `nav`、`shell`、`ui`、`toasts` |
| `AgentsFacade`、`SkillsFacade`、`WebTabsFacade`、`WorkbenchContext`、`EnvironmentService` | 同上，URI `AgentWorkbench.App` | `agents`、`skills`、`web`、`workbench`、`environment` |

两条硬约束：

1. **不要**往 `AgentWorkbench` 这个 URI 里手工注册 C++ 单例——它已经是 `qt_add_qml_module` 生成的（有 qmldir 的）模块，会报 `Cannot install element ... into protected module`。C++ 全局统一放在 `AgentWorkbench.App` 这个纯 C++ URI 下。
2. **不要**用 `engine.rootContext()->setContextProperty()`：qmlcachegen 无法分析未限定访问，会一直报 warning，且性能与可测性都更差。

qmlcachegen 对 `AgentWorkbench.App` 这种「只在 C++ 里注册」的 URI 无法在编译期解析，构建日志会出现 unresolved-import 类警告，属于**预期现象**，不要为了消除它改设计（除非同时引入 `.qmltypes` 生成）。

### 8.3 页面挂载协议

1. 每个功能在 `workbench::BuiltinPages`（或未来插件的注册回调）里向 `NavigationModel` 注册 `PageDescriptor`。
2. `Sidebar.qml` 用 `Repeater` / `ListView` 渲染 `nav`（按 `section` 分组），点击调 `nav.setCurrentPage(id)`。
3. `Workspace.qml` 用一个 `Loader`：`source = nav.currentPage.source`，并把该页面的标题与操作区交给页面自己渲染（页面根元素用 shell 的 `PageHeader`）。
4. 页面**不要**互相 push/pop（0.3.0 的 `StackView` 模式废弃）。需要二级视图时，在工作区内用 `Loader`、`Dialog` 或该页面自己的内部导航。
5. 页面销毁即释放：页面 QML 不该在 `Component.onDestruction` 之外持有必须存活的资源；需要跨页面存活的状态一律放 C++ 侧（模型/服务）。

---

## 9. 插件化基础

### 9.1 现在就要做对的三件事

1. **内置功能与插件走同一条注册路径**：三个内置页面的注册代码与插件注册回调调用的是同一组 API（`Services::registerPage`）。任何「先硬编码，以后再说」的写法都会让插件化永远做不成。
2. **ABI 边界只出现 Qt 类型**（`QString`、`QUrl`、`QJsonObject`、`QObject*` 除外原则：不跨边界传自定义 C++ 类），且带 `apiVersion`。
3. **主题、配置、数据目录对插件开放但只读/受限**：插件通过 `Services` 拿数据目录、拿主题令牌、发通知、注册页面；不许插件直接改 `agents.json`。

### 9.2 插件清单 `plugin.json`

```json
{
  "id": "com.example.notes",
  "name": "Notes",
  "version": "0.1.0",
  "apiVersion": 1,
  "description": "...",
  "author": "...",
  "entry": "notes.dll",
  "pages": [
    { "id": "notes", "title": "Notes", "icon": "qrc:/icons/bot.svg",
      "source": "qrc:/qt/qml/Notes/NotesPage.qml", "section": "extensions", "order": 50 }
  ]
}
```

### 9.3 C ABI（`src/plugin_api/PluginApi.h`）

```cpp
extern "C" {
  // 返回插件编译时使用的 API 版本。
  AWB_PLUGIN_EXPORT int awb_plugin_api_version();
  // 注册入口。返回 0 表示成功；非 0 时宿主记日志并忽略该插件。
  AWB_PLUGIN_EXPORT int awb_plugin_register(awb::plugin::Services *services);
}
```

`Services`（纯虚接口，只在 Qt Core 类型上签名）：`registerPage(PageDescriptor)`、`unregisterPage(QString)`、`addWebSurface(kind, componentUrl)`、`dataDir(QString pluginId)`、`log(Level, QString)`、`notify(Level, QString title, QString text)`、`themeColor(QString token)`、`settingsValue(QString key)`。

宿主侧规则：`apiVersion` 不匹配 → 拒绝加载并给出可读原因；加载失败绝不影响启动；插件默认全部禁用，用户在 设置 → 插件 里逐个启用；设置页必须显示一段「插件在宿主进程内运行，信任级别等同于应用本身」的警示。

### 9.4 路线

| 阶段 | 内容 | 本文档对应 |
| --- | --- | --- |
| 现在 | 只做 §9.1 的三条约束 + 把内置页面做成「可注册」 | S4/S7 |
| S7 | `awb_plugin_api` + `core::PluginHost` + `examples/plugins/hello/` 示例（可编译、可加载、加一个页面）+ 设置页插件列表 | `03-migration-plan.md` §3.8 |
| 以后 | 插件市场/签名/权限声明、独立进程沙箱、插件自带 QML 模块 | 不在本次范围 |

---

## 10. 构建、打包、翻译、版本

- 顶层 `CMakeLists.txt` 只做：`project(AgentWorkbench VERSION 0.4.0 LANGUAGES CXX)`、选项、`add_subdirectory`、翻译、打包。模块构建细节下放到各模块的 `CMakeLists.txt`；公共选项放 `cmake/AwbOptions.cmake`。
- 选项：`AWB_ENABLE_WEBENGINE`（默认 ON）、`BUILD_TESTING`（默认 ON）、`AWB_BUILD_PLUGIN_EXAMPLES`（默认 OFF）。
- WebEngine 只有 MSVC 有；`AWB_ENABLE_WEBENGINE=ON` + MinGW 必须在 configure 阶段就给出可读的报错，而不是链接期一堆符号错误。
- 翻译：**一份** `translations/agentworkbench_zh_CN.ts`，源文件清单集中在 `cmake/AwbTranslations.cmake`（`AWB_TS_SOURCES` 变量 + QML 文件清单），编译产物以 `:/i18n/` 嵌入。
- 打包：`scripts/package.sh` 走 `build.sh --print-qt` 拿 Qt 前缀，Release + windeployqt + zip。WebEngine 打开时按调研结论加 `--no-translations`（只带 en-US locale），并在打包后**记录 zip 体积**（预期 110–130 MB）。
- 构建脚本需要同步的细节：`--target` 的合法取值从 `AgentLauncher, AgentLauncherTests` 改为 `AgentWorkbench` + 各测试目标；`--print-exe` 的默认输出路径改为 `AgentWorkbench.exe`；生成的环境包装文件重命名为 `build/.build-agentworkbench.bat`。项目改名后 `build/` 里的 CMake 缓存属于旧项目名，第一次必须 `bash scripts/build.sh --clean`。
- 版本与变更记录：本次重构对应 `0.4.0`，`CHANGELOG.md` / `CHANGELOG-zh.md` 都要写，中文版是正文。

---

## 11. 测试策略

| 目标 | 覆盖 | 说明 |
| --- | --- | --- |
| `tst_core` | `Paths`、`JsonStore`、`Settings`、`EnvExpander`、`IconResolver`、`TextUtils`、`Logging` | 日志滚动、`formatCommandLine`、`clampOutput` 用现有用例；`Paths` 用 `QTemporaryDir` 注入 |
| `tst_agents` | `AgentRepository`（内置同步、removed、调色板、逐字节写内置文件）、`AgentModel`（插入/删除/角色）、`AgentsFacade`（CRUD）、`AgentScripts`（install 日志端到端）、`AgentUrls`（token 拼接） | 现有 15 个用例全部有归属（映射表见 `03-migration-plan.md` §5） |
| `tst_skills` | `SkillFrontmatter`（引号/折叠标量/BOM/CRLF/缺失 frontmatter）、`SkillScanner`（多根、插件多版本去重、无权限目录） | 用固定文本样本，不依赖本机真实目录 |
| `tst_theme` | `ThemeLoader`（未知键、缺失键回退、非法颜色、id 不匹配）、`ThemeRegistry`（用户主题覆盖内置） | 用 `QTemporaryDir` 写主题文件 |
| `tst_web` | `WebTabsModel`、`WebTabsFacade`（同 agent 复用、关闭不影响进程、offline 转换）、`WebSurfaceRegistry` | 不加载 Qt WebEngine，因此可在任意配置下跑 |
| `check_architecture` | §5.3 的静态检查 | 作为 ctest 中的一个用例 |

规则：测试不许读写开发者真实数据目录（构造注入 + `QStandardPaths::setTestModeEnabled(true)` 双保险）；测试不许依赖网络与已安装的 agent 工具；面向 GUI 的逻辑尽量下沉到可测的 C++（QML 只留布局与绑定）。

WebEngine 部分（渲染、IME、DPI、剪贴板、拖放、全屏、通知、打印）无法自动化，走 `03-migration-plan.md` §6 的人工验收清单。

---

## 12. 未决问题与当前建议

| 问题 | 建议 | 影响 |
| --- | --- | --- |
| 是否升级 Qt（6.7.3 → 6.9/6.10，Chromium 118 → 130/134） | **本次重构不升**（不引入额外变量），Web 功能稳定后单独评估再升 | 只影响 Chromium 版本与缺失的 H.264，见调研报告 §4.1 |
| 是否需要「无 WebEngine」的瘦身发行包 | 保留 `AWB_ENABLE_WEBENGINE=OFF` 构建路径，但默认只发一个包 | 打包脚本增加 `--lean` 开关 |
| 标签页释放策略默认值 | 关闭 = 销毁视图（profile 持久化，会话不丢）；失活 = `Frozen`；超 `maxLiveTabs` 时按 LRU 释放最久未用的冻结标签 | 内存 250–350 MB/视图，见调研 §4.2 |
| QML 是否需要 `qmlcachegen` 全量编译 | 用 `qt_add_qml_module` 的默认行为即可，不做额外配置 | 构建期能更早发现 QML 语法问题 |
| 仓库目录/远程名是否同步改为 AgentWorkbench | 本次只改 CMake 项目名、目标名、数据目录、翻译前缀；仓库目录与 GitHub 仓库名由人类另行决定 | 影响文档链接与 `docs/` 站点 URL |
