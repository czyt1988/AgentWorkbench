# AgentWorkbench

一个用 **Qt6/QML + C++** 开发的 **AI 编码 agent 工作台**（原名 *AgentLauncher*）：左侧边栏 + 右侧工作区的外壳，可启动多个 AI 编码 agent（Kimi Code、OpenCode、Qwen Code）的 **Web 端**、以内嵌标签页使用它们的界面、浏览本机 Skill，并用 JSON 文件驱动全部配色。所有内容**配置化驱动**——新增 agent 只需编辑一个 JSON 文件，无需改代码。

![AgentWorkbench 主界面](../pic/screenshot-main-page.png)

## 为什么需要

每个 AI 编码 agent 都有自己的命令行工具和启动本地 web 服务的方式，默认端口不同、配置目录布局也不同，逐个记住命令很繁琐。AgentWorkbench 给你一个统一入口：启动任意一个，并在应用内标签页（或浏览器）里直接使用它的 web 界面。

## 功能

- **工作台外壳**：侧边栏页面导航带徽标与快捷键（`Ctrl+1…9`、`Ctrl+B`、`Ctrl+,`），工作区页面承载内容，状态栏显示 Python/Node 运行时徽标。
- **卡片网格启动器**：启动/安装/更新操作、版本号、HTTP 健康检查的运行态高亮与会话内停止。
- **内嵌 Web 标签页**（Qt WebEngine）：每个 agent 独立持久 profile、失活冻结、可恢复的释放视图、崩溃/离线/加载失败覆盖层——「在浏览器打开」始终作为兜底可用。
- **Skill 浏览**：覆盖常见 `SKILL.md` 目录，支持搜索、分面、悬停详情与点击复制路径。
- **主题**：JSON 文件驱动，内置两套 Catppuccin 变体，自定义主题保存即热重载，构建门禁拒绝 QML 字面颜色。
- **完全配置化**，统一写在 `agents.json` 与 `settings.json`。
- **实验性插件**（带版本化 ABI，默认禁用）。

## 快速开始

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
./build/AgentWorkbench
```

## 文档地图

文档按读者分成三个板块，各写各的角度：

- **[使用指引](guide/index.md)**——给**用软件的人**：每个功能是做什么的、怎么配置，全部用日常语言写。
- **[架构](architecture/index.md)**——给**读代码的人**：分层、依赖规则、前端与 C++ 库设计原则、扩展点。
- **[开发](development/index.md)**——给**改代码的人**：构建与测试环境，然后每个功能一篇，讲前端设计、后端设计与业务逻辑。

参考资料：[配置参考](configuration.md)（`agents.json` 与 `settings.json` 的每个字段）、[编写插件](plugins.md)、[编程规范](standards/coding-standard.md)、[WebEngine 内嵌调研](research/webengine-embedding.md)。文档自身的写作规约见 [docs/AGENTS.md](AGENTS.md)。

