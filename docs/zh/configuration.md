# 配置

AgentLauncher 采用配置化驱动。所有 agent 定义都放在单个 `agents.json` 文件中，首次
运行时拷贝到 `~/.AgentLauncher/`：

| 系统 | 路径 |
|---|---|
| Windows | `%USERPROFILE%\.AgentLauncher\agents.json` |
| Linux | `~/.AgentLauncher/agents.json` |
| macOS | `~/.AgentLauncher/agents.json` |

所有用户数据都集中在同一个目录：`agents.json`、`agent_state.json`（一次性设置状态）
以及 `log/agentlauncher.log`。0.3.0 之前的版本把配置存放在
`%LOCALAPPDATA%\AgentLauncher\` / `~/.config/AgentLauncher/`；新版本首次启动时会把这些
文件复制到 `~/.AgentLauncher/`（旧文件保留在原处），若新目录中已存在配置则不会覆盖。

应用内置了一份默认配置（来自 `config/default_agents.json`）。

## agent 条目

每个 agent 是 `agents` 数组中的一个 JSON 对象：

```json
{
    "id": "kimi-code",
    "name": "Kimi Code",
    "command": "kimi web",
    "webUrl": "http://127.0.0.1:58627",
    "configDir": "%USERPROFILE%/.kimi-code",
    "icon": "qrc:/icons/kimi-code.svg",
    "color": "#FF6B35",
    "cardColor": "",
    "installCommand": "npm install -g @kimi-code/cli",
    "updateCommand": "npm update -g @kimi-code/cli",
    "versionCommand": "kimi --version",
    "setupCommand": ""
}
```

### 字段说明

| 字段 | 必填 | 用途 |
|---|---|---|
| `id` | 是 | 稳定标识，UI 据此定位 agent |
| `name` | 是 | 卡片标题 |
| `command` | 是 | **启动**按钮执行的 shell 命令（以独立进程运行） |
| `webUrl` | 是 | 每隔 3 秒做健康检查、点击卡片时在浏览器打开的地址 |
| `configDir` | 否 | 配置页「打开」按钮打开的目录；支持 `%VAR%` 展开 |
| `icon` | 否 | 图标路径，详见下方[图标配置](#图标配置) |
| `color` | 否 | 强调色：运行态边框、背景着色、按钮、状态文字。留空 = 从内置色卡自动分配（见下方） |
| `cardColor` | 否 | 非运行态的卡片背景色。留空 = 默认 `#313244` |
| `installCommand` | 否 | 「安装」菜单项执行的命令 |
| `updateCommand` | 否 | 「更新」菜单项执行的命令 |
| `versionCommand` | 否 | 启动时运行的命令，用于检测 agent 是否已安装并解析版本号 |
| `setupCommand` | 否 | 首次启动前运行的一次性命令（详见[设置命令](#设置命令)） |

## 图标配置

`icon` 字段支持三种写法：

### 1. 内置资源路径

使用 `qrc:/icons/<名称>.svg` 引用程序内置的图标：

```json
"icon": "qrc:/icons/kimi-code.svg"
```

**内置图标列表：**

| 路径 | 说明 |
|---|---|
| `qrc:/icons/default.svg` | 中性终端提示符图标（`icon` 留空时也使用此图标） |
| `qrc:/icons/terminal.svg` | 终端窗口图标 |
| `qrc:/icons/cube.svg` | 立方体图标 |
| `qrc:/icons/bot.svg` | 机器人头像图标 |
| `qrc:/icons/kimi-code.svg` | Kimi Code 品牌图标 |
| `qrc:/icons/opencode.svg` | OpenCode 品牌图标 |
| `qrc:/icons/qwen-code.svg` | Qwen Code 品牌图标 |
| `qrc:/icons/openclaw.svg` | OpenClaw 品牌图标 |
| `qrc:/icons/deepseek-harness.svg` | DeepSeek Harness 品牌图标 |

### 2. 本地文件路径

指向磁盘上的任意 SVG 或 PNG 文件。环境变量（`%VAR%`）和 `~` 会自动展开：

```json
"icon": "C:/Users/me/icons/my-agent.svg"
"icon": "%USERPROFILE%/icons/my-agent.png"
"icon": "~/Pictures/agent-logo.svg"
```

如果文件不存在，卡片会回退到默认图标。

### 3. 留空 / 不填

留空或省略 `icon` 字段即可使用内置默认图标：

```json
"icon": ""
```

等同于 `"icon": "qrc:/icons/default.svg"`。

### 远程 URL

也支持 HTTP/HTTPS 网址：

```json
"icon": "https://example.com/icon.svg"
```

## 颜色配置

两个颜色字段控制卡片外观：

### `color`（强调色）

agent 的主强调色，用于：

- 运行态的边框和着色背景
- 启动/打开按钮背景
- 状态指示圆点
- 活动态状态文字

```json
"color": "#FF6B35"
```

**可选。** 如果 `color` 留空或省略，会根据 agent 在列表中的位置从内置
色卡中循环取色并自动分配。色卡使用 Catppuccin Mocha 配色：

| 序号 | 颜色 | 名称 |
|---|---|---|
| 0 | `#f38ba8` | 红 |
| 1 | `#fab387` | 桃 |
| 2 | `#f9e2af` | 黄 |
| 3 | `#a6e3a1` | 绿 |
| 4 | `#94e2d5` | 青 |
| 5 | `#89b4fa` | 蓝 |
| 6 | `#cba6f7` | 紫 |
| 7 | `#f5c2e7` | 粉 |

第一个未指定颜色的 agent 分到红色，第二个分到桃色，依此类推。粉色之后循环
重复。分配的颜色在首次运行时写入 `agents.json`，因此重启后颜色保持稳定。

### `cardColor`（卡片背景色）

可选。控制 agent **非运行态**的卡片背景色。留空或省略时使用默认值
`#313244`（Catppuccin Mocha Surface1）。

```json
"cardColor": "#1a1a2e"
```

运行态下，背景会切换为 `color` 的着色版本（16% 透明度），与 `cardColor` 无关——
这提供了清晰的运行态视觉信号。

### 示例：自定义主题卡片

```json
{
    "id": "my-agent",
    "name": "My Agent",
    "command": "my-agent serve --port 3000",
    "webUrl": "http://127.0.0.1:3000",
    "icon": "qrc:/icons/cube.svg",
    "color": "#00d4aa",
    "cardColor": "#0d2818"
}
```

## 环境变量展开

`configDir` 和 `icon` 字段支持环境变量展开：

- `%VAR%` — Windows 风格（如 `%USERPROFILE%`、`%LOCALAPPDATA%`）
- `~/` — 展开为用户主目录

```json
"configDir": "%USERPROFILE%/.my-agent",
"icon": "%USERPROFILE%/icons/my-agent.svg"
```

## 设置命令

`setupCommand` 字段可选。它持有首次启动前运行的一次性命令（例如为
`qwen serve` 生成 bearer token）。

- 如果 `setupCommand` 以退出码 0 结束，结果会持久化到
  `~/.AgentLauncher/agent_state.json`，之后不再重复运行，除非用户从卡片右键菜单选择
  **重新初始化**。
- 如果 `setupCommand` 以非零退出码结束，会触发 `launchFailed` 并显示捕获
  的输出，agent 不会启动。
- 留空的 `setupCommand` 表示无前置操作，agent 直接启动。

## 启动检测

每个 `webUrl` 每 3 秒做一次 HTTP 探测。**任意** HTTP 响应（哪怕是 `401`/`404`）
都视为服务已起 → 卡片标记为**运行中**。连接被拒或超时则视为**已停止**。

## 迁移

加载时，`AgentConfig::load()` 会将内置默认配置与磁盘配置合并：磁盘上为空的
字段会被默认值填充，默认配置中存在但磁盘上缺失的 agent 会被追加。这确保了旧
配置能自动获得新字段。如有变化，更新后的配置会写回磁盘。

## 默认 agent

| Agent | 启动命令 | Web 地址 | 配置目录 |
|---|---|---|---|
| Kimi Code | `kimi web` | `http://127.0.0.1:58627` | `%USERPROFILE%/.kimi-code` |
| OpenCode | `opencode web --port 4096` | `http://127.0.0.1:4096` | `%USERPROFILE%/.config/opencode` |
| Qwen Code | `qwen serve ...` | `http://127.0.0.1:4170` | `%USERPROFILE%/.qwen` |
| OpenClaw | `openclaw gateway --port 18789` | `http://127.0.0.1:18789` | `%USERPROFILE%/.openclaw` |
| DeepSeek Harness | `dsh web` | `http://127.0.0.1:3080` | `%USERPROFILE%/.dsh` |

!!! note "OpenCode 默认随机端口"
    OpenCode 每次运行随机选取空闲端口，这会让健康检查和「浏览器打开」不可靠。
    默认配置固定为 `4096`（`--port` 参数与 Web 地址同时固定）。如 `4096` 被占用，
    可在配置页修改。

## 新增 agent

1. 打开 `~/.AgentLauncher/agents.json`。
2. 在 `agents` 数组中追加一个至少填好 `id`、`name`、`command`、`webUrl`、`color`
   的对象。
3. 重启 AgentLauncher（或下次启动时自动加载）。

无需重新编译。

### 完整示例

```json
{
    "id": "my-agent",
    "name": "My Agent",
    "command": "my-agent serve --port 3000",
    "webUrl": "http://127.0.0.1:3000",
    "configDir": "%USERPROFILE%/.my-agent",
    "icon": "C:/Users/me/icons/my-agent.svg",
    "color": "#00d4aa",
    "cardColor": "#0d2818",
    "installCommand": "npm install -g my-agent",
    "updateCommand": "npm update -g my-agent",
    "versionCommand": "my-agent --version",
    "setupCommand": ""
}
```
