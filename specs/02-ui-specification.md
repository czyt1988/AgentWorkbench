# AgentWorkbench 界面规格（二）：外壳、页面与主题

> 状态：**设计基线，待实施**（2026-09-26）
> 前置阅读：`01-architecture.md`（模块与依赖规则）。本文只定义「长什么样、叫什么、交互如何」，不重复模块划分。
> 契约强度：本文中的 **QML 文件名、页面 id、主题令牌名、配置键名、快捷键、全局对象名** 都是契约。文中的颜色十六进制值是实现默认值，可以微调；令牌**名字**不许改。
> 交付目标：一个带左侧边栏 + 右侧工作区的窗口；启动器是侧边栏的一个功能；右侧工作区支持卡片网格、带标签页的内嵌 Web 视图、Skill 卡片浏览器；全部视觉取值来自主题 JSON 文件，QML 里零硬编码颜色。

---

## 1. 设计原则

1. **深色优先，但结构上与明暗无关**：所有颜色都是语义令牌，暗色只是默认主题。任何写死的 `#rrggbb` 都算 bug（由 `scripts/check-architecture.sh` 卡门）。
2. **工作区是内容，侧边栏是地图**：侧边栏只负责「去哪」，不承载业务操作；页面的操作按钮属于页面自己的头部。
3. **同一时刻只有一个活动视图**：侧边栏切换页面时，工作区整页切换；页面内部的二级视图（编辑、详情）用对话框或页内 `Loader`，不再叠 `StackView`。
4. **能看见状态，不用猜**：每个异步动作都要有可见反馈（spinner / 进度 / toast / 卡片闪红），沿用 0.3.0 的既有反馈习惯，不倒退。
5. **键盘可达**：所有鼠标能做的事都有键盘路径；Web 视图的快捷键必须显式回收，否则会被 Chromium 吃掉。
6. **QML 只做表现**：任何判断、解析、持久化都在 C++ 门面里。QML 里出现「业务 if」就是分层错了。

---

## 2. 窗口骨架

### 2.1 线框图

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│ AgentWorkbench                                                       ─  □  ×      │  ← 原生标题栏（不自绘）
├────────────────┬─────────────────────────────────────────────────────────────────┤
│  ⌘ AgentWorkbench │  启动器                                                       │  ← PageHeader（页面标题 + 页面操作区）
│                ├─────────────────────────────────────────────────────────────────┤
│  ▸ 启动器   (3) │                                                                 │
│  ▸ Web      (2) │        ┌──────────┐  ┌──────────┐  ┌──────────┐                │
│  ▸ Skills       │        │ 卡片      │  │ 卡片      │  │ 卡片      │                │  工作区
│                 │        └──────────┘  └──────────┘  └──────────┘                │
│  ────────────   │                                                                 │
│                 │                                                                 │
│  ⚙ 设置         │                                                                 │
│  ◂              │                                                                 │
├────────────────┴─────────────────────────────────────────────────────────────────┤
│ Python 3.12 · Node 22  │  运行中 3 · 标签 2                     │  v0.4.0        │  ← 状态栏
└──────────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 尺寸与窗口

| 项 | 值 | 来源 |
| --- | --- | --- |
| 默认窗口 | 1440 × 900 | `settings.json` `window.width/height`（首次运行写入默认值） |
| 最小窗口 | 1024 × 640 | 写死在 `MainWindow.qml`（`minimumWidth/minimumHeight`） |
| 侧边栏展开宽度 | 240 | `theme.sidebarWidth` |
| 侧边栏折叠宽度 | 64（只留图标 + tooltip） | `theme.sidebarCollapsedWidth` |
| 状态栏高度 | 28 | `theme.statusBarHeight` |
| 页面头部高度 | 64（内容区含 `spacingL` 内边距） | 组件 `PageHeader.qml` |
| 窗口标题 | `settings.json` `window.title`，为空 → `AgentWorkbench` | `MainWindow.title` |

关闭行为保持 0.3.0：本次会话若由启动器启动过 agent，关窗时弹「确认退出」对话框，三个选项为「关闭后台终端并退出 / 直接退出 / 取消」。`agent.launching` 或 Web 视图正在加载不作为拦截条件。

---

## 3. 侧边栏规格

### 3.1 结构

```
┌──────────────────────┐
│ [icon] AgentWorkbench│  ← 头部：应用图标 20px + 名称（折叠时只留图标）
├──────────────────────┤
│ ▸ 启动器          (3)│  ← section = main
│ ▸ Web             (2)│
│ ▸ Skills             │
├──────────────────────┤
│ ▸ 插件               │  ← section = extensions（S7 之前恒为空，整段隐藏）
├──────────────────────┤
│ ⚙ 设置              │  ← section = system
├──────────────────────┤
│              [◂ 折叠]│  ← 底部：折叠按钮（折叠后变 [▸]）
└──────────────────────┘
```

### 3.2 交互

| 行为 | 规格 |
| --- | --- |
| 点击条目 | `nav.setCurrentPageId(id)`（`currentPageId` 属性的写入器），工作区 `Loader` 切换页面；当前页高亮 |
| 高亮样式 | 背景 `theme.surfaceBg`，左侧 3px `theme.accent` 竖条，文字 `theme.textPrimary` |
| 悬停样式 | 背景 `theme.surfaceHoverBg`（`Behavior on color { ColorAnimation { duration: theme.durationFast } }`） |
| 徽标 | `nav` 的 `badgeText`；胶囊底色 `theme.badgeBg`，文字 `theme.textSecondary`，`fontSizeCaption`。启动器页徽标 = 运行中的 agent 数（0 时不显示），Web 页 = 打开的标签数 |
| 折叠 | 宽度 240 ⇄ 64，动画 `durationNormal`；折叠状态写入 `settings.json` `window.sidebarCollapsed` |
| 折叠态 | 只显示 18px 图标，居中；tooltip 显示页面标题（延迟 300ms） |
| 键盘 | `Ctrl+B` 折叠/展开；`Ctrl+1…9` 按 `order` 顺序切页；折叠态下键盘仍然有效 |
| 溢出 | 条目多于可视高度时侧边栏整体可滚动，底部折叠按钮固定 |

侧边栏**不**显示运行时徽标（那是状态栏的事），**不**放任何业务按钮。

---

## 4. 工作区与页面宿主

### 4.1 页面描述符

```qml
// shell::PageDescriptor 暴露给 QML 的字段
{ id: "agents", title: qsTr("Agent Launcher"), iconSource: "qrc:/icons/terminal.svg",
  source: "qrc:/qt/qml/AgentWorkbench/agents/AgentGridPage.qml",
  section: "main", order: 10, badgeText: "" }
```

### 4.2 宿主行为

- `Workspace.qml`：`Loader { source: nav.currentPage.source }`，切换时旧的页面组件被销毁（`Loader` 默认行为）。
- 页面根元素必须是 `Item`，并用 `PageHeader` 渲染标题与操作区；**Web 页是唯一例外**（它自己渲染标签栏作为头部，见 §6）。
- 页面切换不带动画，或最多 100ms 淡入淡出；不要横向滑动（会让人误以为有页面栈）。
- 需要在页面切换后仍保留的状态（筛选条件、滚动位置）放 C++ 侧或 `Settings`；QML 里的临时状态允许随页面销毁丢失。
- 每个页面必须实现三种状态：**加载中**、**空**（附下一步操作按钮）、**出错**（可读原因 + 重试）。规格见各页面小节。

---

## 5. 页面清单

| id | 标题（英文源串） | 图标 | section | order | 徽标 | 优先级 |
| --- | --- | --- | --- | --- | --- | --- |
| `agents` | `Agent Launcher` | `qrc:/icons/terminal.svg` | main | 10 | 运行中数量 | P0 |
| `web` | `Web` | `qrc:/icons/web.svg` | main | 20 | 标签数量 | P0 |
| `skills` | `Skills` | `qrc:/icons/skills.svg` | main | 30 | — | P0 |
| `settings` | `Settings` | `qrc:/icons/gear.svg` | system | 100 | — | P0 |
| `logs` | `Logs` | `qrc:/icons/scroll.svg` | system | 110 | — | P2（后续） |

需要新增的 SVG 图标（放进 `icons/`，以 `/icons` 前缀编进资源）：`web.svg`（地球/窗口）、`skills.svg`（书/魔杖）、`copy.svg`、`refresh.svg`、`plus.svg`、`menu.svg`（⋯，标签栏菜单）、`close.svg`、`chevron-left.svg`、`chevron-right.svg`、`external-link.svg`、`folder.svg`、`search.svg`、`scroll.svg`（P2）。风格与既有的 `terminal.svg`、`gear.svg` 保持一致：单色线性、`currentColor` 不可用，因此每个图标提供两种色值版本的做法**不采用**，改为统一用 `theme.textMuted` 色调、通过 `Image` 的 `layer.enabled + ColorOverlay` 或直接内置中性灰 `#7f849c` 绘制。

---

## 6. 页面：Web

### 6.1 布局

```
┌───────────────────────────────────────────────────────────────────────────────┐
│ ◉ Kimi Code × │ ● OpenCode × │   DeepSeek ×     │  [⟳][⤴][⋯]                │  ← 标签栏（= 本页头部）
├───────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│                        内嵌的 agent WebUI（WebEngineView）                     │
│                                                                               │
└───────────────────────────────────────────────────────────────────────────────┘
```

- 标签栏高度 36（`theme.tabBarHeight`），背景 `theme.chromeBg`，下边框 `theme.separator`。
- 标签从左到右按打开顺序排列；溢出时可横向滚动（`Flickable`），不做多行。
- 右侧固定操作区，作用于**当前活动标签**：`⟳` 重载/`✕` 停止加载、`⤴` 在浏览器打开（始终可见，这是调研结论要求的兜底逃生口）、`⋯` 菜单（复制 URL、缩放 ±/重置、开发者工具（仅 Debug 构建）、关闭标签）。
- 无标签时显示空状态：标题「还没有打开的 Web 视图」+ 正文说明 + 运行中 agent 的列表（每个一行：图标、名称、`webUrl`、「打开」按钮）；没有任何 agent 在运行时提示去启动器页启动。

### 6.2 标签外观

| 状态 | 外观 |
| --- | --- |
| 活动 | 背景 `theme.tabActiveBg`，顶部 2px `theme.accent` 条，文字 `theme.textPrimary` |
| 非活动 | 背景 `theme.tabInactiveBg`，文字 `theme.textMuted`，悬停变 `theme.surfaceHoverBg` |
| 加载中 | 标签内 `⟳` 位置显示 `BusyIndicator`（12px）；同时用一条 2px 进度线画在标签栏底部，宽度 = `loadProgress` |
| 标题 | `tab.title` 为空时回退 `tab.url` 的 host:port；超过 160px `elide: Text.ElideMiddle` |
| 图标 | `tab.iconSource`，16px；缺失时用 agent 图标；再缺失用 `qrc:/icons/web.svg` |
| 状态点 | 8px 圆点，颜色 = agent 颜色；`offline` 时 `theme.neutralOff`，`crashed`/`error` 时 `theme.danger` |
| 关闭按钮 | `×`，仅在悬停或活动标签上可见；中键点击标签也可关闭 |
| 生命周期 | 关闭标签 → 销毁视图（**不**结束 agent 进程）；会话数据靠 profile 持久化（见 §6.5） |

### 6.3 视图状态机

```
                 openTab(agentId)
   (无) ─────────────────────────────► loading ──loadFinished(ok)──► ready
                                        │  ▲                          │
                     loadFinished(!ok)  │  │ reload()                 │ agent 停止 / 连接被拒
                                        ▼  │                          ▼
                                      error ◄──── renderProcessTerminated ──► crashed
                                        │                                     │
                                        └──────── reload() ◄──────────────────┘
     ready/error/crashed ──(agent 重新运行)──► loading
```

| 状态 | 覆盖层内容 | 按钮 |
| --- | --- | --- |
| `loading` | 居中 spinner + `qsTr("Loading %1…").arg(host)` | 取消（停止加载） |
| `offline` | `qsTr("Agent 已停止")` 语义英文串 `qsTr("This agent is not running")` + 一行说明（webUrl） | 「重新启动」（调 `workbench.launchAgent(agentId)`）、「重试」 |
| `crashed` | `qsTr("The page crashed")` + 最后一条错误信息 | 「重新加载」、「在浏览器打开」 |
| `error` | `qsTr("Failed to load the page")` + `lastError` + URL | 「重试」、「在浏览器打开」 |
| `released` | 灰色占位 + `qsTr("View released to free memory")` | 「恢复视图」（点标签即恢复） |

覆盖层背景：`theme.windowBg` + 70% 不透明遮罩，文字用 `textPrimary/textMuted`，按钮用 shell 的标准按钮组件。

### 6.4 平台事件处理（缺一不可）

| 事件 | 处理 |
| --- | --- |
| `newWindowRequested`（`target="_blank"`、`window.open`、OAuth 弹窗） | 目标是 loopback（`127.0.0.1`/`localhost`）→ 在本页开新标签并激活；其它 → `QDesktopServices::openUrl`。**必须**设置 `request.accepted`，否则请求直接失败且无任何提示（调研 §3.2） |
| `downloadRequested` | 接受并下载到 `web.downloadDir`（默认 `%USERPROFILE%/Downloads`）；开始与完成各弹一条 toast，完成时带「打开文件夹」 |
| `fullScreenRequested` | 接受，页面切到全屏（标签栏隐藏）；`Esc` 先退出全屏，再退不出才关浮层 |
| `renderProcessTerminated` | 转发给门面 → 标签转 `crashed`；**不要**自动重载（避免崩溃循环） |
| `loadFinished(ok=false)` | 转 `error`，记录 URL 与错误串到日志 |
| HTTP 认证 / JS `alert` / `<input type=file>` | 用 Qt 默认对话框（不接管） |
| 权限请求（摄像头/麦克风/通知） | v1 全部拒绝并记日志，toast 提示「该页面请求了浏览器权限，当前版本不支持」 |

### 6.5 Profile 与内存策略

- **一 agent 一持久 profile**：`<dataRoot>/webprofiles/<agentId>`，`storageName = "awb-" + agentId`，Cookies 与 localStorage 落盘。这是硬要求：Chromium cookie 忽略端口，共用 profile 会让 `127.0.0.1:58627` 与 `127.0.0.1:4096` 互相串会话（调研 §3.1 有实测证据）。
- **失活冻结**：切走的标签 `lifeCycleState = WebEngineView.Frozen`（`web.freezeInactiveTabs` 默认 true）。冻结释放内存但保留会话，切回来恢复。
- **视图上限**：`web.maxLiveTabs` 默认 8。超过时按 LRU 释放最久未用的**冻结**标签的视图（标签保留，状态 `released`，点击恢复）。
- **只有关闭标签才销毁视图**；`web.maxLiveTabs` 触发的释放是「可恢复的丢失」，要在文档与 UI 文案里区分清楚。
- 内存量级写进用户文档：每个活动视图约 +250–350 MB 工作集（调研 §4.2）。`maxLiveTabs` 的说明文字里带上这个数字。

### 6.6 快捷键归属

Web 视图获得焦点后，F5 / Esc / Ctrl+W / Ctrl+F 默认归 Chromium。需要回收的用 `Shortcut { context: Qt.ApplicationShortcut }`：

| 键 | 行为 |
| --- | --- |
| `Ctrl+W` | 关闭当前 Web 标签（不是关闭窗口） |
| `F5` / `Ctrl+R` | 重载当前视图 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 下一个 / 上一个标签 |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | 放大 / 缩小 / 重置缩放（`zoomFactor`，步长 0.1，范围 0.5–2.0，按标签记忆） |
| `Esc` | 退出全屏 → 关闭浮层 → （都不适用时）取消加载 |
| `F12` | 仅 Debug 构建：在独立窗口打开 `devToolsView` |

留给 Chromium：复制/粘贴/撤销/全选、页面内查找（`Ctrl+F`，P1 再补应用内查找栏；v1 该键不接管，行为与浏览器一致——页面自己的查找 UI 若存在仍然可用）。

### 6.7 降级路径

| 情形 | 行为 |
| --- | --- |
| 构建时 `AWB_ENABLE_WEBENGINE=OFF` | `embedded` 表面不存在；`web.surface` 强制为 `external` |
| 用户把 `web.surface` 设为 `external` | `openTab` 不建标签，直接系统浏览器打开 + 一条 info toast |
| WebEngine 初始化失败（GPU 驱动等） | 启动时记 ERROR；打开标签时 toast 提示「内嵌视图不可用，已改用系统浏览器」，并在设置页给出 `chromiumFlags` 的建议填法（如 `--disable-gpu`） |
| 页面用到了内嵌引擎不支持的能力（MP4/H.264 无法播放、页面按 Chrome 版本号判浏览器而拒绝工作） | 不做特殊处理、不伪装 UA；这些是 Qt 6.7.3 内嵌 Chromium 118 的已知限制（决策见 `01-architecture.md` §12.1）。工具栏的「在浏览器打开」是标准应对路径，用户文档里要写明这两条限制 |

「在浏览器打开」在任何情况下都必须是可用的一等公民，不能藏在只有内嵌成功时才出现的菜单里。

---

## 7. 页面：Skills

### 7.1 布局

```
┌───────────────────────────────────────────────────────────────────────────────┐
│ Skills                                                                        │
│ 本机各 AI 工具目录下的 SKILL.md                                        [⟳ 重新扫描]│
├───────────────────────────────────────────────────────────────────────────────┤
│ [搜索名称或描述…]   来源: (全部)(agents)(claude)(codex)(插件)(项目)   排序: [名称 ▾]│
├───────────────────────────────────────────────────────────────────────────────┤
│  ┌────────────────────┐  ┌────────────────────┐  ┌────────────────────┐       │
│  │ tdd          agents│  │ docx         plugin│  │ pdf          plugin│       │
│  │ Test-driven develop│  │ Complete DOCX ...  │  │ Professional PDF...│       │
│  │ ───────────────────│  │ ───────────────────│  │ ───────────────────│       │
│  │ ~/.agents/skills/… │  │ ~/.zcode/…/docx  ⧉ │  │ ~/.zcode/…/pdf   ⧉ │       │
│  └────────────────────┘  └────────────────────┘  └────────────────────┘       │
│  共 47 个 skill（3 个根目录被跳过，见底部提示）                                 │
└───────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 扫描根（默认清单）

`skills.roots` 为空时使用下表；用户可在设置页增删、启停。`~` / `%VAR%` 由 `core::EnvExpander` 展开。

| 顺序 | 路径 | `kind` | 说明 |
| --- | --- | --- | --- |
| 1 | `~/.agents/skills` | `agents` | 通用 agent skill 目录 |
| 2 | `~/.claude/skills` | `claude` | Claude Code 体系 |
| 3 | `~/.codex/skills` | `codex` | Codex 体系 |
| 4 | `~/.zcode/cli/plugins/cache/*/*/*/skills` | `plugin` | ZCode 插件缓存；**同一插件多版本会同时存在**（例如 `browser-use/0.2.1`、`0.3.0`、`0.4.2`、`0.5.1`），必须去重，只保留最高版本 |
| 5 | `<当前工作目录>/.agents/skills`、`<cwd>/.claude/skills` | `project` | 有 cwd 概念后再启用；v1 可只扫描应用自身安装目录同级的 `.agents/skills`（若存在） |

`skills.includePluginCaches = false` 时跳过第 4 行。`skills.maxDepth` 默认 6：从根目录起向下最多 6 层找 `SKILL.md`；找到后不再进入该目录。

去重规则：**同一 marketplace + 同一插件**的多个版本视为同一来源，保留版本号最高的一个；不同根目录下的同名 skill **都保留**（内容可能不同），并在卡片上用来源徽标区分。

### 7.3 卡片规格

| 元素 | 规格 |
| --- | --- |
| 尺寸 | 宽 340（`Flow`，间距 `spacingL`），高 160 |
| 背景 | `theme.surfaceBg`，圆角 `theme.radiusCard`，1px `theme.borderSubtle`；悬停时边框 `theme.accent`，背景 `theme.surfaceHoverBg` |
| 标题 | `skill.name`，`fontSizeSubtitle`，`theme.textPrimary`，加粗，单行 elide |
| 来源徽标 | 右上角胶囊：`agents` / `claude` / `codex` / `plugin` / `project`；底色 `theme.badgeBg`，文字 `theme.textSecondary`，`fontSizeCaption`；`plugin` 徽标悬浮显示插件名与版本 |
| 描述 | frontmatter 的 `description`，`fontSizeSmall`，`theme.textMuted`，最多 3 行（`maximumLineCount: 3`，`elide: Text.ElideRight`） |
| 底部 | 分隔线 `theme.separator`；下面是路径（等宽字体、`fontSizeCaption`、`theme.textMuted`、`elide: Text.ElideMiddle`，悬停显示完整路径 tooltip）+ 右侧 `copy.svg` 图标按钮 |
| 点击卡片主体 | **复制该 skill 目录的绝对路径**到剪贴板，弹一条 success toast：「已复制路径：…」（路径过长时中间省略） |
| 点击 `⧉` | 同上（显式入口，给不看文档的人） |
| 右键菜单 | 复制路径、复制 SKILL.md 路径、复制 skill 名称、打开所在文件夹、打开 SKILL.md、在设置中查看该根目录 |
| 键盘 | Tab 聚焦卡片 → `Enter` = 复制路径，`Ctrl+Enter` = 打开文件夹；`↑↓←→` 在网格中移动焦点 |

### 7.4 悬停详情（hover flyout）

- 触发：鼠标停留 400ms（`theme.durationNormal` 的整数倍即可）；键盘聚焦也显示。
- 尺寸：宽 420，最大高 320，超长内容可滚动（`Flickable`）。
- 位置：默认在卡片右下方 12px 处；超出窗口右/下边界时翻转到左/上。
- 内容（自上而下）：`skill.name`（`fontSizeSubtitle` 加粗）→ 来源徽标 + 根目录标签 → **完整 `description`**（不截断，`wrapMode: Text.WordWrap`，`fontSizeSmall`，`theme.textSecondary`）→ 分隔线 → 元信息表格：SKILL.md 绝对路径、最后修改时间（`yyyy-MM-dd HH:mm`）、文件大小（KB）→ 底部提示行：`qsTr("Click the card to copy the path")`。
- frontmatter 里除 `name`/`description` 之外的标量键（如 `allowed-tools`）以「键: 值」列表追加在元信息表格里；值过长时单行 elide。
- 关闭：鼠标移出 300ms 后，或按下任意键，或页面滚动时立即关闭。

### 7.5 过滤、排序与状态

- 搜索框：匹配 `name`、`description`、`dirPath`（不区分大小写），输入即过滤（无需回车）。
- 来源分面：可多选的胶囊按钮（`全部` 与其余互斥）。
- 排序：`名称`（默认，本地化排序）、`最近修改`、`来源`。
- 加载中：首次进入页面时显示 6 个骨架卡片（同尺寸、`theme.surfaceBg` + 微光动画）。
- 空：`qsTr("No skills found")` + 说明「已扫描的根目录如下」+ 根目录清单 + 「去设置里添加目录」按钮。
- 出错/部分失败：不整页报错；底部显示一条 warning 行「N 个根目录无法读取：<路径>」，可点开查看原因；其余结果照常展示。
- 统计行：`qsTr("%n skill(s) found", "", count)`，用复数形式；`n` 由 Qt 处理，中英各自翻译。

---

## 8. 页面：启动器（Agents）

保留 0.3.0 的卡片交互，只换承载方式与配色来源。

### 8.1 布局

```
┌───────────────────────────────────────────────────────────────────────────────┐
│ Agent Launcher                                                                │
│ 启动 AI 编码 agent 并打开它们的 Web 界面           [+ 添加启动器] [⋯ 恢复默认]  │
├───────────────────────────────────────────────────────────────────────────────┤
│ [搜索…]      显示: (全部)(运行中)(未安装)                                       │
├───────────────────────────────────────────────────────────────────────────────┤
│   ┌────────────┐  ┌────────────┐  ┌────────────┐                              │
│   │  (卡片)     │  │  (卡片)     │  │  (卡片)     │      Flow, spacing 20        │
│   └────────────┘  └────────────┘  └────────────┘                              │
└───────────────────────────────────────────────────────────────────────────────┘
```

- 卡片：260 × 230（`theme.cardMinWidth` / `theme.cardHeight`），沿用 `AgentCard.qml` 的全部内部结构与交互（左键=启动/打开、右键菜单、左上角版本/安装指示、右上角 `×` 停止、控制台面板、闪红反馈）。
- 卡片配色改为：运行中 = agent 颜色 16% 填充 + 2.5px agent 颜色描边；非运行 = `agents.json` 的 `cardColor`（若设置）否则 `theme.surfaceBg`；描边默认 `theme.borderSubtle`；闪红用 `theme.danger`。
- 卡片的「打开」动作从 `launcher.openWeb(id)` 改为 `workbench.openWeb(id)`（跨域意图）。
- 配置弹窗：`AgentEditDialog.qml`（原 `AgentEditPage.qml` 内容搬进 `Dialog`，宽度 640、最大高 = 窗口高 − 120、内部滚动）。保存/取消沿用原逻辑。
- 删除确认：内置 agent 删除后进入 `removed` 列表；确认弹窗里要说明「内置启动器可在设置里恢复」。
- 状态栏与运行徽标不在本页重复显示。

### 8.2 空与错误状态

| 情况 | 表现 |
| --- | --- |
| 没有任何 agent | 「还没有配置启动器」+ 「添加启动器」「恢复默认启动器」两个按钮 |
| 过滤后为空 | 「没有匹配的启动器」+ 「清除过滤」 |
| 配置文件写失败 | 页面顶部常驻一条 error 条：原因 + 「打开配置目录」（调 `workbench.openFolder(configDir)`） |
| 启动失败 | 卡片原地闪红 + 居中错误对话框（列出命令、退出码、捕获输出）。这条沿用 0.3.0 行为，不许降级为 toast |

---

## 9. 主题文件规格

### 9.1 文件位置与命名

| 位置 | 用途 |
| --- | --- |
| `:/themes/*.json`（资源，随包） | 内置主题：`mocha-dark.json`、`latte-light.json` |
| `<dataRoot>/themes/*.json` | 用户主题；`id` 与内置相同则覆盖内置 |

- 文件名必须等于 `id`（`<dataRoot>/themes/mocha-dark.json` ↔ `"id": "mocha-dark"`），否则该文件被跳过并记 `WARN`。
- 当前主题由 `settings.json` 的 `appearance.theme` 指定；找不到时回退 `mocha-dark` 并记 `WARN`。
- 热重载：监听主题目录与当前文件，文件保存后重新加载并立即重绘（用于调色时所见即所得）。

### 9.2 文件骨架

```json
{
  "id": "mocha-dark",
  "name": "Catppuccin Mocha (Dark)",
  "variant": "dark",
  "author": "built-in",
  "description": "Default dark theme.",
  "colors": { "windowBg": "#1e1e2e" },
  "metrics": { "radiusCard": 16 },
  "fonts": { "family": "", "monoFamily": "Consolas, Monaco, Courier New, monospace" },
  "agentPalette": ["#f38ba8", "#fab387", "#f9e2af", "#a6e3a1",
                    "#94e2d5", "#89b4fa", "#cba6f7", "#f5c2e7"]
}
```

- 必填：`id`、`name`、`variant`（`dark` | `light`）。其余可省略。
- 省略的 token 取**同 variant 的内置基准主题**的值（`dark` → `mocha-dark`，`light` → `latte-light`），不是取「当前主题」的值，以保证同一份用户主题在任何基线下结果一致。
- 未知键：记 `WARN` 并忽略（不报错、不阻止加载）。非法颜色（无法被 `QColor` 解析）：记 `WARN`，该 token 用基准值。
- XML 不支持，理由见 `01-architecture.md` §1.2。

### 9.3 颜色令牌表（`colors`）

| 令牌（JSON 键 = `theme.<名>`） | 语义 | mocha-dark | latte-light |
| --- | --- | --- | --- |
| `windowBg` | 窗口/页面底色 | `#1e1e2e` | `#eff1f5` |
| `sidebarBg` | 侧边栏背景 | `#181825` | `#e6e9ef` |
| `workspaceBg` | 工作区背景 | `#1e1e2e` | `#eff1f5` |
| `surfaceBg` | 卡片/面板背景 | `#313244` | `#ffffff` |
| `surfaceAltBg` | 次级面板、输入框 | `#45475a` | `#ccd0da` |
| `surfaceHoverBg` | 悬停填充 | `#4a4d62` | `#bcc0cc` |
| `chromeBg` | 侧边栏/标签栏/状态栏背景 | `#181825` | `#e6e9ef` |
| `overlayBg` | 对话框/弹层背景 | `#1e1e2e` | `#eff1f5` |
| `consoleBg` | 控制台、代码区域 | `#11111b` | `#dce0e8` |
| `textPrimary` | 主文字 | `#cdd6f4` | `#4c4f69` |
| `textSecondary` | 次要文字 | `#a6adc8` | `#5c5f77` |
| `textMuted` | 弱化文字、说明 | `#7f849c` | `#6c6f85` |
| `textDisabled` | 禁用文字 | `#6c7086` | `#9ca0b0` |
| `textOnAccent` | 强调色之上的文字 | `#ffffff` | `#ffffff` |
| `textLink` | 链接文字 | `#89b4fa` | `#1e66f5` |
| `borderSubtle` | 常规边框 | `#45475a` | `#ccd0da` |
| `borderStrong` | 强调边框 | `#585b70` | `#acb0be` |
| `separator` | 分隔线 | `#313244` | `#dce0e8` |
| `accent` | 品牌/主强调色 | `#89b4fa` | `#1e66f5` |
| `focusRing` | 键盘焦点环 | `#89b4fa` | `#1e66f5` |
| `success` | 成功态 | `#a6e3a1` | `#40a02b` |
| `warning` | 警告态 | `#f9e2af` | `#df8e1d` |
| `danger` | 错误/危险态 | `#f38ba8` | `#d20f39` |
| `info` | 信息态 | `#89b4fa` | `#1e66f5` |
| `neutralOff` | 未运行/中性指示 | `#585b70` | `#bcc0cc` |
| `tooltipBg` | 提示气泡背景 | `#313244` | `#4c4f69` |
| `tooltipText` | 提示气泡文字 | `#cdd6f4` | `#eff1f5` |
| `badgeBg` | 徽标/胶囊底色 | `#313244` | `#ccd0da` |
| `tabActiveBg` | 活动标签背景 | `#313244` | `#ffffff` |
| `tabInactiveBg` | 非活动标签背景 | `#181825` | `#e6e9ef` |
| `selectionBg` | 选中行背景 | `#45475a` | `#acb0be` |
| `scrollbar` | 滚动条滑块 | `#45475a` | `#bcc0cc` |

派生色（不进文件，由 `Theme` 的函数算），QML 里统一用它们，不要自己写 `Qt.darker`：

| 函数 | 语义 |
| --- | --- |
| `theme.alpha(color, a)` | 给定颜色叠加透明度，例如悬停填充 `theme.alpha(theme.accent, 0.18)` |
| `theme.hover(color)` | 悬停加深/提亮（内部按 `variant` 决定方向） |
| `theme.pressed(color)` | 按下态（比 `hover` 再深一档） |

### 9.4 度量与字体令牌（`metrics` / `fonts`）

| 令牌 | 默认 | 用途 |
| --- | --- | --- |
| `radiusCard` | 16 | 卡片圆角 |
| `radiusOverlay` | 12 | 对话框圆角 |
| `radiusControl` | 8 | 按钮/输入框圆角 |
| `radiusPill` | 11 | 胶囊/圆点 |
| `spacingXs` / `spacingS` / `spacingM` / `spacingL` / `spacingXl` | 4 / 8 / 12 / 20 / 28 | 间距阶（只许用这五档，禁止 5、6、7、13 这类魔法数） |
| `fontSizeCaption` | 10 | 徽标、路径 |
| `fontSizeSmall` | 11 | 描述、tooltip |
| `fontSizeBody` | 13 | 正文、按钮 |
| `fontSizeSubtitle` | 16 | 卡片标题、小节标题 |
| `fontSizeCardTitle` | 18 | 启动器卡片名称 |
| `fontSizePageTitle` | 24 | 页面标题 |
| `cardMinWidth` / `cardHeight` | 260 / 230 | 启动器卡片 |
| `durationFast` / `durationNormal` | 120 / 180 | 动画时长（ms） |
| `sidebarWidth` / `sidebarCollapsedWidth` | 240 / 64 | 侧边栏 |
| `statusBarHeight` / `tabBarHeight` | 28 / 36 | 状态栏、标签栏 |
| `toastWidth` | 340 | toast 宽度 |
| `fonts.family` | `""`（系统默认 UI 字体） | 全局字体族 |
| `fonts.monoFamily` | `Consolas, Monaco, Courier New, monospace` | 控制台、路径 |

新增一套主题的最小工作流（必须写进用户文档）：把内置主题文件复制到 `<dataRoot>/themes/<你的 id>.json`，改 `id`/`name`/`variant` 与颜色 → 保存 → 界面立即生效 → 在 设置 → 外观 里选择它。

### 9.5 一致性与对比度要求

- 颜色一律用 6 位十六进制（`#rrggbb`）；需要透明度用 `theme.alpha()`，不要在主题文件里写 `#aarrggbb`。
- 正文与背景对比度不低于 4.5:1，次要文字不低于 3:1。新增主题需自查（可在设置页加一个「对比度检查」P2 帮助工具）。
- agent 颜色（`agents.json` 的 `color`）是数据不是主题；但**自动分配用的调色板来自主题**的 `agentPalette`，明色主题必须给一套在浅底上可读的强调色（`latte-light` 用 Catppuccin Latte 的 accent 系列）。
- 主题切换是**运行时**生效的：所有 QML 属性绑定到 `theme.*`，不做重启提示。

---

## 10. 视觉与组件规范

### 10.1 状态样式（所有可交互元素统一）

| 状态 | 表现 |
| --- | --- |
| 默认 | 见各组件 |
| 悬停 | 背景 `theme.hover(baseColor)`；图标/文字提亮到 `textPrimary` |
| 按下 | 背景 `theme.pressed(baseColor)` |
| 焦点（键盘） | 2px `theme.focusRing` 外环，`radiusControl` 圆角，始终可见（不要只在键盘导航时才画，实现成本高于收益） |
| 禁用 | 透明度 0.5，鼠标指针不变，`ToolTip` 说明原因（例如「请先关闭 agent 再安装」） |
| 加载中 | 内联 `BusyIndicator` 替换文字，控件保持尺寸不变（避免布局跳动） |

### 10.2 组件清单（`src/shell/qml/components/`）

| 组件 | 用途 | 关键规格 |
| --- | --- | --- |
| `AButton.qml` | 通用按钮 | `variant`: `primary`（accent 底 + `textOnAccent`）/ `secondary`（`surfaceAltBg`）/ `ghost`（透明 + hover 填充）/ `danger`（danger 底）；高度 32，`radiusControl` |
| `AIconButton.qml` | 图标按钮 | 尺寸 28（大 44），悬停底色 `theme.alpha(iconColor, 0.18)`，tooltip 必需 |
| `ASearchField.qml` | 搜索输入 | 高 32，前置 `search.svg`，有内容时显示清除按钮，`placeholderText` 走 `qsTr` |
| `ACard.qml` | 卡片容器 | `theme.surfaceBg` + `radiusCard` + 1px `borderSubtle`，可选悬停/选中态 |
| `APill.qml` | 胶囊徽标 | `radiusPill`，`badgeBg`，`fontSizeCaption` |
| `ADialog.qml` | 模态对话框骨架 | 居中，`overlayBg` + `radiusOverlay` + 1px `borderSubtle`，标题 + 内容 + 按钮行（右对齐），`Esc` 关闭，打开时聚焦首个可交互元素 |
| `AToolTip.qml` | 统一 tooltip 样式 | `tooltipBg`/`tooltipText`，延迟 300ms，最大宽 360 |
| `AEmptyState.qml` | 空状态 | 图标（48px，`textMuted`）+ 标题 `fontSizeSubtitle` + 说明 `textMuted` + 操作按钮 |
| `ASectionHeader.qml` | 设置页分组标题 | `fontSizeSubtitle` + `textSecondary` + 下分隔线 |
| `AToastStack.qml` | 通知栈 | 右下角，`toastWidth`，最多 3 条，见 §12 |
| `PageHeader.qml` | 页面头部 | 标题 + 副标题 + 右侧操作区 slot |
| `AgentAvatar.qml` | agent 图标 + 状态点 | 由启动器卡片与 Web 标签复用 |

**不许**在没有复用价值时抽组件；也不许直接写裸 `Rectangle` 拼按钮——同一视觉元素出现第三次就抽组件。

> 状态备注（0.4.0 质量审查）：`ACard`、`ADialog`、`AToolTip` 是组件库骨架，当前内置页面尚未出现第三次复用（对话框只有设置页的删除确认/错误提示两处、卡片与 tooltip 都是页面定制形态）。**保留**它们作为插件与后续页面的可用原语，避免各页回退成裸 `Rectangle`；一旦出现复用点必须优先使用，而不是新写一份。

---

## 11. 全局快捷键

| 键 | 行为 | 作用域 |
| --- | --- | --- |
| `Ctrl+1` … `Ctrl+9` | 按 `order` 切换到第 N 个页面 | 全局 |
| `Ctrl+B` | 折叠/展开侧边栏 | 全局 |
| `Ctrl+,` | 打开设置页 | 全局 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 下一个 / 上一个 Web 标签 | Web 页 |
| `Ctrl+W` | 关闭当前 Web 标签 | Web 页 |
| `F5` / `Ctrl+R` | 重载当前 Web 视图 | Web 页 |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | 缩放 Web 视图 | Web 页 |
| `Esc` | 退出全屏 → 关闭浮层/对话框 → 取消加载 | 全局 |
| `F1` | 快捷键帮助（P2） | 全局 |

实现要求：Web 页相关的键必须用 `Shortcut { context: Qt.ApplicationShortcut }` 注册，否则会被 Chromium 先消费。

---

## 12. 通知与提示

| 形式 | 使用场景 | 规格 |
| --- | --- | --- |
| **Toast**（非阻塞） | 复制成功、安装完成、下载开始/完成、扫描部分失败、主题已切换、旧配置已接管 | 右下角，宽 340，`radiusOverlay`，左侧 3px 色条按级别取色（`info`/`success`/`warning`/`danger`）；自动消失：info/success 3s、warning 5s、error 8s；悬停暂停计时；最多同屏 3 条，超出排队 |
| **内联闪红** | 启动/停止失败 | 卡片边框闪 `theme.danger` 4s，状态位显示省略后的原因（沿用 0.3.0） |
| **模态对话框** | 启动失败详情、强制停止确认、删除 agent、退出确认、插件信任警告 | 必须可 `Esc` 关闭（退出确认除外），危险操作的确认按钮用 `danger` 变体 |
| **常驻条** | 配置写失败、部分根目录不可读 | 页面顶部一条，带「详情」「打开目录」等明确动作 |

文案规范：标题短（≤ 6 个汉字/40 字符），正文一句话说清「发生了什么 + 下一步做什么」；不要把异常堆栈直接当正文（放详情里）。

---

## 13. 国际化（i18n）

- 源语言英文：所有 `tr()` / `qsTr()` 的源串必须是英文；新增字符串先写英文，再补 `translations/agentworkbench_zh_CN.ts` 的中文译文（`lupdate` 生成条目，人工填译文）。
- 复数与数量：用 `qsTr("%n skill(s) found", "", count)` 形式，中英各自表述；不要在代码里拼 `+ "s"`。
- 占位符一律用 `%1`/`%2`，并 `.arg()` 按序填充；不要在字符串里手写拼接（语序在不同语言下会变）。
- 已有 15 个硬编码颜色之外的「数字/单位」也要走主题令牌或 `QLocale`（例如大小 KB/MB、时间格式用 `Qt.formatDateTime` 的本地化形式）。
- 品牌名 `AgentWorkbench` 不翻译。日志、注释、标识符一律英文。
- 新增语言：新建 `translations/agentworkbench_<locale>.ts`，加入 `cmake/AwbTranslations.cmake` 的清单，重新构建。

术语统一（英文 → 中文）：

| 英文 | 中文 |
| --- | --- |
| Agent | Agent（不译） |
| Launcher | 启动器 |
| Skill | Skill（不译；首次出现可写「Skill（技能）」） |
| Workspace | 工作区 |
| Surface / Embedded view | 表面 / 内嵌视图 |
| Theme / Token | 主题 / 令牌 |
| Console output | 控制台输出 |
| Force stop | 强制停止 |
| Re-initialize | 重新初始化 |
| Restore default launchers | 恢复默认启动器 |

---

## 14. 无障碍与高 DPI

- 最小点击目标 24×24（图标按钮 28×28，主按钮高 32）。
- 焦点环 2px、始终可见；Tab 顺序 = 视觉顺序；对话框打开时把焦点移入，关闭后还原。
- 不要只用颜色表达状态：运行/停止除了颜色点，还有文字状态（「运行中」/「已停止」）与 tooltip。
- 文字不设固定像素以外的缩放；`fontSize*` 令牌随主题可调，作为「界面密度」的实现基础（P2 提供紧凑/舒适两档）。
- 分数缩放（125%/150%）下：图标用 `sourceSize` 由 Qt 缩放（保留矢量），不要预渲染成固定 PNG；Web 视图的 DPR 由 WebEngine 处理，需人工验证清晰度与命中区域（见 `03-migration-plan.md` §6）。
- 中文输入法在 Web 视图内的候选框、内联组合、光标跟随必须人工验证（无法自动化）。
