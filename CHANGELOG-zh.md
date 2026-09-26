# 变更日志

本文档记录 **AgentLauncher** 的所有重要变更。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，本项目遵循[语义化版本](https://semver.org/lang/zh-CN/spec/v2.0.0.html)。

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

[0.3.0]: https://github.com/czyt1988/AgentLauncher/releases/tag/v0.3.0
[0.2.0]: https://github.com/czyt1988/AgentLauncher/releases/tag/v0.2.0
