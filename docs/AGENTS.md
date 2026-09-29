# docs/AGENTS.md — 文档撰写原则

本文件约束 `docs/` 下的**全部文档**：写作前先读它。它只回答「文档怎么写、写在哪、什么时候改」，不回答「代码怎么改」——那两件事分别见顶层 `AGENTS.md`（工程契约）与 `designs.md`（界面设计）。

优先级：顶层 `AGENTS.md` > `designs.md` > 本文件。冲突时按此顺序取，并回来修订本文。

---

## 1. 目录骨架与中英文同步

```
docs/
  index.md                 站点首页（项目定位 + 快速开始 + 通往三大板块的入口）
  configuration.md         配置参考（agents.json / settings.json 的完整字段表）
  plugins.md               插件编写参考（面向插件作者）
  architecture/            【架构】软件是怎么搭起来的 —— 面向新加入的开发者
  development/             【开发】单个功能是怎么实现的 —— 面向改代码的人
  guide/                   【使用】单个功能怎么用、怎么配 —— 面向最终用户
  standards/               编程规范（代码风格，不是产品文档）
  research/                调研记录（一次性结论，冻结不改写）
  superpowers/plans/       面向 agent/维护者的计划文档：不在导航里、不做中文镜像、不进本节的对照表
  zh/                      以上全部内容的中文镜像（文件名用中文，见下表）
```

**双语规则只约束 `architecture/`、`development/`、`guide/` 三个板块**（以及本就在 `zh/` 下有镜像的既有页面）。`standards/`、`research/`、`superpowers/plans/` 里的文档按各自需要单语撰写，不要为了「凑齐镜像」给它们补一份翻译。

**中英文必须同步、一一对应**，规则如下：

- **英文是源头**。先写/改英文，再补中文；中文不是「有空再说」的选项，同一笔提交里必须一起出现。
- **英文侧的文件名用英文，中文侧的文件名用中文**：`docs/architecture/cpp-design.md` ↔ `docs/zh/architecture/C++库设计.md`。`index.md` 三个板块都保持 `index.md` 不改名；板块之外的既有页面（`configuration.md`、`plugins.md`、`standards/`、`research/`、`blog.md`）也保持英文名不动。不新建 `xxx-zh.md`、也不在中文目录里加英文特有文件。
- **对应关系是 1:1 且必须登记**：新增/改名中文页时，同步更新下面这张表；表是核对中英是否齐平的唯一依据（两边文件名不同，不能再靠同名自动比对）。

  | 英文 | 中文 |
  |---|---|
  | `architecture/layers-and-dependencies.md` | `architecture/分层与依赖.md` |
  | `architecture/frontend-design.md` | `architecture/前端设计.md` |
  | `architecture/cpp-design.md` | `architecture/C++库设计.md` |
  | `architecture/extension-points.md` | `architecture/扩展点.md` |
  | `architecture/state-and-persistence.md` | `architecture/数据与状态.md` |
  | `development/agent-launcher.md` | `development/Agent启动器.md` |
  | `development/web-tabs.md` | `development/Web标签页.md` |
  | `development/webengine-adapter.md` | `development/WebEngine适配层.md` |
  | `development/skill-browser.md` | `development/Skill浏览.md` |
  | `development/agent-tools.md` | `development/AgentTools.md` |
  | `development/shell-and-navigation.md` | `development/外壳与导航.md` |
  | `development/settings.md` | `development/设置.md` |
  | `development/theme-engine.md` | `development/主题引擎.md` |
  | `development/workbench-and-pages.md` | `development/workbench与页面.md` |
  | `development/plugin-host.md` | `development/插件宿主.md` |
  | `development/core-infrastructure.md` | `development/core基础设施.md` |
  | `development/i18n.md` | `development/国际化.md` |
  | `guide/agent-launcher.md` | `guide/Agent启动器.md` |
  | `guide/web-ui.md` | `guide/网页界面.md` |
  | `guide/skills.md` | `guide/技能.md` |
  | `guide/agent-tools.md` | `guide/提示词编写台.md` |
  | `guide/appearance.md` | `guide/外观.md` |
  | `guide/settings.md` | `guide/设置.md` |
  | `guide/plugins.md` | `guide/插件.md` |
  | `guide/troubleshooting.md` | `guide/问题排查.md` |

- **结构必须一致**：一份英文文档有哪几个二级标题，中文镜像就有哪几个；图表、代码块、表格、链接目标一一对应。允许中文行文更简洁，但不允许缺章节。
- **代码/路径/类名/命令/配置键不翻译**，原样保留（`AgentsFacade`、`agents.json`、`theme.surfaceBg`、`AWB_ENABLE_WEBENGINE`）。中文文档里也不需要给它们加中文别名。
- **新增一个文档 = 改四处**：英文文件、中文文件、`mkdocs.yml` 两个 locale 的 `nav`（两个 locale 各写各的文件名）、本文的对应关系表。

### 中英文件名不同的两个代价（用前必须知道）

MkDocs 的 i18n 插件是**按「剥掉语言目录后的路径」给两种语言配对的**（`mkdocs_static_i18n/folder.py` 的 `norm_src_uri`），所以中英文件名一旦不同：

- **语言切换器只能回到对方语言的首页**，不再跳到「当前页的对应页」。这是插件机制的硬限制，改 `mkdocs.yml` 解决不了；能接受就说「切语言≈换站点」，不能接受就得让两边同名。
- 插件默认的 `fallback_to_default: true` 会把**没有同名译文的英文页整页复制进 `/zh/`**（实测 25 个英文副本，且会进搜索索引），所以本站关掉了它（`mkdocs.yml` 里 `i18n.fallback_to_default: false`）。副作用是**共享资源不再自动带进中文站**：`docs/pic/` 的截图在 `docs/zh/pic/` 有一份副本，换图时两边都要换。

构建输出里那串「pages exist in the docs directory, but are not included in the nav」的 INFO 是上面第一条的连带噪声（每次构建会把另一种语言的页面列一遍），属预期，不要为它加 `not_in_nav` 把英文页也一并静音掉。

## 2. 三个板块的读者与语气

写之前先回答「这篇给谁看」，语气和详略都由读者决定。**同一件事不要在三个板块里抄三遍**——各写各的角度，互相链接。

| 板块 | 读者 | 允许出现的概念 | 不该出现的 |
|---|---|---|---|
| `architecture/` | 新加入的开发者、评审者 | 分层、依赖方向、设计取舍、契约、权衡与代价 | 逐功能的实现细节、UI 操作步骤 |
| `development/` | 要改这个功能的开发者 | 文件、类、信号槽、模型 role、配置键、线程、时序 | 用户操作教程、与代码无关的营销话术 |
| `guide/` | 最终用户 | 「这个功能做什么」「按钮在哪」「怎么配置」「出问题怎么办」 | **任何代码文件、类名、函数名、行号、架构术语、开发者黑话** |

`guide/` 的硬红线：**不出现 `src/`、`.cpp`、`.qml`、类名、函数名、内部机制名**。用户不需要知道 `AgentHealthMonitor`，只需要知道「卡片上的圆点表示它在跑」。要描述行为就描述**用户看得见的现象**（「每 3 秒检查一次」「点卡片会打开它的网页界面」）。

## 3. 通用写作规则

1. **绝对不写行号**。全文档统一：引用代码只到「文件 + 类/函数/属性名」，如 `src/agentcatalog/AgentRuntime.cpp` 的 `launch()`。行号随开发漂移，写进去就是负债。
   - 唯一例外：文档本身就是「粘贴某段代码」的示例（插件作者示例、配置片段），只贴内容不标行号。
2. **文件名写相对仓库根的路径**，用反引号包住并在同一句里说明它是什么：`src/web/WebTabsFacade.cpp`（标签生命周期门面）。**不要**贴本机绝对路径（Windows 盘符路径、用户目录展开后的路径都不要出现）。
3. **类与成员用反引号**：`WebTabsFacade`、`openTab()`、`web.surface`。同一段落里首次出现给一句职责说明，之后直接叫名字。
4. **配置键写全路径**：`web.maxLiveTabs`、`skills.roots`、`plugins.disabledIds`。写键时顺手给默认值和取值范围。
5. **面向用户的字符串是英文源串**（与 i18n 规则一致）：文档里引用界面文案时写英文 + 中文镜像写界面的实际中文（若已翻译）。不要把「文档里的示例界面文案」写成中文后再让代码去对齐。
6. **术语统一**，不要同义词轮换：

   | 固定用词 | 不要写成 |
   |---|---|
   | agent（小写，指被启动的工具） | Agent / 智能体 / 助手 |
   | launcher（启动器页/启动能力） | launcher page / 启动面板 |
   | surface（Web 表面，`embedded`/`external`） | 容器 / 视图层 |
   | tab（标签页）、view（视图） | 页签 / 窗口 |
   | theme token（主题令牌，`theme.*`） | 主题变量 / 颜色常量 |
   | data directory（数据目录） | 配置目录 / 用户目录（除非确实指 `configDir` 字段） |

7. **不要复述代码**。文档写「为什么这么设计、边界在哪、坏了怎么表现」，不写「第 1 步调用 A，第 2 步调用 B」这种读代码就能得到的东西。判断标准：如果一段话在函数改名后立刻过期，那它就不该出现在 `architecture/`。
8. **写明代价与坑**。本项目大量设计是「两害相权」的结果（例如冻结标签默认关、`stop()` 只杀本会话进程、内置 agent 每次启动重生成）。文档必须把**取舍和后果**写出来，否则下一个人会把它当 bug「修」掉。
9. **段落不要用箭头链**：`A → B → fails` 这种压缩写法读者要猜。写成完整句子。
10. **不要写「未来计划」**，除非它已经是代码里的 TODO 且有明确触发条件。文档描述现状。

## 4. 图：一律 mermaid，且必须能被读懂

`architecture/` 与 `development/` 鼓励配图；`guide/` 不用 mermaid（用户不看架构图，必要时用文字步骤或列表）。

- 语法用围栏 ```mermaid，MkDocs 侧由 `pymdownx.superfences` 的 `custom_fences` 转交 mermaid.js 渲染（`mkdocs.yml` 已配置）。
- **图要能单独读懂**：每张图前面一句「这张图在说什么」，图内节点用**真实存在的类名/模块名/配置文件**，不要造 `ModuleA` 这类代号。
- 选型：
  - 层级/依赖 → `flowchart`（`subgraph` 表示层，箭头方向 **只能由上层指向下层**，与真实依赖方向一致）；
  - 时序/生命周期 → `sequenceDiagram`（例如启动 agent 到标签页打开）；
  - 状态机 → `stateDiagram-v2`（例如 `WebTab` 的 `loading/ready/offline/crashed/error/released`）；
  - 目录/文件树 → `flowchart` 或代码块（结构树优先用代码块，别用图硬画）。
- **深色/浅色主题下都要可读**：不要在图里写死颜色（不要 `style X fill:#...`）；MkDocs Material 会按主题自动适配 mermaid。
- 一张图不超过约 20 个节点；超了拆成两张。节点标签过长就缩写成真实短名（`WebTabsFacade` 可以写成 `WebTabsFacade`，但不要缩成 `WTF`）。
- **离线预览时图不会渲染，这是环境问题不是配置问题**：MkDocs Material 在浏览器端从 CDN（`unpkg.com`）拉 mermaid，没网时图会退化成原始代码块。构建产物里 `pre class="mermaid"` 与 `class="mermaid"` 是对的，**不要为此改 `mkdocs.yml` 或把 mermaid 塞进仓库**。要确认语法，用本地任意一份 mermaid 11 校验（`mermaid.parse` + `mermaid.render` 全部跑通）即可。
- 图与正文不得互相矛盾；改代码改到图里的东西时，图要一起改（见 §6）。

## 5. 链接与导航

- **站内链接用相对路径且带 `.md`**：英文侧同板块内写 `agent-tools.md`、跨板块写 `../development/agent-launcher.md`；中文侧写的是**中文文件名**（`AgentTools.md`、`../development/Agent启动器.md`），同一板块内两边只有文件名不同。不要用绝对站点路径（`/development/...`）。
- 跨板块链接是**必须的**：`architecture/` 说「详见 `development/` 的某功能」，`development/` 头部给出「用户视角见 `guide/` 对应页面」，`guide/` 末尾给 `development/` 的链接（方便好奇的用户/维护者）。
- 每个板块必须有 `index.md`，作为该板块的目录 + 阅读顺序建议；板块首页要能回答「我该按什么顺序读」。
- 新增页面必须登记进 `mkdocs.yml` 的两个 `nav`；漏登记的表现是页面能访问但导航里找不到。
- 不要在文档里链接到仓库外的临时资源（本地截图路径除外，截图放 `docs/pic/`）。
- **中文标题的自动锚点是空的**：MkDocs 的 slugify 会把非 ASCII 字符整个丢掉，`## 图标配置` 生成不出可用的 `#…` 锚点，写 `[文字](#图标配置)` 会得到一个坏链接（`mkdocs build` 会警告 "no such anchor"）。中文文档里要指代另一节，**用文字描述「见下文『某节』」**，不要写锚点；确有必要时用 `attr_list` 显式给标题一个 ASCII 锚点。

## 6. 维护：什么时候必须改文档

**每个任务收尾时都要过一遍这张表**——这不只是「有空再补」，是完成的定义的一部分（顶层 `AGENTS.md` 已把这条列为原则）。

| 你动了什么 | 至少更新 |
|---|---|
| 新增/删除一个页面、侧栏条目、快捷键 | `development/` 对应功能文档、`guide/` 对应功能文档、`docs/index.md` 功能列表 |
| 新增/重命名 QML 组件（`A*`）、改卡片视觉配方 | `architecture/frontend-design.md` |
| 改动某功能的行为、状态、边界（例如标签释放策略、健康检查语义） | `development/` 对应功能文档（含图）、`guide/` 里描述该行为的那句话 |
| 新增/改名一个设置键、配置字段 | `configuration.md` + `docs/zh/configuration.md`、`guide/settings.md` 对应段落、`development/` 里用到它的文档 |
| 新增插件 ABI 能力、改 `ApiVersion` | `plugins.md`（两个语言）、`architecture/extension-points.md` |
| 改构建选项、测试目标清单、Qt 版本支持范围 | `development/index.md`、`architecture/cpp-design.md`（若涉及兼容策略） |
| 新增一个模块 / 改动模块依赖方向 | `architecture/layers-and-dependencies.md`（含依赖图） |
| 界面上用户能看到的文案改了 | `guide/` 里引用该文案的地方（两个语言都要对） |
| 新增/改名/删除一个中英对照的页面 | §1 的对应关系表 + `mkdocs.yml` 的中文 `nav` 条目 |
| 换了 `docs/pic/` 里的截图 | `docs/zh/pic/` 里的同名副本一起换（中文站不再自动继承共享资源） |

反过来也成立：**文档说的和代码不一致时，以代码为准，并当场把文档改对**。发现别人留下的过期文档，顺手修，不要复制它的写法。

## 7. 提交前检查清单

- [ ] 英文写完，中文镜像已同步（章节、图表、表格、链接逐项对得上）。
- [ ] 新增页已按 §1 的对应关系表配好中文文件名，并把新行补进那张表。
- [ ] 全文无行号；代码引用只到文件 + 类/函数名。
- [ ] `guide/` 全文无代码文件名、类名、函数名、架构术语。
- [ ] 新增页已登记进 `mkdocs.yml` 两个 `nav`（各自写自己的文件名）。
- [ ] mermaid 图语法正确、不写死颜色、节点用真实名字。
- [ ] 站内链接可点：英文侧用英文名、中文侧用中文名，相对路径 + `.md`，在对应目录下真的存在。
- [ ] `mkdocs build` 无新增错误（中英文都构建），且 `/zh/` 下没有多出来的英文页副本。
- [ ] 该板块的 `index.md` 已把新页面列进去。
