# AGENTS.md

面向在本仓库中工作的 AI 编码 agent 的指导说明。

## 项目简介

AgentWorkbench（0.3.0 及以前叫 AgentLauncher）是一个用 Qt6/QML + C++ 开发的 AI 编码 agent 工作台：左侧边栏 + 右侧工作区的外壳，内嵌 Web 标签页、Skill 浏览、配置文件驱动的主题与实验性插件。它**配置化驱动**：agent 的定义（命令、Web 地址、配置目录、颜色）都在 `agents.json` 里，应用设置在 `settings.json` 里，而不是写在 C++ 中。

## 规格（必读）

重构规格在 `specs/` 下，**动手改代码前先读**：

- `specs/01-architecture.md` — 分层、模块划分、依赖规则、配置与数据文件、插件化基础
- `specs/02-ui-specification.md` — 窗口骨架、侧边栏、各页面、主题文件与令牌规格
- `specs/03-migration-plan.md` — 分阶段实施计划、文件与测试迁移映射、验收清单

规格里的模块名、目标名、路径、公开类型名、配置键名、主题令牌名是契约：实现要与规格一致；确需改动时先改规格并写清理由。规格与代码冲突时以规格为准。实施状态与人工验收记录追加在 `specs/03-migration-plan.md`。

## 构建

```bash
bash scripts/build.sh              # Debug 构建到 build/
bash scripts/build.sh --test       # 构建后运行单元测试（含 check_architecture）
bash scripts/build.sh --release    # Release 构建到 build-release/
bash scripts/build.sh --help       # 全部选项
```

日常编译请用 `scripts/build.sh`，不要直接手写 cmake 命令：它自动探测 Qt 与 MSVC、复用构建目录里已有的生成器与 Qt 前缀、在构建目录属于旧路径时清掉陈旧的 CMake 缓存，并生成 `compile_commands.json`。Git Bash 下无法用 `eval "$(cmd //c ... set)"` 把 vcvars64 环境导入当前 shell（cmd 收到的是转义后的引号，`cl.exe` 不会出现在 PATH 里），所以脚本改为生成一个 `.bat` 把 vcvars + cmake 包起来执行——这是在本仓库里从 Git Bash 驱动 MSVC 唯一可靠的做法，不要在其它写法上反复试错。构建目录、生成器与 Qt 前缀的解析顺序都写在脚本头部的注释里，需要手工排查时可直接读 `build/.build-agentworkbench.bat` 看实际执行的命令。

需要手工执行时的等价命令（前提是自己已经准备好 MSVC 环境，例如在「x64 本机工具命令提示符」中运行）：

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

- 需要 Qt 6.5+（模块：Core、Gui、Qml、Quick、QuickControls2、Network、LinguistTools；内嵌 Web 另需 WebEngineQuick）。
- 需要 CMake 3.16+，C++17。
- 生成器：Ninja（推荐）或 MSBuild。脚本新建构建目录时优先用 Ninja；构建目录已配置过则沿用其生成器，因此 `--release` 不需要 MSVC 环境也能跑。
- 构建选项（`cmake/AwbOptions.cmake`）：`AWB_ENABLE_WEBENGINE`（默认 ON，MinGW + ON 在配置期报错）、`BUILD_TESTING`（默认 ON）、`AWB_BUILD_PLUGIN_EXAMPLES`（默认 OFF）。额外参数经 `bash scripts/build.sh -- -D…` 传入。
- 发布打包用 `bash scripts/package.sh`：它调用 build.sh 完成 Release 构建，然后 windeployqt + zip。要改 Qt 前缀只改一处——`package.sh` 通过 `build.sh --print-qt` 取同一个值。
- 测试目标：`tst_core`、`tst_agents`、`tst_theme`、`tst_shell`、`tst_web`、`tst_skills` 与 `check_architecture`；可以 `./build/tst_core testRoundTrip` 这样按名字跑单个用例。

## 目录结构

```
app/           可执行文件：组装（main.cpp）、QML 模块、全部嵌入资源
src/
  core/          L0 基础设施：Paths、JsonStore、Settings、Logging、ProcessRunner、
                 ScriptRunner、HttpProbe、PluginHost、LegacyImport
  plugin_api/    L0 插件 ABI（仅头文件）
  theme/         L1 主题引擎：ThemeFile/ThemeLoader/ThemeRegistry/Theme
  agents/        L2：AgentDefinition/AgentState、AgentRepository、AgentModel、
                 AgentRuntime、AgentScripts、AgentHealthMonitor、AgentsFacade + QML
  shell/         L2 UI 框架：NavigationModel、ShellController、UiServices、
                 Notifications、窗口骨架 QML、A* 组件（不认识 agent/skill/web）
  skills/        L2：SkillFrontmatter、SkillScanner、SkillModel、SkillsFacade + QML
  web/           L2：WebTab、WebTabsModel、WebSurfaceRegistry、WebTabsFacade + QML
    webengine/   L2 适配器（唯一链接 Qt WebEngine 的目标）
  workbench/     L3：WorkbenchContext、BuiltinPages、EnvironmentService、PluginServices
cmake/         AwbOptions.cmake、AwbTranslations.cmake（可翻译源清单）
resources/     内置主题 JSON（mocha-dark、latte-light）
config/        default_agents.json（打包为 Qt 资源）
icons/         SVG 图标（打包为 Qt 资源）
examples/      示例插件（AWB_BUILD_PLUGIN_EXAMPLES）
translations/  .ts 翻译源文件（构建时编译为 .qm，以 :/i18n/ 嵌入为资源）
docs/          MkDocs 站点（英文 + zh/）
specs/         重构规格（架构 / 界面 / 实施计划，见上文）
tests/         每模块一个测试目标 + check_architecture
scripts/       build.sh、package.sh、check-architecture.sh
```

## 配置结构

数据目录是 `~/.AgentWorkbench/`（`core::Paths::dataRoot()` 是唯一来源；首次启动会**复制**旧 `~/.AgentLauncher` 的数据，旧目录不删）。单元测试在 `QStandardPaths::setTestModeEnabled(true)` 下运行——测试模式不重定向 `HomeLocation`，因此 `dataRoot()` 改用测试模式对应位置；需要固定目录的测试用 `Paths::setDataRootForTesting()` 注入 `QTemporaryDir`。

`settings.json` 的键位表见 `specs/01` §7.2，读写只经 `core::Settings`，禁止在别处直接读这个文件。`agents.json` 根级 `title` 字段已停用（窗口标题来自 `window.title`，残留值会记一条 INFO）。根级可选的 `removed` 数组记录用户删除的内置 agent id；内置 agent 每次启动都按随包默认重新生成，靠这个列表保持删除状态。

每个 agent 对象包含：`id`、`name`、`command`、`webUrl`、`configDir`、`icon`、`color`、`cardColor`、`installCommand`、`updateCommand`、`versionCommand`、`setupCommand`、`tokenFile`。**内置** agent 的定义只来自 `config/default_agents.json`，改它并重新编译即可，不要在 C++ 中硬编码 agent 条目；代码里**没有**针对旧版本配置的迁移处理，也不要再加。与默认完全一致（无自建、无删除）时 `save()` 逐字节写入内置文件，保持可 diff。

`icon` 解析在 `core::IconResolver`（fallback 由调用方给出，core 不写死应用资源路径）；环境变量展开在 `core::EnvExpander`（`%VAR%` 与 `~`）。`color` 留空时从**当前主题**的 `agentPalette` 按位置循环分配（`AgentRepository::paletteColorAt` 是回退）。

## 约定

- **依赖方向**：`app → workbench → {shell, agents, skills, web, theme} → core`；领域模块之间零依赖，跨域行为写在 `awb_workbench`（或经 `WorkbenchContext` 的意图方法）。`scripts/check-architecture.sh` 会卡反向 include、QML 字面色值、tr 非英文源串、core/theme 的 UI 纯净性，违反即 ctest 失败。
- **QML 契约**：页面只用 `theme.*` 语义令牌，绝不写字面颜色；C++ 全局注册在 `AgentWorkbench.App` URI 上且类型名必须大写（Qt ≥6 拒绝小写单例名），QML 侧经 `MainWindow.qml` 根别名保持小写契约名（`theme`、`nav`、`shell`、`ui`、`toasts`、`agents`、`web`、`skills`、`workbench`、`environment`）；不要用 `setContextProperty`。
- **门面**：QML 只与门面对象和模型交互——agent 操作走 `agents.*`（`AgentsFacade` 保留 0.3.0 的 Q_INVOKABLE/信号名），跨域动作走 `workbench.*`，通知走 `workbench.notify`/`toasts`。
- 运行状态通过向 `webUrl` 发 HTTP 健康检查来检测（任何 HTTP 响应 = 运行中；连接被拒绝/超时 = 已停止）。不要新增进程嗅探逻辑。
- Python / Node.js 版本由 `workbench::EnvironmentService` 检测，状态栏徽标绑定 `environment.*`；缺失时显示红色 ×。
- 启动用 `core::ProcessRunner::startDetached`（先 `findExecutable` 解析 PATHEXT，`.cmd`/`.bat` 垫片经 `cmd /c` 包装），agent 在启动器关闭后仍继续运行；一次性命令（install/update/version/setup）统一走 `core::ScriptRunner`，输出实时上卡片。
- `setupCommand` 成功（退出码 0）后写 `agent_state.json`（`AgentStateStore`），之后不再重复运行，除非卡片右键「重新初始化」。
- 修改 QML 时只用主题令牌；深浅色都必须可读。
## 国际化（i18n）

这是一个国际化的开源项目，所有面向用户的字符串都必须可翻译。

- **源语言是英文。** 绝不要在 `tr()` 或 `qsTr()` 里写中文（或任何其它非英文语言）——源字符串必须是英文。中文及其它语言的翻译放在 `translations/` 下的 `.ts` 文件里。
- **C++**：每个面向用户的字符串都用 `tr()` 包裹。
- **QML**：每个面向用户的字符串都用 `qsTr()` 包裹。
- 翻译文件位于 `translations/`。构建时通过 `cmake/AwbTranslations.cmake` 里的源清单运行 `lupdate`（从源码同步 `.ts`）和 `lrelease`（编译 `.qm`）。编译出的 `.qm` 以 Qt 资源形式嵌入在 `:/i18n/` 下。**新增可翻译文件时把它加进 `AWB_TS_SOURCES`**，不要改 `qt6_create_translation` 调用本身。
- `app/main.cpp` 安装 `QTranslator`，根据系统区域设置自动加载（前缀 `agentworkbench`）。
- 添加新语言：创建 `translations/agentworkbench_<locale>.ts`，把它加入 `AWB_TS_SOURCES`，然后构建（lupdate 会填充内容）。填写翻译后重新构建。
- 注释、标识符和日志信息也应使用英文。
- 本地化文档（`docs/zh/`、`README-zh.md`）以及 `mkdocs.yml` 中的语言名称标签**不是**源代码——它们是正当的本地化内容，不受本规则约束。

## 提交

每完成一个完整任务就提交一次，不要只改不提交；提交前先跑 `bash scripts/build.sh --test`，确认能编译且测试通过。提交信息按 Conventional Commits 格式写：类型与 scope 用标准英文关键字（`feat`、`fix`、`docs`、`refactor`、`test`、`build`、`chore`；scope 用模块名，如 `core`、`agents`、`shell`、`web`、`skills`、`theme`、`build`），描述与正文用中文，正文只说清楚「为什么这么改」。

只提交本次任务相关的文件——这个仓库的工作区经常有其它在途改动，不要用 `git add -A`。除非用户明确要求。

在进行代码提交时，应避免将一个大任务的全部改动积压到最后一次性提交。每个提交应尽量保持原子性，并尽可能保证可独立构建、测试通过、审查和回滚。这样可以缩小变更范围，降低合并时产生大量冲突的概率

## 不要做

- 不要在 C++ 中硬编码 agent 定义。
- 不要在 `tr()`/`qsTr()` 里写非英文的源字符串。
- 不要在 QML 里写字面颜色（`check_architecture` 会拒绝）。
- 不要让领域模块互相 include，也不要用 `setContextProperty` 注册 C++ 全局。
- 除非明确要求，不要运行 `git push`（提交规则见上一节「提交」）。

## 停止 agent

agent 运行期间，卡片右上角会显示一个低调的 ×。点击它会结束**本次会话中由此启动的**进程树——`AgentRuntime::launch()` 记录 `startDetached` 返回的 PID，`stop()` 经 `core::ProcessRunner::killTree` 运行 `taskkill /F /T /PID …`，使整棵 `cmd → .cmd → node` 进程树终止。PID 只保存在内存中，因此：

- 通过 HTTP 健康检查检测到运行中、但**并非由本启动器启动**的 agent 没有 PID；`stop()` 只显示一条提示，不会结束任何进程。这类 agent 请用它们自己的命令停止（或用右键菜单的「强制停止」按端口杀）。
- 启动器重启后 PID 丢失——被健康检查识别为运行中的 agent，在重新从这里启动之前，无法从卡片上停止。

对 dev server 启动器而言，强制结束是有意为之；需要优雅退出的 agent 仍应通过它们自己的命令停止。

## Windows 下的启动

`AgentRuntime::launch()` 先用 `core::ProcessRunner::findExecutable` 解析裸程序名（它会应用 PATHEXT），再通过 `cmd /c` 运行 `.cmd`/`.bat` 垫片——单靠 `CreateProcess` 找不到 `qwen.cmd` 这类 npm 风格的垫片。启动失败会发出 `launchFailed(id, message)`；界面会在卡片原位闪一次红色，并弹出说明原因的弹窗。
