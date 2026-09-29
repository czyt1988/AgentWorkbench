# 开发

这一板块是给**改代码的人**看的。每个用户可见的功能都有独立一篇，讲它的前端设计、后端设计与业务逻辑，并点名涉及的文件与类。如果你需要的是整体心智模型，先看[架构板块](../architecture/index.md)；本页只讲怎么构建、怎么测试、以及每个功能对应哪一篇。

## 功能文档索引

| 功能 | 开发文档 | 对应的使用指引 |
|---|---|---|
| Agent 启动器（卡片网格、进程生命周期、健康检查） | [Agent 启动器](agent-launcher.md) | [Agent 启动器](../guide/agent-launcher.md) |
| 内嵌 Web 标签页（标签模型、内存策略） | [Web 标签页](web-tabs.md) | [网页界面](../guide/web-ui.md) |
| WebEngine 适配层（引擎差异、Qt 5/6 兼容） | [WebEngine 适配层](webengine-adapter.md) | [网页界面](../guide/web-ui.md) |
| Skill 浏览 | [Skill 浏览](skill-browser.md) | [技能](../guide/skills.md) |
| Agent Tools 页（提示词编写台、文件树） | [Agent Tools](agent-tools.md) | [Agent Tools](../guide/agent-tools.md) |
| 外壳、导航与组件 | [外壳与导航](shell-and-navigation.md) | [入门](../guide/index.md) |
| 设置页与设置文件 | [设置](settings.md) | [设置](../guide/settings.md) |
| 主题引擎 | [主题引擎](theme-engine.md) | [外观](../guide/appearance.md) |
| 跨域接线与页面注册 | [workbench 与页面](workbench-and-pages.md) | — |
| 插件宿主 | [插件宿主](plugin-host.md) | [插件](../guide/plugins.md) |
| 基础设施层（路径、JSON、进程、日志……） | [core 基础设施](core-infrastructure.md) | — |
| 翻译 | [国际化](i18n.md) | — |

## 环境要求

- **Qt 6.5+**，含 `Core`、`Gui`、`Qml`、`Quick`、`QuickControls2`、`Network`、`Concurrent`、`LinguistTools` 模块——或用 Qt 5.15.16 LTS 作为兜底工具链（见下文「Qt 5 支持」）。
- **CMake 3.16+**
- **C++17** 编译器（MSVC 2019+、GCC 9+ 或 Clang 10+）
- （可选）**Ninja** 生成器，构建更快。
- `third_party/spdlog` 子模块——用 `scripts/worktree-add.sh` 创建工作树时会自动检出。

## 构建

`scripts/build.sh` 一步完成配置与编译。它会自动探测 Qt 与 MSVC 工具链、复用已有构建目录的生成器与 Qt 前缀、清掉因项目目录被移动而残留的陈旧 CMake 缓存，并生成编辑器用的 `compile_commands.json`：

```bash
bash scripts/build.sh              # Debug 构建到 build/
bash scripts/build.sh --test       # 构建后运行单元测试
bash scripts/build.sh --release    # Release 构建到 build-release/
bash scripts/build.sh --help       # 全部选项
```

其它值得知道的选项：`--target NAME` 只构建单个目标、`--no-tests` 以 `-DBUILD_TESTING=OFF` 配置（打包用的就是这个）、`--run` 构建后启动应用、`--clean` 删掉并重新配置构建目录、`--print-exe` 打印可执行文件路径。

Qt 的解析顺序是：`QT_PREFIX`（或 `--qt`）→ 构建目录 CMake 缓存里记录的前缀 → 各盘常见安装位置（含 Qt 在线安装器的嵌套布局）→ `PATH` 上的 `qmake`/`qtpaths` → 最后是逐盘限时扫描（`AWB_QT_DEEP_SEARCH=0` 可跳过，`QT_DEEP_TIMEOUT` 设每盘的时间预算，单位秒）。版本低于要求的安装会被拒绝，而不是静默使用。一个可用的都找不到时，脚本会列出它见到的每个 Qt 及不可用原因，在终端上询问前缀；无人值守的运行（agent、CI）则以该报告直接失败，不会阻塞等待。

Windows + MSVC 下从 Git Bash 驱动编译器需要一个装载 `vcvars64.bat` 的 `.bat` 包装：用 `eval "$(cmd /c ... set)"` 把那个环境导入 Git Bash 是不行的——`cmd` 收到的是被转义后的引号，`cl.exe` 永远进不了 `PATH`。脚本会替你生成这个包装（`build/.build-agentworkbench.bat`），想看实际执行的命令时直接读它。

手工构建依然可行，但需要你自己准备好 MSVC 环境，例如在「x64 本机工具命令提示符」里：

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

## 目录结构

```
app/          可执行文件：组装（main.cpp、QML 模块、资源）
src/
  core/         L0 基础设施：Paths、JsonStore、Settings、Logging、
                ProcessRunner、ScriptRunner、HttpProbe、PluginHost、
                LegacyImport、IconResolver、EnvExpander、TextUtils、OpResult
  plugin_api/   L0 插件 ABI（只有头文件；外部仓库链接它）
  theme/        L1 主题引擎：JSON 主题 -> 语义令牌 -> QML
  agentcatalog/ L2：外部 agent 目录（定义、持久化、进程、健康检查、增删改）+ QML
  shell/        L2 UI 框架：导航、窗口骨架、通知、A* 组件
  skillcatalog/ L2：本机 skill 目录（SKILL.md frontmatter、扫描、模型、门面）+ QML
  tools/        L2：Agent Tools 页（工作区记忆、懒加载文件树、图标映射、提示词草稿）+ QML
  web/          L2：标签、表面、内存策略 + QML
    webengine/  L2 适配器（唯一链接 Qt WebEngine 的目标）
  workbench/    L3：跨域意图、内置页面、环境检测、插件服务
cmake/        共享构建选项（AwbOptions.cmake、AwbTranslations.cmake）
resources/    内置主题 JSON
config/       default_agents.json、default_file_icons.json（打包为 Qt 资源）
icons/        SVG 图标（打包为 Qt 资源）
translations/ agentworkbench_zh_CN.ts -> :/i18n/agentworkbench_zh_CN.qm
docs/         本站点（英文 + zh/）
tests/        每模块一个测试目标 + check_architecture
scripts/      build.sh、package.sh、check-architecture.sh、update-ts.sh、worktree-add.sh
```

每个模块的 QML 都放在自己的 `src/<模块>/qml/` 下；共享的 `A*` 组件在 `src/shell/qml/components/`。可执行文件的 `app/CMakeLists.txt` 持有唯一那份资源清单，它决定每个 QML 文件的 URL——注册路径见[外壳与导航](shell-and-navigation.md)，新增文件时要遵守的规则见[前端设计](../architecture/frontend-design.md)。

## 构建选项

| 选项 | 默认 | 含义 |
|---|---|---|
| `AWB_ENABLE_WEBENGINE` | `ON` | 内嵌网页视图（仅 MSVC；MinGW 下打开它会在配置期以一段可读的错误失败） |
| `BUILD_TESTING` | `ON` | 单元测试目标（需要 Qt Test 模块） |

`build.sh` 对它的两个测试开关都会显式设置 `BUILD_TESTING`——`--test` 配成 `ON`，`--no-tests`（`package.sh` 用的）配成 `OFF`——所以同一个构建目录在两种用法之间来回切都能正常工作。

额外配置参数写在 `--` 之后，例如 `bash scripts/build.sh -- -DAWB_ENABLE_WEBENGINE=OFF`。

## 运行测试

```bash
bash scripts/build.sh --test
```

ctest 会跑「每个模块一个可执行文件」加上架构门禁：

| 测试 | 覆盖内容 |
|---|---|
| `check_architecture` | QML 无字面颜色（十六进制或数字字面量的 `Qt.rgba`）、无反向/横向模块 include、源串只有 ASCII、core/theme 保持与 UI 无关、每个 QML 单例方法调用都有 `Q_INVOKABLE` 且每个属性写入都有 `WRITE` |
| `tst_core` | 路径、JSON 读写、设置、日志、进程执行器、脚本执行器、HTTP 探测、插件 manifest 解析、旧数据导入 |
| `tst_agentcatalog` | 仓库同步语义、模型角色、门面增删改、脚本日志、URL、运行期启动/停止/强制停止 |
| `tst_theme` | 加载器校验规则、注册表覆盖行为 |
| `tst_shell` | 导航注册、徽标、窗口持久化、剪贴板结果、QML 可调用方法面 |
| `tst_web` | 标签复用、关闭语义、离线/在线迁移、LRU 释放（不需要 WebEngine） |
| `tst_webengine` | QML 表面加载冒烟（仅在 `AWB_ENABLE_WEBENGINE=ON` 时构建） |
| `tst_workbench` | 跨域意图：`openWeb` 导航、外部表面不建标签的路径、交浏览器打开时的 URL 交接 |
| `tst_skillcatalog` | frontmatter 解析、扫描、插件版本去重、过滤 |
| `tst_tools` | 工作区记忆语义、懒加载树模型（角色/取子/增量刷新）、图标映射、门面接线、QML 可调用面 |

单个用例可以按名字跑，例如 `./build/tst_core testRoundTrip`。

有两条约定在这个仓库里咬过人：

- **用例必须写在 `private Q_SLOTS:` 里**（大写宏）。写在尾部小写 `private:` 之后的用例照样能编译、套件也照样报 100% 通过——它只是永远不会执行，而且没有任何警告。加完用例用 `./build/tst_core -functions` 确认它已被注册。
- **测试不许碰开发者真实的数据目录。** 它们在 `QStandardPaths::setTestModeEnabled(true)` 下运行；需要固定目录的测试经 `Paths::setDataRootForTesting()` 注入一个 `QTemporaryDir`。任何测试都不许依赖网络、依赖本机已安装的 agent 工具，或依赖真实数据目录。

## Qt 5 支持

项目同时面向 **Qt 6（6.5+，主线）** 与 **Qt 5.15.16 LTS（兜底）** 构建。Qt 5 侧验证过的下限就是 5.15.16：内嵌表面依赖的那些 WebEngine backport 只存在于 LTS 补丁版里。

版本差异被刻意集中到两处：

- **构建期差异**（组件改名、`qt_add_qml_module`、qrc 别名、按大版本区分的编译参数）写在 `cmake/AwbQtCompat.cmake` 的包装函数里。
- **编译期差异**写在需要它的代码旁边，用 `QT_VERSION_MAJOR` / `#if QT_VERSION` 分支。

不要用 `setContextProperty` 或 QML 里的版本判断绕开它。写跨版本代码前值得知道两个坑：Qt 5 路线的失败方式是**整页打挂或静默什么都不做**，而 Qt 6 的构建和所有 C++ 测试依旧全绿；以及「兼容」不等于「降级」——Qt 6 有更好的做法时，Qt 6 就用它，Qt 5 单独写一条显式的兜底分支。完整策略见 [C++ 库设计](../architecture/cpp-design.md)。

## 在本仓库工作

- **绝不直接在 `dev` 上开发。** 用 `bash scripts/worktree-add.sh <分支> [基线]` 创建工作树（例如 `bash scripts/worktree-add.sh feat/web-xyz`），在分支上开发、测试全绿、合并回 `dev`，然后删分支删树。工作树统一放在 `.worktree/` 下。「只有一个会话在跑」不是跳过这条规矩的理由——多工作树并行是本项目的常态。
- **一次提交一件事**，提交信息用 Conventional Commits 格式。只暂存与你这次改动相关的文件；工作区里常常还有别人在途的改动。
- **代码风格、命名与注释规范**见[编程规范](../standards/coding-standard.md)（中文版在 `docs/zh/standards/coding-standard.md`）。新代码与你改动到的任何类、函数都必须符合它。
- **界面设计规则**在仓库根目录的 `designs.md`——任何 UI 改动之前先读。
- **文档是「做完」的一部分。** 改完之后过一遍 `docs/AGENTS.md` 里那张「改了代码要同步改哪些文档」的对照表：功能行为、设置键、界面文案、组件、构建选项、插件 ABI、模块依赖都可能牵动文档。新增功能页时，同时把它加进上面的索引。

## 文档站点

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

打开 `http://127.0.0.1:8000`。站点是双语的（英文为默认，中文在 `/zh/` 下），用基于目录结构的 i18n 插件实现，Mermaid 图经 `pymdownx.superfences` 的自定义围栏渲染。页面结构、翻译规则与写作约定见 [docs/AGENTS.md](../AGENTS.md)。

## 相关

- [架构总览](../architecture/index.md)——分层、运行期装配，以及一次完整的交互。
- [分层与依赖](../architecture/layers-and-dependencies.md)——构建期强制的那些规则。
- [数据与状态](../architecture/state-and-persistence.md)——什么落在磁盘上，什么只活在内存里。
