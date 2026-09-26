# AgentLauncher

一个用 **Qt6/QML + C++** 开发的桌面小工具，让你能从同一个卡片网格中快速启动多个
AI 编码 agent 的 **Web 端**（Kimi Code、OpenCode、Qwen Code、DeepSeek Harness），并一键打开它们的
配置目录。所有内容都**配置化驱动**——新增 agent 只需编辑一个 JSON 文件，无需改代码。

![平台: Windows · Linux · macOS](https://img.shields.io/badge/平台-Windows%20%7C%20Linux%20%7C%20macOS-blue)
![协议: MIT](https://img.shields.io/badge/协议-MIT-green)
![Qt6](https://img.shields.io/badge/Qt-6.5%2B-41cd52)

![AgentLauncher 主界面](docs/pic/screenshot-main-page.png)

## 功能

- 首页以**卡片网格**展示已配置的 agent。
- 每张卡片有**启动**按钮（启动 agent 的 web 服务）和**配置**按钮（进入该 agent 设置页）。
- 通过 HTTP 健康检查检测**已启动状态**；运行中的卡片用 agent 专属颜色做边框高亮 + 背景着色。
- 点击**已启动**的卡片（或其「打开」按钮）即用默认浏览器打开该 agent 的 web 端。
- 运行中的卡片右上角显示**停止**按钮（×），可结束本次会话内启动的 agent。
- 右上角**运行时版本徽标**显示检测到的 Python 与 Node.js 版本；若缺失会以红色提示，方便判断哪些 agent 可能无法运行。
- 卡片左上角显示 agent 的**版本号**（当配置了 `versionCommand` 时）。
- **配置页**：可编辑启动命令（例如改 `--port`）、编辑 web 地址，并一键在文件管理器中打开 agent 的配置目录。
- **配置化驱动**：所有 agent、命令、URL、配置目录都写在 `agents.json` 里。新增 agent 只需往文件里加一个对象。

## 默认支持的 agent

| Agent | 启动命令 | Web 地址 | 配置目录 |
|---|---|---|---|
| Kimi Code | `kimi web` | `http://127.0.0.1:58627` | `%USERPROFILE%/.kimi-code` |
| OpenCode | `opencode web --port 4096` | `http://127.0.0.1:4096` | `%USERPROFILE%/.config/opencode` |
| Qwen Code | `qwen serve` | `http://127.0.0.1:4170` | `%USERPROFILE%/.qwen` |
| OpenClaw | `openclaw gateway --port 18789` | `http://127.0.0.1:18789` | `%USERPROFILE%/.openclaw` |
| DeepSeek Harness | `dsh web` | `http://127.0.0.1:3080` | `%USERPROFILE%/.dsh` |

> OpenCode 默认使用随机端口，因此 AgentLauncher 在默认配置里固定为 `4096`
> （`--port` 参数与 Web 地址同时固定），以保证健康检查和「浏览器打开」可靠。
> 如需更改可在配置页修改。

## 构建

依赖：**Qt 6.5+**（含 `Core`、`Gui`、`Qml`、`Quick`、`Network`）、**CMake 3.16+**、
C++17 编译器（MSVC / GCC / Clang）。

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

然后运行 `build/AgentLauncher`（Windows 上为 `build/AgentLauncher.exe`）。

## 配置

首次运行时，AgentLauncher 会把内置默认配置拷贝到 `~/.AgentLauncher/`：

- Windows：`%USERPROFILE%\.AgentLauncher\agents.json`
- Linux：`~/.AgentLauncher/agents.json`
- macOS：`~/.AgentLauncher/agents.json`

该目录同时存放 `agent_state.json` 与 `log/agentlauncher.log`。内置启动器的定义来自
随包内置的 `config/default_agents.json`，每次启动都会重新应用，因此主目录下的副本只
承载你自己新增的启动器（以及你在设置页里删掉的内置项）。

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
| `webUrl` | 用于健康检查并在浏览器打开的地址 |
| `configDir` | 配置页「打开」按钮打开的目录（支持 `%VAR%` 展开） |
| `icon` | 图标资源路径 |
| `color` | 运行中时的高亮颜色 |

详见[配置指南](https://agentlauncher.dev/zh/configuration/)。

## 文档

完整文档（英文 + 中文）使用 [MkDocs](https://www.mkdocs.org/) 与
[Material](https://squidfunk.github.io/mkdocs-material/) 主题构建：

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

## 贡献

欢迎提交 PR。请把 agent 定义放在 `agents.json` 中，不要硬编码进 C++。
构建命令与项目约定见 [AGENTS.md](AGENTS.md)。

## 协议

[MIT](LICENSE) © AgentLauncher Contributors
