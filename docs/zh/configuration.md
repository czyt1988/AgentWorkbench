# 配置

AgentWorkbench 采用配置化驱动。应用写入的所有内容都放在同一个数据目录中，该目录在
首次运行时创建。升级后的首次启动会先把旧 `~/.AgentLauncher` 的数据（配置和日志）
**复制**过来——旧目录不会被删除：

| 系统 | 路径 |
|---|---|
| Windows | `%USERPROFILE%\.AgentWorkbench\agents.json` |
| Linux | `~/.AgentWorkbench/agents.json` |
| macOS | `~/.AgentWorkbench/agents.json` |

所有用户数据都集中在同一个目录：`agents.json`、`agent_state.json`（一次性设置状态）以及 `log/agentworkbench.log`。

日志记录启动器做过的事情：它执行的每条命令（真实命令行、退出码、耗时，以及命令自身的输出，失败时同样记录），以及配置写入、一次性设置状态和 agent 运行状态的变化。日志按 5 MB 轮转，最多保留三个文件（`agentworkbench.log`、`agentworkbench.log.1`、`agentworkbench.log.2`），最旧的一个会被删除，因此日志总量不会超过 15 MB。写盘在后台线程完成（异步队列 + 独立工作线程）：发出日志的线程只负责拼行与入队，日志再密也不会卡住界面；warning 及以上即时落盘，其余最迟每秒一次——异常退出最多丢失最后一秒内低于 warning 的行。

应用内置了默认配置（`config/default_agents.json`，编译进可执行文件）。**内置** agent
的定义只来自这份文件：磁盘上的同名 id 条目会在每次启动时被它整体覆盖，所以磁盘里的
`agents.json` 只负责两件事——记录你删掉了哪些内置项（根级 `removed` 数组），以及保存
你自己新增的 agent（排在内置项之后）。改内置项要改 `config/default_agents.json` 并
重新编译，改自己新增的项在设置页里改即可。若配置与默认值完全一致（没有自建 agent、
没有删除记录、标题未改），磁盘文件就是默认配置的逐字节副本，可直接 diff。

## settings.json

应用的全部设置集中在一个文件里，键位与分组如下。缺失的键就地取默认值——**没有
迁移代码**：

```json
{
  "window":  { "title": "", "width": 1440, "height": 900,
               "sidebarWidth": 240, "sidebarCollapsed": false,
               "lastPageId": "agents" },
  "appearance": { "theme": "mocha-dark", "followSystem": false },
  "locale":  { "override": "" },
  "launcher": { "healthCheckIntervalMs": 3000, "startupVersionCheck": true },
  "web":     { "surface": "embedded", "freezeInactiveTabs": false,
               "maxLiveTabs": 8, "downloadDir": "", "chromiumFlags": "",
               "homeUrl": "" },
  "skills":  { "roots": [], "includePluginCaches": true, "maxDepth": 6 },
  "logging": { "maxFileSize": 5242880, "maxFiles": 3,
               "level": "debug", "mirrorToStderr": true },
  "plugins": { "enabled": false, "disabledIds": [] }
}
```

- `window.title` 为空表示使用品牌标题 `AgentWorkbench`；`agents.json` 根级
  的 `title` 字段已停用，残留值会在日志中提示一次。
- `appearance.theme` 指向主题 `id`，找不到时回退 `mocha-dark` 并记警告。
- `web.surface` 取 `embedded` 或 `external`；未编译 WebEngine 时自动降级为
  `external`。`chromiumFlags` 在 WebEngine 初始化前注入——内嵌视图无法启动
  （GPU 驱动问题）时可在此添加 `--disable-gpu`，重启后生效。
- `web.maxLiveTabs` 限制同时存活的视图数（每个约 250–350 MB），超出后按
  最久未用释放为可恢复状态；`web.freezeInactiveTabs`（默认关闭）额外挂起
  切走标签的 JS 以省 CPU——但恢复冻结的 agent WebUI 时页面会明显重绘，且
  切走时仍在途的加载会一直挂起到你切回来，因此默认关闭。两种情况下
  Chromium 本身都会对隐藏页面做节流。
- `skills.roots` 为空 = 平台默认根目录；非空即**完全取代**默认，条目为
  `{ "id", "label", "path", "kind", "enabled" }`。
- `logging.level` 是落盘的最低级别——`debug`（默认，全部记录）、`info`、`warning`、`critical` 或 `off`，非法值告警后回退 `debug`；`logging.mirrorToStderr`（默认开启）把每行同时镜像到启动它的控制台。两项都在下次启动时生效。
- `plugins.enabled` 是插件总开关，`disabledIds` 记录逐项禁用；插件在启动时
  加载，开关重启后生效。参见[插件](plugins.md)。

## 主题

| 位置 | 用途 |
|---|---|
| `:/themes/*.json`（随包编译） | 内置主题：`mocha-dark.json`、`latte-light.json` |
| `<数据目录>/themes/*.json` | 你的主题；`id` 相同则覆盖内置 |

新建主题的最小流程：把内置主题复制到 `<数据目录>/themes/<你的 id>.json`，
改 `id`/`name`/`variant` 与颜色，保存——界面立即热重载——然后在
**设置 → 外观** 中选择它。文件名与 `id` 不符会跳过并记警告；未知令牌忽略；
缺失令牌回退同 variant 的内置主题。全部令牌即内置主题文件用的那一套——
QML 只引用 `theme.<令牌>`。

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
  `~/.AgentWorkbench/agent_state.json`，之后不再重复运行，除非用户从卡片右键菜单选择
  **重新初始化**。
- 如果 `setupCommand` 以非零退出码结束，会触发 `launchFailed` 并显示捕获
  的输出，agent 不会启动。
- 留空的 `setupCommand` 表示无前置操作，agent 直接启动。

## 启动检测

每个 `webUrl` 每 3 秒做一次 HTTP 探测。**任意** HTTP 响应（哪怕是 `401`/`404`）
都视为服务已起 → 卡片标记为**运行中**。连接被拒或超时则视为**已停止**。

## 内置 agent 与默认配置

加载时，`AgentRepository::load()` 会把默认配置里的每个内置 agent 应用到配置中：磁盘上
同 id 的条目被整体替换为 `config/default_agents.json` 里的定义（`removed` 数组中的 id
除外），你自己新增的 agent 原样保留并排在内置项之后。因此不存在「旧配置兼容」一说，
也不需要对用户数据做迁移：调整内置启动器只需要改 `config/default_agents.json` 并
重新编译。内容发生变化时更新后的配置会写回磁盘。

注意由此带来的取舍：在设置页里编辑一个**内置** agent（例如改端口）只对本次运行有效，
下次启动会被默认配置覆盖。要让改动长期生效，就把它写进
`config/default_agents.json`。

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

1. 打开 `~/.AgentWorkbench/agents.json`。
2. 在 `agents` 数组中追加一个至少填好 `id`、`name`、`command`、`webUrl`、`color`
   的对象。
3. 重启 AgentWorkbench（或下次启动时自动加载）。

无需重新编译。

!!! note "只对新的 id 生效"
    这里手写或删除的条目必须使用不属于内置项的 `id`。同 id 的条目会在每次启动时被
    `config/default_agents.json` 覆盖（见上一节）；要改内置项，请改那份默认配置并
    重新编译。若要删除内置项，用设置页的「删除」，它会把 id 记进 `removed` 数组。

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
