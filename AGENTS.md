# AGENTS.md

面向在本仓库中工作的 AI 编码 agent 的指导说明。

## 项目简介

AgentLauncher 是一个用 Qt6/QML + C++ 开发的桌面应用，通过卡片网格启动 AI 编码 agent 的 Web 界面。它**配置化驱动**：agent 的定义（命令、Web 地址、配置目录、颜色）都在 `agents.json` 里，而不是写在 C++ 中。

## 构建

```bash
bash scripts/build.sh              # Debug 构建到 build/
bash scripts/build.sh --test       # 构建后运行单元测试
bash scripts/build.sh --release    # Release 构建到 build-release/
bash scripts/build.sh --help       # 全部选项
```

日常编译请用 `scripts/build.sh`，不要直接手写 cmake 命令：它自动探测 Qt 与 MSVC、复用构建目录里已有的生成器与 Qt 前缀、在构建目录属于旧路径时清掉陈旧的 CMake 缓存，并生成 `compile_commands.json`。Git Bash 下无法用 `eval "$(cmd //c ... set)"` 把 vcvars64 环境导入当前 shell（cmd 收到的是转义后的引号，`cl.exe` 不会出现在 PATH 里），所以脚本改为生成一个 `.bat` 把 vcvars + cmake 包起来执行——这是在本仓库里从 Git Bash 驱动 MSVC 唯一可靠的做法，不要在其它写法上反复试错。构建目录、生成器与 Qt 前缀的解析顺序都写在脚本头部的注释里，需要手工排查时可直接读 `build/.build-agentlauncher.bat` 看实际执行的命令。

需要手工执行时的等价命令（前提是自己已经准备好 MSVC 环境，例如在「x64 本机工具命令提示符」中运行）：

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

- 需要 Qt 6.5+（模块：Core、Gui、Qml、Quick、QuickControls2、Network、LinguistTools）。
- 需要 CMake 3.16+，C++17。
- 生成器：Ninja（推荐）或 MSBuild。脚本新建构建目录时优先用 Ninja；构建目录已配置过则沿用其生成器（例如 `build-release/` 目前是 Visual Studio 16 2019），因此 `--release` 不需要 MSVC 环境也能跑。
- 发布打包用 `bash scripts/package.sh`：它调用 build.sh 完成 Release 构建，然后 windeployqt + zip。要改 Qt 前缀只改一处——`package.sh` 通过 `build.sh --print-qt` 取同一个值。

## 目录结构

```
src/           C++ 后端：AgentConfig, AgentModel, AgentLauncher, main.cpp
qml/           QML 界面：main.qml, AgentCard.qml, AgentEditPage.qml, SettingsPage.qml
config/        default_agents.json（打包为 Qt 资源）
icons/         SVG 图标（打包为 Qt 资源）
translations/  .ts 翻译源文件（构建时编译为 .qm，以 :/i18n/ 嵌入为资源）
docs/          MkDocs 站点（英文 + zh/）
tests/         QtTest 单元测试（tst_core.cpp）
scripts/       构建与打包：build.sh（配置 + 编译 + 可选测试）、package.sh（Release + windeployqt + zip）
```

## 配置结构（agents.json）

根对象有一个可选的 `title` 字段（字符串）。设置后会覆盖应用窗口标题；为空或缺失时使用默认标题 `AgentLauncher`。根级其余内容就是下面描述的 `agents` 数组。

根级可选的 `removed` 数组列出用户在设置页删除的内置 agent 的 id。内置 agent 在每次启动时都会按随包默认配置重新生成，因此必须靠这个列表才能让已删除的内置 agent 保持删除状态。设置页的「恢复默认启动器」（Restore default launchers）按钮会清空该列表并重新应用全部内置项。

Agent 也可以在设置页（右下角齿轮按钮）添加、编辑和删除；界面通过 `AgentLauncher::addAgent()` / `updateAgentFull()` / `removeAgent()` 写入同一个 `agents.json`。

每个 agent 对象包含：`id`、`name`、`command`、`webUrl`、`configDir`、`icon`、`color`、`cardColor`、`installCommand`、`updateCommand`、`versionCommand`、`setupCommand`、`tokenFile`。**内置** agent 的定义只来自 `config/default_agents.json` 里的 `agents` 数组，改它并重新编译即可，不要在 C++ 中硬编码 agent 条目。用户自己在设置页新增的 agent 则写在 `~/.AgentLauncher/agents.json` 里。

`icon` 字段接受：`qrc:/icons/<name>.svg`（内置）、本地文件路径（会展开环境变量 `%VAR%` 和 `~`）、`http(s)://` URL，或留空（回退到 `qrc:/icons/default.svg`）。内置的中性图标有：`default`、`terminal`、`cube`、`bot`。`AgentConfig::resolveIcon()` 在解析阶段完成以上解析；`AgentConfig::expandEnv()` 负责环境变量展开（`configDir` 也用它）。

`cardColor` 字段是可选的——它设置卡片在非运行状态下的背景色。留空 = 默认 `#313244`。

`color` 字段是可选的——留空时会从内置的 Catppuccin Mocha 调色板中，按 agent 在列表中的位置循环自动分配一个颜色。分配到的颜色在首次运行时被持久化。

`setupCommand` 字段是可选的——它保存一条一次性命令，在 agent 首次启动前运行（例如为 `qwen serve` 生成 bearer token）。如果该命令以退出码 0 结束，结果会持久化到 `~/.AgentLauncher/agent_state.json`，之后不会再重复运行，除非用户从卡片右键菜单选择「重新初始化」（Re-initialize）。`setupCommand` 为空表示没有前置步骤——agent 直接启动。

`AgentConfig::userDataDir()` 是用户数据目录（`~/.AgentLauncher/`）的唯一来源：`agents.json`、`agent_state.json` 和日志都存放在那里。单元测试在 `QStandardPaths::setTestModeEnabled(true)` 下运行，而测试模式不会重定向 `HomeLocation`，因此 `userDataDir()` 会改用测试模式对应的位置——否则运行测试会改写开发者真实的配置。

`AgentConfig::load()` 用随包默认配置定义所有**内置** agent：磁盘上同 id 的条目会被 `config/default_agents.json` 里的定义整体覆盖，用户自建的 agent（id 不在默认列表中）原样保留，排在内置项之后。因此开发期只需要改 `config/default_agents.json` 并重新编译，下次启动就会生效；代码里**没有**任何针对旧版本配置的兼容或迁移处理，也不要再加。副作用是：设置页里对内置 agent 的修改会在下次启动时被默认配置覆盖，内置项的定义只能来自 `config/default_agents.json`。当配置与默认值完全一致（没有自建 agent、没有删除记录、标题未改）时，`save()` 会把资源里的默认配置逐字节写入，使 `~/.AgentLauncher/agents.json` 与 `config/default_agents.json` 保持可直接 diff。

## 约定

- 所有 agent 状态（命令、URL）都来自 `AgentConfig`。启动器通过 model 读写；界面从不自己保存一份 agent 数据。
- 运行状态通过向 `webUrl` 发 HTTP 健康检查来检测（任何 HTTP 响应 = 运行中；连接被拒绝/超时 = 已停止）。不要新增进程嗅探逻辑——保持基于 HTTP，以做到各工具行为一致。
- Python 和 Node.js 版本在启动时通过 `AgentLauncher::detectRuntimeVersions()` 检测（经由 `cmd /c` 运行 `python --version` / `node --version`）。结果通过 `Q_PROPERTY`（`pythonVersion`、`pythonInstalled`、`nodeVersion`、`nodeInstalled`）暴露给 QML，并以徽标形式显示在首页右上角。如果某个运行时不在 PATH 中，徽标显示红色 ×，提示说明依赖它的 agent 可能无法运行。
- 启动使用 `QProcess::startDetached`，因此 agent 在启动器关闭后仍继续运行。
- 如果设置了 `setupCommand` 且尚未运行过（记录在 `agent_state.json` 中），`launch()` 会先通过 `cmd /c` 运行它（无可见窗口），只有退出码为 0 才继续执行真正的启动命令。失败时发出 `launchFailed(id, message)`，并带上捕获到的输出。
- `configDir` 中的环境变量使用 `%VAR%`（Windows）形式；启动器会展开它们。`~` 也会展开为 home 目录。
- 修改 QML 时，保持深色主题配色（Catppuccin Mocha 调色板）。

## 国际化（i18n）

这是一个国际化的开源项目，所有面向用户的字符串都必须可翻译。

- **源语言是英文。** 绝不要在 `tr()` 或 `qsTr()` 里写中文（或任何其它非英文语言）——源字符串必须是英文。中文及其它语言的翻译放在 `translations/` 下的 `.ts` 文件里。
- **C++**：每个面向用户的字符串都用 `tr()` 包裹。
- **QML**：每个面向用户的字符串都用 `qsTr()` 包裹。
- 翻译文件位于 `translations/`。构建时通过 CMakeLists.txt 里的 `qt6_create_translation` 运行 `lupdate`（从源码同步 `.ts`）和 `lrelease`（编译 `.qm`）。编译出的 `.qm` 以 Qt 资源形式嵌入在 `:/i18n/` 下。
- `main.cpp` 安装 `QTranslator`，根据系统区域设置自动加载。
- 添加新语言：创建 `translations/agentlauncher_<locale>.ts`，把它加入 CMakeLists.txt 的 `TS_FILES` 列表，然后构建（lupdate 会填充内容）。填写翻译后重新构建。
- 注释、标识符和日志信息也应使用英文。
- 本地化文档（`docs/zh/`、`README-zh.md`）以及 `mkdocs.yml` 中的语言名称标签**不是**源代码——它们是正当的本地化内容，不受本规则约束。

## 不要做

- 不要在 C++ 中硬编码 agent 定义。
- 不要在 `tr()`/`qsTr()` 里写非英文的源字符串。
- 除非明确要求，不要运行 `git commit`/`git push`。

## 停止 agent

agent 运行期间，卡片右上角会显示一个低调的 ×。点击它会结束**本次会话中由此启动器启动的**进程树——`launch()` 记录 `startDetached` 返回的 PID，`stop()` 在 Windows 下运行 `taskkill /F /T /PID …`，使整棵 `cmd → .cmd → node` 进程树终止。PID 只保存在内存中，因此：

- 通过 HTTP 健康检查检测到运行中、但**并非由本启动器启动**的 agent 没有 PID；`stop()` 只显示一条提示，不会结束任何进程。这类 agent 请用它们自己的命令停止。
- 启动器重启后 PID 丢失——被健康检查识别为运行中的 agent，在重新从这里启动之前，无法从卡片上停止。

对 dev server 启动器而言，强制结束是有意为之；需要优雅退出的 agent 仍应通过它们自己的命令停止。

## Windows 下的启动

`launch()` 先用 `QStandardPaths::findExecutable` 解析裸程序名（它会应用 PATHEXT），再通过 `cmd /c` 运行 `.cmd`/`.bat` 垫片——单靠 `CreateProcess` 找不到 `qwen.cmd` 这类 npm 风格的垫片。启动失败会发出 `launchFailed(id, message)`；界面会在卡片原位闪一次红色，并弹出说明原因的弹窗。
