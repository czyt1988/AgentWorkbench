# 变更日志

本文档记录 **AgentWorkbench**（0.3.0 及以前为 AgentLauncher）的所有重要变更。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，本项目遵循[语义化版本](https://semver.org/lang/zh-CN/spec/v2.0.0.html)。

## [未发布]

### 新增

- **Agent Tools 页**：面向 agent CLI 用户的提示词编写台——左侧起草提示词
  （回车只换行，本页从不发送任何内容；Copy 把草稿放进剪贴板），右侧浏览
  工作区。最多记忆 20 个常用工作区文件夹（MRU、逐项移除、系统文件夹选择
  器），当前工作区渲染为懒加载文件树，带自动刷新（QFileSystemWatcher 防抖）
  与手动刷新；把树的条目拖进编辑区（插入到落点光标处）或双击文件行，即可
  插入 `` `./相对路径` `` 形式的文件引用。草稿跨页面切换与重启保留
  （`tools.json`）。
- **Web 页「主页」按钮**：标签栏工具栏新增主页入口，可回到运行中 agent 列
  表（无标签页状态）而不关闭任何已开标签——此前视图打开后就回不去列表，
  后续启动的 agent 无法从 Web 页打开。点击标签、从列表打开 agent 或
  `Ctrl+Tab` 轮换即离开该列表。
- **启动器卡片拆分按钮**：agent 运行中，"Open" 在**应用内打开 WebUI 并跳转
  到 Web 页**（此前标签在后台打开，还要手动切页）；旁边的箭头提供「在浏览
  器打开」（右键菜单亦有）。
- `tst_workbench` 测试目标：覆盖 `openWeb` 跳页契约、external surface 不建
  标签的路径与新的浏览器打开意图。

### 修复

- **切换 Web 标签不再整页重绘。**两个冻结缺陷：切走时仍在加载的视图被冻结
  （Chromium 会挂起冻结页的 JS，加载停滞，回来时加载遮罩闪盖在已渲染内容
  上）；error/offline 状态变化尝试冻结**活动**标签（被 Qt 以"页面可见"拒
  绝，标签卡死）。生命周期绑定现在只冻结非活动的、已稳定的标签——活动标
  签与加载中的视图一律不冻。`web.freezeInactiveTabs` 同时改为默认**关闭**：
  恢复冻结的 SPA 有可见重绘，Chromium 本就节流隐藏视图，需要省 CPU 的场景
  再显式开启。`maxLiveTabs` 的 LRU 释放不再依赖该设置，内存始终有界。
- **Web 标签按钮渲染成白块**：tab delegate 的 `required property string
  color`（匹配模型的 `color` role）遮蔽了 delegate Rectangle 的 `color`，
  主题绑定落到字符串上，标签底色画成默认白、文字几乎不可见。delegate 根改
  为 Item + 内层背景 Rectangle（AgentCard 的既有模式）。
- **Web 页运行中 agent 列表行坍塌**：`AListRow` 只有显式 `height` 没有
  `implicitHeight`，布局驱动的列表把行压到 ~0——"Open" 按钮只剩窄条、第二
  行不可见、明明有 agent 在跑却显示 "No agent is running"。`AListRow` 现在
  把高度镜像进 `implicitHeight`（所有布局用法受益），运行列表也不再自设行
  高。

### 移除

- `examples/plugins/hello` 示例插件与 `AWB_BUILD_PLUGIN_EXAMPLES` 构建选项；
  插件文档改用内联示例。

## [0.4.0] - 2026-09-27

本次版本把 AgentLauncher 重构为 **AgentWorkbench**：从单页卡片网格升级为「侧边栏 + 工作区」的工作台外壳，并新增内嵌 Web 标签页、Skill 浏览、配置文件驱动的主题与实验性插件支持。

### 新增

- **工作台外壳**：左侧边栏（页面导航、徽标、折叠）+ 右侧工作区 + 状态栏。`Ctrl+1…9` 按序切页、`Ctrl+B` 折叠侧边栏、`Ctrl+,` 打开设置；窗口尺寸、侧边栏状态与上次页面写入 `settings.json`，重启后恢复。
- **内嵌 Web 标签页**：agent 的 Web 界面可以在应用内以标签页打开（Qt WebEngine），支持多 agent 并存、失活冻结、超出 `maxLiveTabs` 后按 LRU 释放视图（标签保留、点击恢复）、崩溃/加载失败/离线三态覆盖层与 `Ctrl+W`/`F5`/`Ctrl+Tab`/缩放快捷键。**每个 agent 一个持久 profile**——Chromium 的 cookie 按主机索引并忽略端口，共用 profile 会让不同端口的本地服务互相串会话。「在浏览器打开」在任何情况下都是一等公民；构建开关 `AWB_ENABLE_WEBENGINE=OFF` 或设置中的外部表面会整体降级为系统浏览器。
- **Skill 浏览**：扫描 `~/.agents/skills`、`~/.claude/skills`、`~/.codex/skills`、ZCode 插件缓存与项目目录，解析 `SKILL.md` 的 frontmatter；卡片支持搜索、来源分面、排序、悬停详情、点击复制路径。插件缓存的多版本只保留最高版本。
- **主题引擎**：颜色与度量全部来自 JSON 主题文件（内置 Catppuccin Mocha 深色与 Latte 浅色两套），运行时可切换，保存主题文件即时热重载；`scripts/check-architecture.sh` 把「QML 不得出现字面颜色、依赖方向、英文源串」等规则挂进 ctest，违反即构建失败。
- **实验性插件**：`awb_plugin_api` 头文件接口 + 宿主发现/加载 + 示例插件。插件与内置功能走同一条页面注册路径；默认禁用，设置页有总开关与逐项开关及进程内运行的信任警示，重启生效。详见 [插件](plugins.md)。
- **设置页分组**：外观（主题）、启动器、环境（Python/Node 检测）、Skill 根目录管理、Web 表面与 Chromium 参数、插件、高级（数据目录、恢复默认）。

### 变更

- **产品改名为 AgentWorkbench**：可执行文件、窗口标题、日志文件名（`agentworkbench.log`）、数据目录（`~/.AgentWorkbench`）全部换新；首次启动会把旧 `~/.AgentLauncher` 的配置**复制**过来（旧目录保留），并弹一次提示。`agents.json` 根级 `title` 字段停用，窗口标题改由设置页的 `settings.json` `window.title` 控制。
- **代码分层**：扁平的 `src/` 拆为 `core` / `theme` / `agents` / `shell` / `skills` / `web` / `workbench` 模块与 `app/` 组装层，领域模块之间零依赖，跨域行为集中在应用层；每个模块有独立的测试目标（tst_core、tst_agents、tst_theme、tst_shell、tst_web、tst_skills），15 个旧用例全部迁移保留。
- **设置文件 `settings.json`**：窗口、外观、locale、启动器健康检查、Web、Skill、日志、插件八组键位就地取默认值，没有迁移代码。
- **打包**：`scripts/package.sh` 的 windeployqt 扫描 `src/`（旧 `qml/` 目录已删除），随包 QML 全部编入可执行文件。

### 修复

- QML 单例类型名必须大写（Qt ≥ 6 拒绝小写名导致界面加载失败）：C++ 注册名大写、QML 契约名经窗口根别名保持小写。
- 导航的 `currentPage` 由可通知属性暴露：此前它是 Q_INVOKABLE，QML 绑定求值为函数引用，工作区页面实际从未加载。
- 质量审查轮（5 项 P0 + 约 25 项 P1）：首启写 `settings.json` 不再先于旧目录接管检查（否则一次性接管 `~/.AgentLauncher` 永远不会发生）；`workbench.openWeb` 保留 `#token=` 片段（否则内嵌视图对 mutation 路由 401），日志与 toast 一律脱敏；Skills 卡片描述不再被钳到约 4px 高而不可见；Web 空态与缩放快捷键恢复工作（`tabCount`/`tabObject` 成为真实的可通知 API）；插件 skill 去重保留胜出版本的**全部** skill；关闭活动标签左侧的标签不再使活动标签漂移；状态栏运行数/标签数徽标随变化重绑；标签栏补齐图标、中键关闭、下边框分隔线、⟳/停止加载切换与 `⋯` 菜单图标；按钮显示键盘焦点环；`F12` 打开开发者工具（仅 Debug 构建）；Skills 悬停卡支持键盘聚焦打开、窗口边界翻转、滚动即关；分面/kind 标签可翻译；启动时接线 `logging.*` 与 `locale.override`。
- **三个 C++ 方法缺 `Q_INVOKABLE`，QML 调用即抛「…is not a function」且操作静默失效**（手工运行发现）：侧边栏点击与 `Ctrl+1…9` 切页（`setCurrentPageId`）、设置页表面切换（`setWebSurface`）、Chromium flags 编辑（`setWebChromiumFlags`）——三者自 S4 起即坏，因页面冒烟从不点击而直到人工运行才暴露。已全部补 `Q_INVOKABLE`；`check-architecture` 新增规则5（QML 单例方法调用必须 `Q_INVOKABLE`、属性赋值必须有 `WRITE`，正负向实测有效）；`tst_shell::testQmlCalledMethodsAreInvokable` 经 meta-object 真实调用三方法防回归。

### 打包

- `scripts/package.sh` 产出 `dist/AgentWorkbench-0.4.0-win64-Portable.zip`，**实测体积 121,303,277 字节（约 115.7 MiB / 121.3 MB）**，在预期的 110–130 MB 区间内（含 Qt WebEngine 的 Chromium 运行时）。
- 部署目录已在「PATH 不含 Qt」的干净环境下启动验证：界面加载 0 错误；自动打开一个内嵌标签后 `QtWebEngineProcess` 辅助进程随之启动、标签日志记录 `opened tab … surface=embedded`，宿主退出时辅助进程一并回收。

### 已知限制

- 内嵌引擎为 Qt 6.7.3 自带的 Chromium 118：不支持 H.264/MP4 播放，UA 误报 `Windows NT 6.2`；受影响页面用「在浏览器打开」绕行（Qt 与 WebEngine 的升级评估待定）。
- 中文输入法候选框、分数缩放清晰度、拖放等体验项需要人工验收。

## [0.3.0] - 2026-09-10

本次发布加入了应用内的启动器管理，让便携版构建自包含，并把所有用户数据集中到一个目录。

### 新增

- **设置页与应用内启动器管理**：右下角齿轮按钮打开设置页，列出所有启动器及其运行状态。可直接在其中新增、编辑、删除启动器（含内置项），并提供**恢复默认启动器**操作，无需再手工编辑 `agents.json`。
- **启动器编辑器**（`AgentEditPage.qml`，取代 `ConfigPage.qml`）：覆盖全部字段的新增/编辑表单，含必填标记、逐字段提示、图标与颜色实时预览以及输入校验。
- **删除的内置项不再复活**：`agents.json` 新增可选的根级 `removed` 数组，记录在设置页删除的内置 agent id，随包默认配置不会在下次启动时把它们加回来。
- **界面加载失败诊断**：QML 界面无法加载时，记录 import 搜索路径以及随包部署的 QML 模块是否存在于磁盘，并弹出原生对话框指明日志文件位置，不再静默退出。

### 变更

- **用户数据迁移到 `~/.AgentLauncher/`**（Windows：`%USERPROFILE%\.AgentLauncher\`）：`agents.json`、`agent_state.json` 与日志目录现在集中存放，不再分散于平台配置目录与用户主目录两处。
- **便携版构建自包含**：`scripts/package.sh` 生成 `qt.conf`，把 Qt 前缀固定到可执行文件所在目录；应用优先使用随 exe 部署的 QML 模块，而非机器上已有的 Qt 安装。修复了在自带 Qt 的机器上出现「module … is not installed」的问题。
- **打包脚本**：版本号从 `CMakeLists.txt` 读取（仍可用 `VERSION` 覆盖），压缩包命名为 `AgentLauncher-<版本>-win64-Portable.zip`；项目目录被移动或改名后遗留的 CMake 缓存会被自动识别并清理。
- **内置启动器由随包配置定义**：每次启动都会用内置的 `config/default_agents.json` 重新应用所有内置 agent，`~/.AgentLauncher/` 里的配置只承载你自己新增的启动器和删除记录。调整内置启动器 = 改那份文件并重新编译，不涉及用户数据迁移或旧配置兼容逻辑。因此在设置页里编辑内置项只对当次运行有效，下次启动会被默认配置覆盖。
- **agent 安装命令**：Kimi Code 改为安装 `@moonshot-ai/kimi-code`（原为 `@kimi-code/cli`）；其余内置 agent 的安装命令固定为 `@latest`。

### 修复

- **单元测试不再触碰真实用户配置**：Qt 的测试模式不会重定向 `HomeLocation`，因此在数据目录迁移后，测试套件会读写开发者真实的 `agents.json`，并留下测试用的启动器。现在数据目录会遵循测试模式。
- **图标往返一致**：`file://` 图标 URL 会原样通过 `resolveIcon()`，本地解析出的图标在保存并重新加载后不再退化为默认图标。

## [0.2.0] - 2026-08-17

AgentLauncher 的首个正式版本化发布——一个基于 Qt6/QML + C++ 的桌面应用，可从一个卡片网格中启动各类 AI 编码助手的 Web UI。全部配置驱动：agent 定义存放在 `agents.json` 中，而非硬编码在 C++ 里。

### 新增

- **卡片网格主界面**：展示所有已配置的 agent，每张卡片都有「启动」按钮（启动 agent 的 Web 服务）和「配置」按钮（打开 agent 的设置页）。
- **HTTP 健康检查状态检测**：运行中的卡片会用 agent 自身的颜色高亮并加彩边；只要收到任何 HTTP 响应即视为运行中，连接被拒/超时即视为已停止。
- **停止按钮（×）**：仅对本会话启动的 agent 显示。`launch()` 会记录 `startDetached` 返回的 PID，`stop()` 通过 `taskkill /F /T /PID`（Windows）终止整个 `cmd → .cmd → node` 进程树。
- **`launching` 过渡状态**：带 30 秒安全超时，卡片在健康检查确认服务上线前一直显示旋转动画。
- **启动错误反馈**：`launchFailed` 信号会触发卡片原位红色闪烁，并弹出可滚动、等宽字体的居中错误对话框。
- **可配置的 agent 图标与颜色**（通过 `agents.json`）：图标支持 `qrc:/` 资源、本地文件路径（展开 `%VAR%` 与 `~`）、`http(s)://` URL，或留空（回退到内置默认图标）；颜色留空时按内置 Catppuccin Mocha 调色板自动分配，可选的 `cardColor` 设定非运行态卡片背景色。
- **四个中性内置图标**：`default`、`terminal`、`cube`、`bot`。
- **安装 / 更新 / 版本支持**：每个 agent 可声明 `installCommand`、`updateCommand` 与 `versionCommand`；卡片版本标签会显示已安装的 agent 版本。
- **命令输出流式显示到卡片**：当 agent 正在安装、更新或执行一次性设置时，状态行下方会出现可滚动的等宽控制台，实时显示 stdout/stderr。成功时隐藏，失败时保留 5 秒，可手动关闭，并可通过右键「显示输出」重新打开。
- **一次性 `setupCommand`**：在 agent 首次启动前运行（例如为 `qwen serve` 生成 bearer token）。退出码为 0 时结果会持久化到 `agent_state.json`，且不再重跑——除非用户从卡片右键菜单选择「重新初始化」。
- **Bearer token 鉴权**：通过 `tokenFile` 字段实现（Qwen Code）——启动时将 token 设为 `QWEN_SERVER_TOKEN` 环境变量，并作为 `#token=<value>` 追加到 Web URL。
- **强制停止**右键菜单项：杀死监听 agent Web 端口的进程，即使该进程不是本启动器启动的。
- **右上角运行时版本徽章**：检测到的 Python 与 Node.js 版本以绿色徽章显示；缺失的运行时显示红色 ×，并在提示中说明受影响的 agent 可能无法工作。
- **窗口标题配置**：`agents.json` 根对象可选的 `title` 字段可覆盖应用窗口标题；留空或缺失时回退为 `AgentLauncher`。
- **滚动文件日志**：所有 Qt 日志输出写入 `~/.AgentLauncher/log/agentlauncher.log`，达到 10 MB 时滚动，保留 2 个文件（当前 + 1 个备份）。现有 `qWarning()` 调用会被自动捕获。
- **关闭窗口确认**：当已启动过 agent 时，关闭窗口会提示是否终止本会话启动的所有后台进程。
- **默认 agent**：打包的 `default_agents.json` 内含 Kimi Code、OpenCode、Qwen Code、OpenClaw 与 DeepSeek Harness。
- **Windows 打包脚本**（`scripts/package.sh`）：一条命令完成 Release 构建 + `windeployqt`。可在资源管理器中双击运行——当 MSVC 环境缺失时自动加载 `vcvars64.bat`，始终 cd 到项目根目录，并使用显式的 `windeployqt.exe` 路径。
- **应用图标**（`app.rc`、`app-icon.png`）。
- **国际化（i18n）**：源字符串为英文；`QTranslator` 会根据系统语言自动加载中文（`agentlauncher_zh_CN.qm`）。`.ts` 源文件由 `lupdate` 同步并编译为 `.qm`，内嵌于 `:/i18n/`。
- **文档站点**（MkDocs + Material，英文 + 中文），含配置指南，README 与文档中附主界面截图。

### 变更

- **启动可靠性**：裸命令通过 `QStandardPaths::findExecutable` 解析（会应用 PATHEXT），`.cmd`/`.bat` 垫片经 `cmd /c` 运行，使 npm 风格的 agent（如 `qwen.cmd`）能正确启动——单靠 `CreateProcess` 找不到它们。
- **简化 Qwen Code 命令**为 `qwen serve`；bearer token 改由 `tokenFile` + `setupCommand` 处理，不再内联。
- **上下文菜单稳定性**：强制停止与重新初始化项改用 `enabled`（置灰）而非 `visible`，菜单不再随状态变化伸缩——与更新/安装、显示输出项保持一致。
- **卡片布局**：按钮行锚定到卡片矩形底部，消除了原先由顶部堆叠 Column 内容留下的底部大面积空白。
- **版本检查体验**：设 500 毫秒最小旋转时长以确保指示器始终可见；`checkingVersion` 在 QML 渲染前初始化，使卡片从第一帧就显示旋转动画；读取 stderr 作为版本解析的回退；退出码非零但能解析出版本字符串时仍视为已安装。

### 修复

- **ConfigPage 属性名冲突**：`data` 属性重命名为 `agentData`，避免与 `QQuickItem.data` 同名遮蔽——此前子绑定解析到了错误对象，导致字段为空。
- **卡片 Flow 溢出**：第 4 张卡片（OpenClaw）会溢出到右边缘，因为 `ColumnLayout` 宽度绑定到了未定义的 `parent.availableWidth`（ScrollView 内部 Flickable 没有该属性）；改绑到 `scrollView.availableWidth` 后卡片正常换行并随窗口缩放重排。
- **安装/更新状态卡死**：移除了安装/更新命令末尾的 `& pause`（它会等待按键，导致 `QProcess::finished` 永不触发，卡片停留在「安装中…」）；增加了运行保护（agent 运行时拒绝执行），并修正了成功/失败两种情况下 `installFinished` 信号的发射。
- **停止按钮状态**：`stop()` 失败时现在会复位 `stopping` 状态，按钮不再卡住。

[0.3.0]: https://github.com/czyt1988/AgentWorkbench/releases/tag/v0.3.0
[0.2.0]: https://github.com/czyt1988/AgentWorkbench/releases/tag/v0.2.0
