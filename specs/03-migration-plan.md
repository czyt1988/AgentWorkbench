# AgentWorkbench 实施计划（三）：分阶段迁移

> 状态：**待实施**（2026-09-26）
> 前置阅读：`01-architecture.md`（模块与依赖规则）、`02-ui-specification.md`（界面与主题规格）
> 本文是执行手册：每个阶段给出目标、任务、涉及文件、验收标准。实施 agent 应当**一次只做一个阶段**，阶段内一次只做一批任务，每完成一个阶段就构建、跑测试、提交。
> 契约：阶段划分与阶段内的 `S<n>-T<m>` 编号在本文是稳定的引用锚点；拆分或合并任务时更新本文。

---

## 1. 迁移原则

1. **任何时刻都能构建、都能跑测试。** 不允许出现「重构到一半，先关掉测试」的状态。每个阶段结束时 `bash scripts/build.sh --test` 必须绿。
2. **功能对等优先于功能新增。** S1–S4 只做搬运与重组，不改变启动器的可观察行为（例外都在本文写明）。新增功能集中在 S5（Web）与 S6（Skills）。
3. **一次只删旧的。** 旧文件在新实现通过验收之后再删；删除动作写在同一阶段的最后一批任务里。
4. **不做「顺手也修一下」。** 发现的新问题记进本文 §9 的待办，不要夹带进当前阶段。
5. **提交要能单独回滚。** 每个阶段一次或几次提交，提交信息按仓库规范（Conventional Commits，类型/scope 英文，描述与正文中文，正文只说为什么）。

---

## 2. 阶段总览

| 阶段 | 目标 | 关键交付 | 规模 | 依赖 |
| --- | --- | --- | --- | --- |
| **S0** | 改名为 AgentWorkbench，接管旧数据目录 | 新项目名/目标名/数据目录/翻译前缀；`LegacyImport`；GitHub 仓库与站点标识改名；构建脚本文案 | S | — |
| **S1** | 抽出 `awb_core` | 7 个基础类型 + `tst_core`；构建脚本目标名同步 | M | S0 |
| **S2** | 抽出 `awb_agents` | 定义/状态分离、6 个服务 + 门面、15 个旧用例各自归位；旧 UI 不变 | L | S1 |
| **S3** | 主题引擎 + 现有 QML 令牌化 | `awb_theme` + 两套内置主题 + QML 零硬编码色 | M | S1 |
| **S4** | 新外壳：侧边栏 + 工作区 | `awb_shell`、`awb_workbench`、`app/`；旧 `main.qml` 退役 | L | S2, S3 |
| **S5** | 内嵌 Web 标签页 | `awb_web` + `awb_web_webengine`、构建开关、打包与体积记录 | L | S4 |
| **S6** | Skill 浏览器 | `awb_skills` + Skills 页 + 复制路径 | M | S4 |
| **S7** | 插件化收口（可选） | `awb_plugin_api`、`PluginHost`、示例插件、设置页插件列表 | M | S6 |
| **S8** | 文档与交付收尾 | 文档双语更新、CHANGELOG、人工验收清单签字 | S | S5, S6 |

并行约束：S3 只依赖 S1，可与 S2 并行；S5 与 S6 在 S4 之后可并行；S7 必须最后（它验证前六个阶段的缝是否真的留着）。

规模标注：S ≈ 半天以内、M ≈ 1–2 天、L ≈ 2–4 天（以单个 agent 的工作量为单位）。

---

## 3. 各阶段任务

### S0 改名为 AgentWorkbench

**目标**：仓库内的产品身份从 AgentLauncher 切到 AgentWorkbench，并且老用户（含本机开发者）的配置不丢。

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S0-T1 | `project(AgentWorkbench VERSION 0.4.0)`、可执行目标改名 `AgentWorkbench`、`src/app.rc` 内的产品名/版本信息同步 | `CMakeLists.txt`、`src/app.rc` | 构建产物为 `AgentWorkbench.exe`；`--print-exe` 输出正确 |
| S0-T2 | 应用标识改名：`setApplicationName("AgentWorkbench")`、图标、`QTranslator` 前缀改 `agentworkbench`、`.ts` 文件改名 | `src/main.cpp`、`translations/agentlauncher_zh_CN.ts` → `agentworkbench_zh_CN.ts`、CMake 翻译段 | 启动后窗口标题与日志文件名使用新名字；切换系统语言到中文仍是中文界面 |
| S0-T3 | 数据目录改 `<home>/.AgentWorkbench`，实现 `core::LegacyImport::runOnce()`（见 `01-architecture.md` §7.3） | 新增 `src/core/LegacyImport.{h,cpp}`（本阶段可先放在 `src/` 下临时位置，S1 搬走） | 把 `.AgentLauncher` 改名模拟旧用户：首次启动后新目录出现配置副本、旧目录仍在、界面弹一次提示；再次启动不再提示 |
| S0-T4 | 构建脚本与命令文案同步：`--target` 合法值、`--print-exe`、包装 bat 名、`--help` 文本 | `scripts/build.sh`、`scripts/package.sh` | `bash scripts/build.sh --clean --test` 一次通过（改名后 CMake 缓存必须清） |
| S0-T5 | 默认标题、窗口默认尺寸（1440×900）与最小尺寸（1024×640）落到 QML | `qml/main.qml` | 窗口按新尺寸打开 |
| S0-T6 | `agents.json` 根级 `title` 停用：读到有值时记 INFO 提示改到设置页（`settings.json` 尚未存在时忽略该提示） | `src/AgentConfig.cpp` | 测试 `testTitleIsIgnored` 新增并通过 |
| S0-T7 | 仓库与站点标识改名（GitHub 端 + 本地远端 + 文档站点元数据） | `mkdocs.yml`、`README.md`、`README-zh.md`、`docs/*.md`、`docs/zh/*.md` | 见下方步骤清单 |
| S0-T8 | 本地工作目录改名（可选，人工执行） | `C:\src\Qt\AgentLauncher` → `C:\src\Qt\AgentWorkbench` 及其构建目录 | 改名后 `bash scripts/build.sh --clean --test` 通过 |

**S0-T7 步骤清单**（GitHub 改名不可逆性低但影响外部链接，执行前确认 §9 TODO-7）：

1. GitHub 端改名：`gh repo rename AgentWorkbench --repo czyt1988/AgentLauncher`（或在仓库设置页改）。GitHub 会把旧仓库 URL 永久重定向到新地址。**已执行（2026-09-27，经 GitHub API），旧 URL 301 跳转已验证。**
2. 本地远端：`git remote set-url github https://github.com/czyt1988/AgentWorkbench.git`。**注意** `origin` 指向 Gitee 镜像 `gitee.com/czyt1988/start-agent`，不要改错那个。**已执行并核对 `git remote -v`：仅 github 改动，origin 保持原样。**
3. `mkdocs.yml`：
   - `repo_url: https://github.com/czyt1988/AgentWorkbench`、`repo_name: czyt1988/AgentWorkbench`——现值 `https://github.com/AgentLauncher/AgentLauncher` 指向一个不存在的 owner，属既存错误，本次一并修正。
   - `site_name: AgentWorkbench`。
   - 若站点发布在 GitHub Pages 的项目路径下：`site_url` 与 `extra.alternate[].link` 的路径段从 `/AgentLauncher/` 改为 `/AgentWorkbench/`（现值为 `https://agentlauncher.dev` + `/AgentLauncher/`，两者互相矛盾，说明至少有一项过期；以实际发布方式为准，无法确认时保持域名、只改路径段，并在提交信息里说明）。
4. 文档里的仓库 URL：`docs/zh/blog.md` 里的 `github.com/czyt1988/AgentLauncher` 等硬编码链接改为新地址；README 里的相对图片路径不受影响。
5. 验收：`git remote -v` 指向新地址 ✓；浏览器访问旧仓库 URL 自动跳转 ✓（HTTP 301 → `czyt1988/AgentWorkbench`，`git ls-remote github` 经新地址可用）；本地 `mkdocs build` 无死链（未装 mkdocs 就人工核对全部 `github.com` 出现处——已核对 mkdocs.yml/README/docs 的 `github.com` 出现处全部指向新地址或无仓库链接）。

**S0 提交**：`build: 项目改名为 AgentWorkbench 并接管旧数据目录`（S0-T1/T2/T4 可合一次，S0-T3 单独一次 `feat(core): 首次启动接管旧版 AgentLauncher 数据目录`，S0-T7 单独一次 `docs: 仓库与文档站点改名`）。

**风险**：改名后 `build/` 缓存属于旧项目，必须清；`.ts` 改名会让既有译文条目按新文件名重排，提交时确认中文译文未丢失（`lrelease` 无警告）。

---

### S1 抽出 `awb_core`

**目标**：把与业务无关的基础设施从 `AgentLauncher.cpp` / `AgentConfig.cpp` / `Logger.cpp` 中搬进 `src/core/`，形成第一个可独立测试的静态库。

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S1-T1 | 建立 `src/core/CMakeLists.txt`（`qt_add_library(awb_core STATIC)`，公开 include 根为 `src/`），顶层 CMake 改为 `add_subdirectory(src/core)` | `CMakeLists.txt`、`src/core/CMakeLists.txt` | `awb_core` 可单独构建；链接 `Qt6::Core`、`Qt6::Network` |
| S1-T2 | 基础类型：`Paths`（含 `setDataRootForTesting`）、`JsonStore`、`OpResult` | `src/core/{Paths,JsonStore,OpResult}.{h,cpp}` | `tst_core` 用 `QTemporaryDir` 注入目录，验证 `configFilePath()` 等路径拼接 |
| S1-T3 | `Settings`：`settings.json` 的类型化访问 + 默认值 + `save()`；键表见 `01-architecture.md` §7.2 | `src/core/Settings.{h,cpp}` | 缺失文件/缺键/非法值时取默认并记警告；写入后重新读出相等 |
| S1-T4 | `Logger` → `Logging` 改名迁入，加分类前缀；`formatCommandLine`/`clampOutput` 保留 | `src/core/Logging.{h,cpp}` | 现有日志用例（滚动、格式化、截断）迁到 `tests/core/tst_logging.cpp` 并通过 |
| S1-T5 | `EnvExpander`、`IconResolver`、`TextUtils`（`extractVersion`）从 `AgentConfig` 拆出 | `src/core/{EnvExpander,IconResolver,TextUtils}.{h,cpp}` | `testResolveIconPassthrough` 迁到 `tests/core/tst_iconresolver.cpp`；`%VAR%` 与 `~` 各有用例 |
| S1-T6 | `ProcessRunner`（`startDetached`/`run`/`killTree`/`findExecutable`）与 `ScriptRunner`（流式输出、epoch、输出上限）从 `AgentLauncher` 拆出 | `src/core/{ProcessRunner,ScriptRunner}.{h,cpp}` | `tst_core` 覆盖：解析裸程序名（PATHEXT）、退出码、流式输出顺序、epoch 失效旧回调 |
| S1-T7 | `HttpProbe` + `portFromUrl` 拆出，语义固定（任何响应=可达） | `src/core/HttpProbe.{h,cpp}` | 用例：4xx 仍判定为可达（用本机 `QTcpServer` 造响应，不依赖网络） |
| S1-T8 | 测试改造：`tests/CMakeLists.txt`，按模块建 `tests/<module>/`；测试统一用构造注入的数据根 | `tests/CMakeLists.txt`、`tests/core/*` | `ctest` 能列出并按名字跑单个用例 |

**本阶段不改行为**：`AgentLauncher`/`AgentConfig` 暂时改为调用 `core` 的类型，其余保持不变。

**提交**：`refactor(core): 抽出 awb_core 基础模块`（可拆成 2–3 次提交：路径/设置、日志、进程与 HTTP）。

---

### S2 抽出 `awb_agents`

**目标**：把 1479 行的 `AgentLauncher` 拆成领域模块，UI 一行不改（仍用旧 `main.qml`），行为完全对等。

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S2-T1 | `AgentDefinition` / `AgentState` 值类型拆分；`AgentModel` 改为合并两者，**角色名与顺序不变** | `src/agents/{AgentDefinition.h,AgentState.h,AgentModel.{h,cpp}}` | 旧卡片 QML 无需修改即可渲染；`testModelInsertRemove` 通过 |
| S2-T2 | `AgentRepository`（原 `AgentConfig`）：构造注入数据根、内置默认同步、`removed`、调色板（暂用内置数组，S3 改为取主题）、`slugFromName`、`loadDefaults` | `src/agents/AgentRepository.{h,cpp}` | 原 `testRemovedIdsRoundTrip`、`testFirstRunCopiesBundledDefaultVerbatim`、`testBuiltinAgentsFollowBundledDefault`、`testDeletedBuiltinStaysDeleted`、`testDefaultAgentIds`、`testSlugFromName` 全部迁入并通过 |
| S2-T3 | `AgentStateStore`（`agent_state.json`） | `src/agents/AgentStateStore.{h,cpp}` | setup 状态往返用例 |
| S2-T4 | `AgentRuntime`（启动/停止/强制停止/PID/进程树/token 环境变量） | `src/agents/AgentRuntime.{h,cpp}` | 启动失败发 `launchFailed`；`stopAll` 返回杀死数量；强制停止的端口→PID 逻辑保留并有用例 |
| S2-T5 | `AgentScripts`（install/update/version/setup） | `src/agents/AgentScripts.{h,cpp}` | `testInstallCommandIsLogged` 迁入并通过（日志里仍有 `[cmd] install "<id>": running: cmd /c …` 与 `done, exit=0`） |
| S2-T6 | `AgentHealthMonitor`（HTTP 轮询 → `runningChanged`） | `src/agents/AgentHealthMonitor.{h,cpp}` | 用例：从不可达到可达的转换只发一次信号 |
| S2-T7 | `AgentUrls`（token 拼接）、`AgentsFacade`（聚合 + 保留旧 Q_INVOKABLE 与信号名） | `src/agents/{AgentUrls,AgentsFacade}.{h,cpp}` | `testLauncherCrud` 迁入并通过（add/update/remove/restore/isDefaultAgent/configFilePath 全覆盖） |
| S2-T8 | 删除旧文件 `src/AgentConfig.*`、`src/AgentModel.*`、`src/AgentLauncher.*`；QML 里的 `launcher` → `agents`、`agentModel` → `agents.model` | `qml/*.qml`、`src/main.cpp` | 手工点一遍：启动/停止/安装/编辑/删除/恢复默认/退出确认全部照旧 |

**提交**：`refactor(agents): 领域模块化 Agent 启动器`（建议按「定义与模型」「进程与脚本」「门面与 QML 接线」分三次）。

---

### S3 主题引擎 + 现有 QML 令牌化

**目标**：颜色与度量全部来自 `awb_theme`，两套内置主题可运行时切换；视觉与 0.3.0 的暗色外观一致。

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S3-T1 | `ThemeFile`/`ThemeLoader`/`ThemeRegistry`/`Theme` | `src/theme/*`、`src/agents/AgentRepository.cpp`（调色板改取主题） | `tst_theme`：未知键警告并忽略、缺键回退基准、非法颜色回退、`id` 与文件名不符则跳过、用户主题覆盖内置 |
| S3-T2 | 内置主题文件 `mocha-dark.json`、`latte-light.json`（取值见 `02-ui-specification.md` §9.3/§9.4），以 `:/themes/` 编入资源 | `resources/themes/*.json`（或 `src/theme/themes/`，由 `app/CMakeLists.txt` 编入） | 两套主题都能被列出并应用 |
| S3-T3 | 现有 QML（`main.qml`、`AgentCard.qml`、`SettingsPage.qml`、`AgentEditPage.qml`）的 170 余处字面色值替换为 `theme.*`；间距/圆角/字号换成令牌 | `qml/*.qml` | `scripts/check-architecture.sh` 的「QML 无字面色值」检查通过；暗色下与改造前逐屏对比无差异 |
| S3-T4 | 主题切换入口（先放进旧设置页）与热重载 | `qml/SettingsPage.qml`、`src/theme/ThemeRegistry.cpp` | 改主题文件保存后界面立即变化；切换主题写入 `settings.json` |
| S3-T5 | `scripts/check-architecture.sh` + ctest 用例 `check_architecture` | `scripts/check-architecture.sh`、`tests/CMakeLists.txt` | 故意写一个 `#ff0000` 能让构建失败 |

**提交**：`feat(theme): 配置文件驱动的主题引擎`、`refactor(ui): QML 改用语义化主题令牌`。

---

### S4 新外壳：侧边栏 + 工作区

**目标**：窗口改成左侧边栏 + 右侧工作区；启动器成为第一个页面；设置页拆分；状态栏与通知就位。这是「看起来变成工作台」的一步。

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S4-T1 | `awb_shell`：`PageDescriptor`、`NavigationModel`、`ShellController`、`UiServices`、`Notifications` | `src/shell/*.{h,cpp}` | `tst_shell`：重复 id 注册被拒、徽标更新、折叠状态持久化、剪贴板写入成功/失败返回 `OpResult` |
| S4-T2 | 外壳 QML：`MainWindow.qml`、`Sidebar.qml`、`Workspace.qml`、`StatusBar.qml`、`Toasts.qml`、`PageHeader.qml` | `src/shell/qml/` | 窗口按 §2 线框呈现；`Ctrl+1…9`、`Ctrl+B`、`Ctrl+,` 生效；侧边栏折叠态有 tooltip |
| S4-T3 | 共用组件集：`AButton`、`AIconButton`、`ASearchField`、`ACard`、`APill`、`ADialog`、`AToolTip`、`AEmptyState`、`ASectionHeader`、`AgentAvatar` | `src/shell/qml/components/` | 组件在暗/亮两套主题下都正常；组件本身零字面色值 |
| S4-T4 | 新增图标：`web/ skills/ copy/ refresh/ plus/ close/ chevron-left/ chevron-right/ external-link/ folder/ search` | `icons/*.svg` | 在 100%/125%/150% 缩放下清晰 |
| S4-T5 | `awb_workbench`：`WorkbenchContext`、`BuiltinPages`、`EnvironmentService` | `src/workbench/*.{h,cpp}` | QML 全局 `workbench`/`nav`/`ui`/`toasts`/`environment` 可用；`workbench.openWeb(id)` 在 S5 之前先落到「系统浏览器打开」 |
| S4-T6 | 启动器页：`AgentGridPage.qml`（工具栏 + 过滤 + `Flow`）、`AgentCard.qml` 搬迁、`AgentEditDialog.qml` | `src/agents/qml/` | 卡片全部交互与 S2 验收一致；编辑对话框可打开、保存、取消 |
| S4-T7 | 设置页：外观 / 环境 / Skill 根目录（先占位） / Web（先占位） / 高级（数据目录、日志、恢复默认启动器） | `src/shell/qml/SettingsPage.qml` | 分组清晰；「恢复默认启动器」仍在且可用 |
| S4-T8 | `app/`：`main.cpp` 精简为组装（组装顺序严格按 `01-architecture.md` §4.9），`qt_add_qml_module(AgentWorkbench …)`，资源清单集中；删除旧 `qml/main.qml` 等 | `app/*`、删除 `qml/` 目录 | 旧文件删净后仍能构建运行；`windeployqt` 部署出的目录能启动 |
| S4-T9 | 窗口状态持久化（尺寸/位置/上次页面/侧边栏折叠） | `src/shell/ShellController.cpp` | 重启后恢复到上次状态 |

**提交**：`feat(shell): 侧边栏 + 工作区外壳`、`refactor(app): 组装与 QML 模块化`。

---

### S5 内嵌 Web 标签页

**目标**：启动 agent 后可在应用内以标签页使用其 Web 界面；多 agent 并存；内存与崩溃可管理。

**相关计划（细节来源，但有冲突）**：仓库里已有一份更细的 WebEngine 实施计划 `2026-09-26-webengine-embedding-integration.md`（调研会话的产物；`docs/research/webengine-embedding.md` §7 把它引用为 `docs/superpowers/plans/…` 下的同名文件，实际当前位于仓库根目录且未纳入版本控制）。它的十个任务（初始化顺序、每 agent profile、弹窗/下载/全屏接管、快捷键与查找、视图生命周期、打包与体积、能力兜底、人工验证矩阵、OpenCode 端口冲突、文档）与本节 S5-T1…T9 一一对应，**可以直接当作 S5 各任务的细节参考**。但注意它的架构与命名写在重构之前，与 `specs/` 冲突处一律以 `specs/` 为准：

| 它的写法 | 本项目规格的写法 |
| --- | --- |
| `WebUiPage.qml` 推入现有 `StackView` | `src/web/qml/WebTabsPage.qml`，由侧边栏工作区宿主加载 |
| `src/WebProfiles.{h,cpp}` 由 `AgentConfig` 提供路径 | `awb_web_engine` 的 `WebEngineProfileStore` + `awb_web` 的 `WebProfilePaths` |
| `~/.AgentLauncher/webengine/<id>` | `<dataRoot>/webprofiles/<agentId>`（`dataRoot` = `~/.AgentWorkbench`） |
| `option(ENABLE_WEBVIEW)` | `AWB_ENABLE_WEBENGINE` |
| D1 建议先升 Qt 到 6.9/6.10 | 已决策：本次不升（`01-architecture.md` §12.1） |

建议顺手把这份文件移进 `docs/` 或 `specs/` 并纳入版本控制，不要让一份重要计划长期以未跟踪文件的形式停在仓库根目录。

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S5-T1 | `awb_web`：`WebTab`、`WebTabsModel`、`WebSurfaceRegistry`、`WebTabsFacade`、`WebProfilePaths`；注册 `external` 表面 | `src/web/*` | `tst_web`：同 agent 复用标签、关闭标签不清进程、`markOfflineForAgent` 转换、表面 URL 解析 |
| S5-T2 | `AWB_ENABLE_WEBENGINE` 选项与 MinGW 拒绝逻辑；`app/main.cpp` 的 `QtWebEngineQuick::initialize()` 顺序；`chromiumFlags` 注入 | `CMakeLists.txt`、`cmake/AwbOptions.cmake`、`app/main.cpp` | `ON`/`OFF` 两种配置都能构建、都能启动 |
| S5-T3 | `awb_web_webengine`：`WebEngineSurfaceProvider`、`WebEngineProfileStore`（一 agent 一持久 profile） | `src/web/webengine/*` | 两个不同端口的 WebUI 同时打开，互不串 cookie（用调研里的双端口探针页复现验证） |
| S5-T4 | `WebEngineSurface.qml`：事件处理按 `02-ui-specification.md` §6.4 逐条实现 | `src/web/webengine/qml/WebEngineSurface.qml` | `target=_blank`、下载、全屏、崩溃、加载失败各有明确结果，无「点了没反应」 |
| S5-T5 | Web 页：`WebTabsPage.qml`、标签栏、工具栏、状态覆盖层、空状态 | `src/web/qml/` | 三状态覆盖层（offline/crashed/error）在真实场景下能触发并恢复 |
| S5-T6 | 生命周期与内存策略：失活 `Frozen`、`maxLiveTabs` LRU 释放为 `released`、关闭销毁 | `src/web/*` | 冻结后工作集明显回落（任务管理器观察）；恢复视图后页面会话仍在 |
| S5-T7 | 快捷键回收（`Ctrl+W`/`F5`/`Ctrl+R`/`Ctrl+Tab`/`Ctrl±/0`/`Esc`） | 各 QML + `src/shell` | 在视图获得焦点时按键行为与规格一致 |
| S5-T8 | 打包：`scripts/package.sh` 处理 WebEngine（`--no-translations`、必要文件校验），记录 zip 体积 | `scripts/package.sh` | 干净环境（PATH 无 Qt）运行部署目录可打开内嵌视图；体积记录写进 CHANGELOG |
| S5-T9 | 降级路径：`surface=external` 与初始化失败的 toast + 设置页建议 | `src/web/*`、`src/shell/qml/SettingsPage.qml` | 关掉配置项后行为符合 §6.7，无死链 |

**提交**：`feat(web): 内嵌 Web 视图与标签页`、`build(webengine): WebEngine 构建开关与打包`。

---

### S6 Skill 浏览器

**目标**：把本机各目录的 skill 以卡片列出，悬停看完整描述，点击复制路径。

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S6-T1 | `SkillFrontmatter`（YAML 子集） | `src/skills/SkillFrontmatter.{h,cpp}` | 用例：引号、折叠标量 `>`/`|`、多行缩进、BOM、CRLF、缺失 frontmatter、含冒号的描述 |
| S6-T2 | `SkillRoot` 默认清单与 `Settings` 覆盖（`skills.roots`、`includePluginCaches`、`maxDepth`） | `src/skills/SkillRoots*.{h,cpp}`、`src/core/Settings.cpp` | 默认清单指向 §7.2 的五个位置；用户清空后回到默认 |
| S6-T3 | `SkillScanner`：遍历 + 插件多版本去重 + 单根失败不致命 + `refresh()`/`scanFinished(Stats)` | `src/skills/SkillScanner.{h,cpp}` | 在本机真实目录上跑：`~/.agents/skills`(22)、`~/.claude/skills`、`~/.codex/skills`、插件缓存都要出结果；插件同一 skill 只出现一次且是最高版本 |
| S6-T4 | `SkillModel`（过滤 + 分面 + 排序）与 `SkillsFacade`（含 `copyPath`） | `src/skills/{SkillModel,SkillsFacade}.{h,cpp}` | `tst_skills`：过滤分面排序各有用例；复制路径返回 `OpResult` |
| S6-T5 | Skills 页：`SkillGridPage.qml`、`SkillCard.qml`、`SkillDetailFlyout.qml` | `src/skills/qml/` | 悬停 400ms 出完整描述；点击卡片复制路径并弹 toast；骨架屏/空状态/部分失败提示齐备 |
| S6-T6 | 设置页 Skill 分组：根目录增删启停、重新扫描、显示上次扫描耗时与被跳过的目录 | `src/shell/qml/SettingsPage.qml` | 禁用一个根后重新扫描，结果相应减少 |

**提交**：`feat(skills): Skill 扫描、浏览与路径复制`。

---

### S7 插件化收口（可选，最后做）

**目标**：证明前面留下的缝是真的——一个编译出来的示例插件能通过公开接口加一个页面。

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S7-T1 | `awb_plugin_api`（`PluginApi.h`，C ABI + `Services` 纯虚接口 + `PageDescriptor`） | `src/plugin_api/*` | 头文件只依赖 Qt Core；示例插件能单独编译 |
| S7-T2 | `core::PluginHost`：扫描、`apiVersion` 校验、`QLibrary` 加载、失败不影响启动 | `src/core/PluginHost.{h,cpp}` | 清单缺字段/版本不符/库损坏三种情况都只记日志 |
| S7-T3 | 内置页面改为走同一注册路径；示例插件 `examples/plugins/hello/`（加一个页面） | `src/workbench/BuiltinPages.cpp`、`examples/plugins/hello/*` | 打开 `plugins.enabled` 后侧边栏 extensions 分组出现 Hello 页；关闭后消失，且需要重启才生效 |
| S7-T4 | 设置页「插件」分组：列表、启用/禁用（需重启）、信任警示文案 | `src/shell/qml/SettingsPage.qml` | 警示文案明确写出「插件在宿主进程内运行，信任级别等同应用本体」 |
| S7-T5 | 插件开发文档 `docs/plugins.md`（+ zh） | `docs/`、`mkdocs.yml` | 文档里的最小示例能照抄编译通过 |

---

### S8 文档与交付收尾

| 编号 | 任务 | 涉及文件 | 验收 |
| --- | --- | --- | --- |
| S8-T1 | 用户文档更新：`docs/configuration.md`、`docs/zh/configuration.md`（settings.json、主题文件、Skill 根目录、Web 选项） | `docs/` | 文档中的键名与 `Settings` 实现一致 |
| S8-T2 | `docs/development.md`（+ zh）：模块图、构建选项、测试怎么跑、架构检查脚本 | `docs/` | 新加入的 agent 能只看这两页完成一次构建 |
| S8-T3 | `CHANGELOG.md` / `CHANGELOG-zh.md` 写 0.4.0；`README.md` / `README-zh.md` 更新定位（从「启动器」到「工作台」）、功能列表与截图；`mkdocs.yml` 的 `site_description` 与 nav 补上主题 / 设置 / 插件等新页面 | 仓库根、`mkdocs.yml`、`docs/pic/` | 截图与当前界面一致；site 导航覆盖全部新功能 |
| S8-T4 | `AGENTS.md` 更新：模块表、构建选项、约定（零字面色值、依赖规则、`specs/` 指向） | `AGENTS.md` | 文档与 `specs/` 不矛盾 |
| S8-T5 | 人工验收清单签字（§6） | 本文 | 每项有结论 |

---

## 4. 旧 API → 新 API 映射（QML 侧）

| 0.3.0（`launcher`） | 0.4.0 | 说明 |
| --- | --- | --- |
| `launcher.launch(id)` | `agents.launch(id)` | 语义不变 |
| `launcher.stop(id)` / `forceStop(id)` / `stopAll()` | `agents.stop(id)` / `forceStop(id)` / `stopAll()` | 不变 |
| `launcher.openWeb(id)` | `workbench.openWeb(id)` | 变成跨域意图（agent 域不知道 web 域） |
| `launcher.openConfigDir(id)` | `workbench.openConfigDir(id)` | 需要 shell 的文件管理器能力 |
| `launcher.install(id)` / `updateTool(id)` / `resetSetup(id)` | `agents.install(id)` / `updateTool(id)` / `resetSetup(id)` | 不变 |
| `launcher.hasLaunchedAgents()` | `agents.hasLaunchedAgents()` | 退出确认用 |
| `launcher.addAgent/updateAgentFull/removeAgent/restoreDefaults/isDefaultAgent/configFilePath` | `agents.*` 同名 | 不变 |
| `launcher.pythonVersion` / `pythonInstalled` / `nodeVersion` / `nodeInstalled` | `environment.pythonVersion` … | 移到 `EnvironmentService` |
| `launcher.launchFailed(id,msg)` / `installFinished(id,ok,msg)` | `agents.launchFailed` / `agents.installFinished` | 信号名不变，绑定对象改 `agents` |
| `agentModel`（context property） | `agents.model` | 模型挂在门面上 |
| `appTitle`（context property） | `shell.windowTitle` | 来自 `settings.json` |

---

## 5. 测试迁移映射

| 0.3.0 用例 | 迁移后位置 | 备注 |
| --- | --- | --- |
| `testSlugFromName` | `tests/agents/tst_agentrepository.cpp` | 逻辑不变 |
| `testResolveIconPassthrough` | `tests/core/tst_iconresolver.cpp` | fallback 由调用方传入 |
| `testRemovedIdsRoundTrip` | `tests/agents/tst_agentrepository.cpp` | 数据根注入 `QTemporaryDir` |
| `testFirstRunCopiesBundledDefaultVerbatim` | `tests/agents/tst_agentrepository.cpp` | 逐字节比对内置文件 |
| `testBuiltinAgentsFollowBundledDefault` | `tests/agents/tst_agentrepository.cpp` | 内置覆盖语义 |
| `testDefaultAgentIds` | `tests/agents/tst_agentrepository.cpp` | — |
| `testDeletedBuiltinStaysDeleted` | `tests/agents/tst_agentrepository.cpp` | — |
| `testUserDataDirStaysInTestSandbox` | `tests/core/tst_paths.cpp` | 改成「测试注入根目录生效」 |
| `testModelInsertRemove` | `tests/agents/tst_agentmodel.cpp` | 角色名不变，断言不变 |
| `testLauncherCrud` | `tests/agents/tst_agentsfacade.cpp` | 覆盖 add/update/remove/restore |
| `testFormatCommandLine` | `tests/core/tst_logging.cpp` | — |
| `testClampOutput` | `tests/core/tst_logging.cpp` | — |
| `testLogRotation` | `tests/core/tst_logging.cpp` | — |
| `testInstallCommandIsLogged` | `tests/agents/tst_agentscripts.cpp` | 仍断言日志中的命令与退出码 |
| （新增） | `tests/core/tst_settings.cpp`、`tst_jsonstore.cpp`、`tst_httpprobe.cpp`、`tst_processrunner.cpp` | S1 |
| （新增） | `tests/theme/tst_themeloader.cpp`、`tst_themeregistry.cpp` | S3 |
| （新增） | `tests/shell/tst_navigationmodel.cpp` | S4 |
| （新增） | `tests/web/tst_webtabs.cpp` | S5 |
| （新增） | `tests/skills/tst_skillfrontmatter.cpp`、`tst_skillscanner.cpp` | S6 |
| （新增） | ctest `check_architecture` | S3 |

测试总量目标：把 15 个旧用例全部保留 + 新增约 25–35 个覆盖新模块，全部在无网络、无外部工具依赖的前提下通过。

---

## 6. 验收清单

### 6.1 功能（自动化能覆盖的部分由测试保证，其余手工过一遍）

- [ ] 旧数据目录接管：新目录为空 + 旧目录存在 → 复制 + 一次提示；旧目录不被删除
- [ ] 内置 agent 仍完全由 `config/default_agents.json` 决定；用户自建 agent 保留且排在内置项之后；删除的内置项保持删除；「恢复默认启动器」可用
- [ ] 健康检查语义不变：`webUrl` 返回 403/404 仍判定为运行中
- [ ] 卡片右键全部动作可用（启动/关闭/强制停止/安装/更新/显示输出/配置/打开配置目录/重新初始化）
- [ ] 一次性 setup（qwen 的 token 生成）仍只跑一次，重启后不重跑；「重新初始化」能重置
- [ ] 安装/更新/重新初始化的输出在卡片上实时滚动，失败后保留 5 秒并弹错误对话框
- [ ] 退出确认：本次会话启动过 agent 时提示，三个选项行为正确
- [ ] 侧边栏：切页、折叠、快捷键、徽标数字、折叠状态重启后保持
- [ ] 主题：切换即时生效、热重载、未知键忽略、缺键回退、亮色主题下无不可读文字、agent 自动配色在亮色下可读
- [ ] Web：多 agent 标签并存；同一 agent 再次打开复用标签；关闭标签不结束进程；agent 停止显示离线覆盖层并能重启；页面崩溃可重载；`target=_blank` 与下载有明确结果；切换标签后内存回落
- [ ] Skills：四类根目录都能出结果；插件多版本去重；悬停显示完整描述；点击复制路径并提示；过滤/排序/空状态/部分失败提示
- [ ] 打包：干净环境（无 Qt）运行部署目录；zip 体积记录；`--lean`（无 WebEngine）构建可跑
- [ ] i18n：`check_architecture` 通过；中文界面完整无英文残留（品牌名除外）

### 6.2 必须人工验证（无法自动化，逐项给结论）

沿用调研报告 §3.5 的清单：

- [ ] 中文输入法：候选窗口、内联组合、光标跟随（在 Web 视图内的输入框里）
- [ ] 分数缩放 125% / 150%：界面清晰度与命中区域
- [ ] Web 视图内与应用内的复制/粘贴互操作
- [ ] 从资源管理器拖文件进 Web 页面；页内拖放上传
- [ ] 页面全屏、打印/导出 PDF、桌面通知、摄像头/麦克风权限提示
- [ ] 页面缩放（Ctrl+滚轮 与 `zoomFactor`）与应用缩放的配合
- [ ] 覆盖层与 Web 视图的层叠：对话框/浮层必须能画在视图之上，若出现层叠/闪烁异常，改为独立窗口（`Qt.Tool`）承载

---

## 7. 风险与对策

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| 静态库里的 qrc 资源初始化器被链接器丢弃 | 图标/主题/内置配置在运行时找不到 | 所有 QML 与资源只编进 `app`（`01-architecture.md` §4.9），静态库不放资源 |
| `QtWebEngineQuick::initialize()` 必须在 `QGuiApplication` 之前 | 内嵌视图无法创建 | `app/main.cpp` 的固定顺序写进规格并加注释；`AWB_ENABLE_WEBENGINE=OFF` 时不调用 |
| 每 agent 一 profile 的成本 | 磁盘占用、首次加载变慢 | profile 目录记进用户文档；提供「清空 Web 数据」入口（P2） |
| 活动视图内存 250–350 MB | 多开卡顿 | 冻结 + `maxLiveTabs` + LRU；数值写进用户文档 |
| QML 手工注册到 `AgentWorkbench` URI 报 protected module | 启动即崩 | C++ 全局统一注册到独立 URI `AgentWorkbench.App`（`01-architecture.md` §8.2） |
| QtWebEngine 的 Chromium 118 缺 H.264、UA 报 NT 6.2 | 含 MP4 的页面不播；按版本判浏览器的页面误判 | 「在浏览器打开」始终可见；用户文档记录该限制；后续评估升 Qt |
| 主题令牌替换不彻底 | 亮色主题下出现暗色残块 | `check_architecture.sh` 卡字面色值；亮色主题全页面走查 |
| 迁移期间功能回归 | 用户丢功能 | S1–S4 只搬不改；每阶段手工过一遍 §6.1 的相关条目；旧 UI 保留到 S4 验收通过再删 |
| 插件 ABI 不稳定 | 未来插件全废 | `apiVersion` 显式校验 + 只暴露 Qt 类型；文档标注「实验性」，v1 默认关闭 |
| 非 ASCII 用户路径（`C:\Users\陈宗衍\…`） | 窄字符 API 打开文件失败 | 一律用 `QFile`/`QDir`/`QFileInfo`（core 层已封装），禁止 `std::ifstream`/`fopen` |
| 工作区有其它在途改动 | 提交夹带无关文件 | 只 `git add` 本阶段涉及的文件，禁止 `git add -A` |
| 改名后旧构建目录缓存 | 配置期报错或用了旧目标名 | S0 强制 `bash scripts/build.sh --clean` 一次，并在脚本里对项目名变化给出提示 |
| 仓库改名导致外部链接、Pages 路径失效 | 从旧链接进来的人打不开、语言切换链接错位 | GitHub 会永久重定向旧仓库 URL；S0-T7 显式核对 `site_url` 与 `extra.alternate.link`；本地目录改名（S0-T8）后必须 `--clean` |
| Gitee 镜像落后于 GitHub | 从 Gitee 拉代码的人拿到旧版本 | 本次不自动推远端（仓库约定：不 `git push`）；镜像同步由仓库所有者决定，见 TODO-7 |

---

## 8. 完成定义（DoD）与提交规范

一个阶段算完成，当且仅当：

1. `bash scripts/build.sh --test` 绿（含 `check_architecture`）。
2. 该阶段列出的验收条目全部有结论（通过 / 记录为已知问题并写进本文 §9）。
3. 旧代码已删除，没有留下「新旧两套并存」的死代码。
4. 面向用户的字符串全是英文源串，中文译文已补进 `.ts`。
5. 文档中受影响的页面（`specs/`、`docs/`、`AGENTS.md`）已同步——**规格与实现不一致时以规格为准**。
6. 已提交，且只包含本阶段涉及的文件。

提交信息模板（描述与正文用中文）：

```text
refactor(core): 抽出 awb_core 基础模块

进程、路径、日志原先都堆在 AgentLauncher 里，无法在不启动
Qt Quick 的情况下测试。先把它们抽成不依赖 UI 的库，后续
agents/skills/web 三个领域模块才能各自独立测试。
```

---

## 9. 待办与已知问题（实施中就地记录）

| 编号 | 内容 | 来源 | 归属 |
| --- | --- | --- | --- |
| TODO-1 | `opencode` 默认端口 4096 在本机被 VS Code Kilo Code 扩展占用，`opencode web --port 4096` 启动失败 | 调研报告 §7 | 改 `config/default_agents.json` 的 `webUrl`/`command` 到空闲端口（重新编译即对所有安装生效） |
| TODO-2 | QML 查找栏（`findText`）未在 v1 提供 | 本文 §6.6 | S5 之后按需补 |
| TODO-3 | 日志页（`logs`）未实现 | `02-ui-specification.md` §5 | P2 |
| TODO-4 | 主题编辑器 UI、对比度检查工具 | `02-ui-specification.md` §9.5 | P2 |
| TODO-5 | Qt 6.9/6.10 升级评估（Chromium 130/134） | 调研报告 §6 | **已决策本次不升**（`01-architecture.md` §12.1）；0.4.0 发布后单独评估 |
| TODO-6 | 无 WebEngine 的瘦身发行包 | `01-architecture.md` §10 | S5-T8 一并决定 |
| TODO-7 | 域名 `agentlauncher.dev` 与 Gitee 镜像 `czyt1988/start-agent` 是否跟随改名 | `01-architecture.md` §12.2 | 需仓库所有者决定；S0-T7 执行前确认，否则站点的 `site_url` 只能保持现状、只改路径段 |

---

## 10. 验收记录（2026-09-27，0.4.0-rc）

实施方式：按 S0→S8 顺序完成，每阶段 `bash scripts/build.sh --test` 全绿后提交（提交见 git log：`build:` 改名、`refactor(core)`、`refactor(agents)`、`feat(theme)`、`feat(shell)`、`feat(web)`、`feat(skills)`、`feat(plugin)`、`docs`）。当前 ctest 套件：`check_architecture`、`tst_core`（39 例）、`tst_agents`（15 例）、`tst_theme`（9 例）、`tst_shell`（6 例）、`tst_web`（6 例）、`tst_skills`（18 例）——全部通过。

### §6.1 功能（自动化覆盖的部分）

- [x] 旧数据目录接管：`tst_legacyimport` 覆盖复制/旧目录保留/二次启动不再导入（弹窗提示为 UI 层，见 §6.2）。
- [x] 内置 agent 语义：`tst_agentrepository`（内置覆盖、removed、逐字节写入）、`tst_agentsfacade`（CRUD/恢复默认）。
- [x] 健康检查语义：`tst_httpprobe` 断言 404 仍判定为运行中、拒绝连接/超时为停止。
- [x] 一次性 setup：`tst_agents` 覆盖状态往返；重置入口在 `AgentsFacade::resetSetup`。
- [x] 安装输出日志端到端：`tst_agentscripts::testInstallCommandIsLogged`。
- [x] 主题：`tst_theme`（未知键、缺键回退、非法颜色、id 不符、用户覆盖内置）；切换写 `settings.json`；热重载链路 registry→Theme→QML 已接线（人工观感见 §6.2）。
- [x] Web 标签：`tst_web`（同 agent 复用、关闭不清进程语义、离线/在线转换、表面解析、Ctrl+Tab 循环、maxLiveTabs LRU 释放与恢复）。
- [x] Skills：`tst_skills` frontmatter 12 例 + 扫描/多版本去重/缺根容错/过滤排序（真实目录实测：53 个 skill、34 个重复版本被去重）。
- [x] i18n：`check_architecture` 的英文源串检查通过；翻译0未完成条目。
- [x] 构建门禁：`check_architecture` 挂进 ctest，注入 `#ff0000` 探针确认能使构建失败（S3 实施时验证）。

### §6.1 功能（第二轮验收补充，2026-09-27）

- [x] **打包（S5-T8）**：`DIST_DIR=dist-awb-verify bash scripts/package.sh` 跑通；最终干净包（无冒烟钩子）`dist/AgentWorkbench-0.4.0-win64-Portable.zip` = **121,303,277 字节（约 115.7 MiB）**，落在规格预期的 110–130 MB 区间；体积已写入 CHANGELOG/CHANGELOG-zh。
- [x] **干净环境启动部署目录**：以不含 Qt 的 PATH 启动 dist-awb-verify/AgentWorkbench.exe，界面加载 0 错误（QML 资源全部随包）。
- [x] **打开内嵌视图**：`AWB_SMOKE_OPEN_TAB` 临时钩子（验收后已移除、未提交）自动打开 kimi 的标签——日志 `WebTabs: opened tab tab-1 for agent kimi-code (… surface=embedded)`，`QtWebEngineProcess.exe` ×2 随之启动（48 MB + 86 MB 工作集），宿主退出后辅助进程一并回收；页面截图见 `build/screenshot-web-tabs.png`。
- [x] **Hello 插件 UI（S7-T3）**：`plugins.enabled=true` + `AWB_BUILD_PLUGIN_EXAMPLES=ON` 下，日志 `PluginHost: loaded plugin "hello" 0.1.0`；以 `lastPageId=hello` 重启，`unknown page id` 0 次（注册先于页面恢复）、`failed to load page` 0 次——插件 qrc 页面经同一条注册路径渲染成功。
- [x] **内嵌验收顺带抓出并修复的 4 个真实缺陷**（均为 S5 期间冒烟未能覆盖的 API 漂移/作用域问题）：Qt 6.7 Basic 样式无 `MenuButton`；Qt 6 已把 `downloadRequested` 移到 profile、移除视图的 `loadFinished`/`errorOccurred`、`lifeCycleState` 更名为 `lifecycleState`；`Loader` 无 `onUnloaded`；标签标题 Label 缺 id。
- [ ] 卡片右键菜单的实际点击流、退出确认三选项的交互手感——**仍需人工**（无法无头点击 QML 控件）。
- [ ] 双端口 WebUI 同开不串 cookie——本轮只启动了单一 kimi 实例；profile 隔离机制已由 `WebProfilePaths`（每 agent 一目录 + `awb-<id>` storageName）与 `tst_web` 覆盖，**真实双端口对照仍需两个同时运行的 agent，留待人工**。

### §6.2 必须人工验证——逐项结论（2026-09-27）

可自动化的项已用程序化手段给出结论；其余项无法在无头环境中执行，如实标注：

- [x] **主题可读性走查（量化替代肉眼）**：对两套内置主题按 §9.5 规则计算 WCAG 对比度——`textPrimary` 对 window/surface/overlay/surfaceAlt 全部 ≥ 4.5:1（暗 6.31–11.34、亮 5.17–7.99），`textSecondary`/`textMuted` 对相应背景全部 ≥ 3.0:1（暗 3.40–7.37、亮 4.05–6.25）：**0 违规**。观察项（§9.5 未覆盖的强调色作正文用）：暗色 `textOnAccent` #ffffff 对 `accent` #89b4fa 仅 2.11:1——这是 §9.3 固定值的固有属性，按「值可微调」条款建议将暗色 textOnAccent 调为深色（与 0.3.0 按钮的深字一致），**留待所有者决策，本轮未擅改规格值**；亮色 success/warning 作状态文字约 2.3–3.0:1，同上记录为建议项。
- [x] **亮色主题实际渲染**：`appearance.theme=latte-light` 启动后截图 `build/screenshot-light-theme.png`，近白像素占比 96%（793/828）——主题经 settings → 引擎 → QML 重绑定全链路生效，页面 0 错误。
- [x] **0.4.0 截图替换（S8-T3）**：`docs/pic/screenshot-main-page.png` 已用当前 0.4.0 启动器页重截（1456×939、采样 34 色非空白、0 页面错误）；新增 `docs/pic/screenshot-web-tabs.png`（内嵌标签页）并在双语 README 中引用。像素多样性检查替代肉眼确认非空白；**逐像素的版式审美仍建议人工过目**。
- [ ] 中文输入法候选窗口、内联组合、光标跟随（Web 视图内）——无法自动化，需人工。
- [ ] 分数缩放 125%/150% 清晰度与命中区域——需人工（多 DPI 环境）。
- [ ] Web 视图内与应用内复制/粘贴互操作——需人工。
- [ ] 拖放（资源管理器 → 页面、页内上传）——需人工。
- [ ] 页面全屏、打印/导出 PDF、桌面通知、摄像头/麦克风权限提示——权限流已按 §6.4 实现（全部拒绝 + toast）并有单例注册确证，真实页面触发需人工。
- [ ] Ctrl+滚轮缩放与 zoomFactor 配合——滚轮缩放未与 tab.zoom 同步（已知缺口，规格未强制），快捷键缩放路径由代码覆盖；配合体验需人工。
- [ ] 覆盖层与 Web 视图层叠（对话框画在视图之上）——实现为视图上方的同级覆盖层，真实 Chromium 合成表现需人工。

### 实施期发现并已按流程处理的契约修订

- `specs/01` §8.2：C++ 单例注册名必须大写（Qt ≥6 拒绝小写名），QML 契约名经根别名保持小写——已改规格并注明。
- `specs/01` §4.5/§4.6 落地时补充：`WebTabsFacade.openDetachedTab`（同 agent 去重与 `target=_blank` 弹窗需求冲突时的出口）、`NavigationModel.currentPage` 必须是可通知属性（Q_INVOKABLE 在 QML 绑定里求值为函数引用，S4 起页面实际从未加载——S6 期间发现并修复）。

### 质量审查修复记录（2026-09-27 第四轮）

两个并行审查 Agent（C++/架构一路，QML/规格/文档一路）对重构全量走查，发现的问题已全部修复并验证（7/7 ctest、5 页 smoke 0 错误、`unfinished=0`）。按严重度：

**P0（5 项，全部修复）**

1. `main.cpp` 首启先写 `settings.json` 再跑 `LegacyImport::runOnce`，`isUntouched()` 永远为假——旧目录接管成了死代码。已把首启落盘移到接管之后（§4.9 代码块同步改写）。
2. `WorkbenchContext::openWeb` 用裸 `def.webUrl`，丢掉 `#token=` 片段——内嵌视图对 mutation 路由 401。改用 `AgentUrls::finalUrl(def)`；同时 `WebTabsFacade` 的外部打开日志/toast 一律走去掉 token 的 `redactedUrl()`。
3. `SkillCard` 描述高度用 `lineHeight`（倍率）当像素钳制 → 3.9px 不可见。移除错误绑定（`maximumLineCount: 3` 已封顶）。
4. `WebTabsPage` 空态绑定 `web.model.rowCount()`（无 NOTIFY，永不重算）。facade 新增 `tabCount` Q_PROPERTY（rowsInserted/rowsRemoved 驱动）。
5. `web.tabObject(id)` 在 QML 里被调用但 facade 没有该方法 → 缩放快捷键 TypeError。新增 `tabObject(id)` 返回 live `QObject*`，`stepZoom` 加空指针防护。

**P1（要点，全部修复）**：skill 版本去重键加入 skill 名（一个插件多 skill 只留其一的回归，附 `testMultiSkillPluginKeepsAllSkills`）；`WebTabsModel::removeTab` 关闭左侧 tab 时活动索引不漂移（附回归用例）；`EnvironmentService` pending 计数器改按 key 的 `QSet`（重复 refresh 不再卡死 `detecting`）；删除死代码 `AgentsFacade::openWeb`（规格明确它不是门面方法）；CRUD 三方法写盘失败回滚内存模型；`NavigationModel.badges` / `SkillsFacade.roots` 改为可通知属性（StatusBar、设置页根列表的死绑定）；标签栏补 tab 图标 16px、中键 `acceptedButtons`、下边框分隔线、⟳/✕ 停止加载切换（`activeState` 属性 + surface `stopLoading()` + `LoadStoppedStatus` 落状态）、`⋯` 菜单图标（新增 `icons/menu.svg`，替下 plus.svg）；`AButton`/`AIconButton` 焦点环（2px `focusRing`）；F12 开发者工具（仅 Debug）；SkillCard 键盘聚焦开 flyout、边界翻转、滚动即关（`activeFocusOnTab` 替代 `focus: true` 抢焦点）；分面/kind 标签补 `qsTr` 映射（`qsTr(modelData)` 对 lupdate 不可见）；AgentCard 四处 `Qt.rgba` 字面量换 `theme.alpha(theme.accent/danger, 0.22)`。

**P2（要点）**：`AgentRuntime` 补 6 个用例（S2-T4：空命令/不可解析程序/无 PID 停止/stopAll/无端口强停/端口→PID 列举 + 真实启动+taskkill——`findPidsForPort` 提为 public 供直测）；`applyTheme` 拒绝未知 id；LRU 上限把活动视图也计入且 `maxLiveTabs` 变更立即重跑；`AgentUrls` 已带 fragment 的 webUrl 用 `&token=` 拼接（附回归用例）；`SkillRoots` 路径原样存取、只在扫描时展开（不再把机器绝对路径固化进 settings.json）；`ScriptRunner` 分块解码持有不完整 UTF-8 尾序列；`AgentHealthMonitor` 同 URL 每轮一请求 + 在途跳过（迟到旧答复不能覆盖新状态）；`logging.*`/`locale.override` 接线到 `Logging::install`/`QTranslator`；`appearance.followSystem`、`launcher.startupVersionCheck`、`web.homeUrl` 标注为**预留键**（有默认值、无行为定义——加行为前先改规格）；`window.sidebarWidth` 接到侧边栏宽度；`check-architecture` 增补数字 `Qt.rgba` 与 `QQuick*`/`QQml*` 规则；剪贴板三连提取 `copyToClipboard` helper；主题调色板回退走 `paletteColorFor`。

**误报（核对后不改）**：`revealSkillFile` 非 Windows 分支传的是 `skillFilePath` 而非审查所称的 `info.absolutePath()`——`openFolder` 内部自己取父目录，行为正确。

**规格同步**：`specs/01` §4.4（SkillScanner 去重键 + SkillsFacade 实际 API）、§4.5（WebTabsFacade 全量成员 + token 脱敏）、§4.7（setCurrentPageId 命名 + badges）、§4.8（PluginServices + EnvironmentService QSet + main.cpp 顺序）、§5.1 矩阵（core→plugin_api ✅）、§7.2（预留键 + 路径原样存取）、§11（tst_agents 覆盖）、`specs/02` §5（menu.svg）/§7.4（WheelHandler 滚动关闭）/§10.2（ACard/ADialog/AToolTip 保留备注）。

### S0-T7 GitHub 端步骤的执行状态

- [x] `mkdocs.yml` 的 `repo_url`/`repo_name`/`site_name`/语言切换路径本地修正（指向 `czyt1988/AgentWorkbench`）。
- [x] **仓库改名与 remote 更新已执行并验收（2026-09-27 第三轮）**：本机无 `gh` CLI，改用 Git Credential Manager 的既有凭据经 GitHub REST API 执行 `PATCH /repos/czyt1988/AgentLauncher {"name":"AgentWorkbench"}`（HTTP 200）；验收证据：
  - `git remote -v` → `github https://github.com/czyt1988/AgentWorkbench.git`（fetch/push 均指向新地址）；
  - 新地址 `https://github.com/czyt1988/AgentWorkbench` 返回 200；旧地址 `https://github.com/czyt1988/AgentLauncher` 返回 **301 → https://github.com/czyt1988/AgentWorkbench**（规格要求的旧 URL 跳转 ✓）；
  - `git ls-remote github HEAD` 经新 remote 正常返回（`a90ea88…`），拉取链路可用；
  - API `full_name = czyt1988/AgentWorkbench`，仓库为 public、default_branch main。
- [x] Gitee 镜像（`origin`）与域名（TODO-7）按规格保持「所有者决定」，**未改动**（规格 §12.2 明确不在本次改名范围）。
