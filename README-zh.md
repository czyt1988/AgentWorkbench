# AgentWorkbench

一个用 **Qt6/QML + C++** 开发的 **AI 编码 agent 工作台**（原名 *AgentLauncher*）：左侧边栏 + 右侧工作区的外壳，可启动多个 AI 编码 agent（Kimi Code、OpenCode、Qwen Code、DeepSeek Harness）的 **Web 端**、以内嵌标签页使用它们的界面、浏览本机 Skill，并用 JSON 文件驱动全部配色。所有内容都**配置化驱动**——新增 agent 只需编辑一个 JSON 文件，无需改代码。

![平台: Windows · Linux · macOS](https://img.shields.io/badge/平台-Windows%20%7C%20Linux%20%7C%20macOS-blue)
![协议: MIT](https://img.shields.io/badge/协议-MIT-green)
![Qt6](https://img.shields.io/badge/Qt-6.5%2B-41cd52)

![AgentWorkbench 主界面](docs/pic/screenshot-main-page.png)

> 截图为 0.3.0 启动器界面（0.4.0 工作台界面的截图待更新）；应用自动跟随系统语言。

## 功能

- **工作台外壳**：侧边栏页面导航带徽标（`Ctrl+1…9` 切页、`Ctrl+B` 折叠、`Ctrl+,` 打开设置），工作区页面承载内容，状态栏显示 Python/Node 运行时徽标；窗口尺寸、侧边栏状态与上次页面跨重启恢复。
- **卡片网格启动器**：启动、安装、更新、版本号、右键菜单、HTTP 健康检查的运行态高亮，以及会话内停止（×）。
- **内嵌 Web 标签页**：agent 的 Web 界面在应用内打开（Qt WebEngine），每个 agent 独立持久 profile、失活冻结、超出 `maxLiveTabs` 按 LRU 释放、崩溃/离线/加载失败覆盖层——「在浏览器打开」始终一键可达；无 WebEngine 构建（`-DAWB_ENABLE_WEBENGINE=OFF`）自动降级为系统浏览器。

  ![内嵌 Web 标签页](docs/pic/screenshot-web-tabs.png)
- **Skill 浏览**：扫描常见 `SKILL.md` 目录（`~/.agents`、`~/.claude`、`~/.codex`、ZCode 插件缓存、项目目录），支持搜索、来源分面、排序、悬停详情与点击复制路径。
- **主题**：颜色与度量来自 JSON 主题文件；内置两套 Catppuccin 变体，自定义主题保存即热重载；ctest 中的 `check_architecture` 会拒绝 QML 里的字面颜色。
- **配置化驱动**：所有 agent、命令、URL、配置目录写在 `agents.json`，应用设置写在 `settings.json`。
- **实验性插件**：ABI 稳定的头文件接口 + 示例插件；默认禁用，设置页有总开关与进程内运行的信任警示（[文档](https://agentlauncher.dev/zh/plugins/)）。

## 默认支持的 agent

| Agent | 启动命令 | Web 地址 | 配置目录 |
|---|---|---|---|
| Kimi Code | `kimi web` | `http://127.0.0.1:58627` | `%USERPROFILE%/.kimi-code` |
| OpenCode | `opencode web --port 4096` | `http://127.0.0.1:4096` | `%USERPROFILE%/.config/opencode` |
| Qwen Code | `qwen serve` | `http://127.0.0.1:4170` | `%USERPROFILE%/.qwen` |
| OpenClaw | `openclaw gateway --port 18789` | `http://127.0.0.1:18789` | `%USERPROFILE%/.openclaw` |
| DeepSeek Harness | `dsh web` | `http://127.0.0.1:3080` | `%USERPROFILE%/.dsh` |

> OpenCode 默认使用随机端口，因此 AgentWorkbench 在默认配置里固定为 `4096`（`--port` 参数与 Web 地址同时固定），以保证健康检查和「浏览器打开」可靠；如需更改可在编辑对话框中修改。

## 构建

依赖：**Qt 6.5+**（含 `Core`、`Gui`、`Qml`、`Quick`、`QuickControls2`、`Network`，内嵌视图另需 `WebEngineQuick`）、**CMake 3.16+**、C++17 编译器（MSVC / GCC / Clang）。

Windows 下推荐直接运行 `scripts/build.sh`，它会自行定位 Qt 与 MSVC 工具链，一条命令完成配置与编译：

```bash
bash scripts/build.sh --test       # Debug 构建到 build/，随后运行单元测试
```

也可以手工执行 CMake 命令（需自行准备 MSVC 环境，例如在「x64 本机工具命令提示符」中运行）：

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

然后运行 `build/AgentWorkbench`（Windows 上为 `build/AgentWorkbench.exe`）。

## 配置

首次运行时 AgentWorkbench 创建自己的数据目录；从 AgentLauncher 升级时会先把旧 `~/.AgentLauncher` 的数据**复制**过来（旧目录保留不删）：

- Windows：`%USERPROFILE%\.AgentWorkbench\`
- Linux / macOS：`~/.AgentWorkbench/`

该目录存放 `agents.json`、`settings.json`、`agent_state.json`、`themes/`、`plugins/`、`webprofiles/` 与 `log/agentworkbench.log`。内置启动器的定义来自随包的 `config/default_agents.json`，每次启动重新应用，因此你的副本只承载自己新增的启动器（以及在设置页删掉的内置项）。

每条 agent 配置形如：

```json
{
    "id": "kimi-code",
    "name": "Kimi Code",
    "command": "kimi web",
    "webUrl": "http://127.0.0.1:58627",
    "configDir": "%USERPROFILE%/.kimi-code",
    "icon": "qrc:/icons/kimi-code.svg",
    "color": "#FF6B35"
}
```

| 字段 | 用途 |
|---|---|
| `id` | 稳定标识 |
| `name` | 卡片标题 |
| `command` | **启动**按钮执行的 shell 命令（以独立进程运行） |
| `webUrl` | 用于健康检查并打开（内嵌或浏览器）的地址 |
| `configDir` | 卡片菜单打开的目录（支持 `%VAR%` 展开） |
| `icon` | 图标资源路径 |
| `color` | 运行中时的高亮颜色 |

`settings.json`、主题、Skill 根目录与 Web 选项详见[配置指南](https://agentlauncher.dev/zh/configuration/)。

## 文档

完整文档（英文 + 中文）使用 [MkDocs](https://www.mkdocs.org/) 与 [Material](https://squidfunk.github.io/mkdocs-material/) 主题构建：

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

## 贡献

欢迎提交 PR。请把 agent 定义放在 `agents.json` 中，不要硬编码进 C++。重构规格在 [`specs/`](specs/) 目录，是模块边界与 UI 契约的权威来源；构建命令与项目约定见 [AGENTS.md](AGENTS.md)。

## 协议

[MIT](LICENSE) © AgentWorkbench Contributors
