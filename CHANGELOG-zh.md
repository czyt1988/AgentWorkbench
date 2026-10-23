# 变更日志

本文档记录 **AgentWorkbench**（0.3.0 及以前为 AgentLauncher）的所有重要变更。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，本项目遵循[语义化版本](https://semver.org/lang/zh-CN/spec/v2.0.0.html)。

## [Unreleased]

### 新增

- **Markdown 编辑器支持图片。** 从工作区文件树或资源管理器把图片拖进提示词
  编辑器或文档源码区，或在剪贴板里有图片文件/位图时按 `Ctrl+V`：应用在光标
  或落点插入 `![文件名](路径)`。
  - 路径相对文档。已经在文档目录里的图片原地引用；工作区内其它位置的图片写
    带 `..` 的路径；工作区外的图片先复制进图片目录。
  - 粘贴的位图保存成 PNG。图片目录在 **设置 → Markdown** 里配置
    （`markdownEditor.imageAssetsDir`，默认 `assets`，相对工作区根——没有
    工作区时相对文档目录；支持多级路径）。
  - 没有工作区的草稿把图片存到 `<dataRoot>/draft-assets/<草稿id>`，重启后
    仍然有效。
  - 剪贴板优先级：图片文件地址优先，其次是没有文本的位图；文本与富内容仍走
    编辑器原生粘贴。
  - 多张图片每行一张、整段一步撤销；只有预览能提供的图片格式算作图片，非
    图片文件保持原行为。
  - 内置预览改为先解码再编码资源路径，带空格与中文的图片名不再 404，图片的
    `alt` 文本也会保留。
- **Markdown 工作台编辑器：「输入手感」批次（A）**，每一项都可以在新的
  **设置 → Markdown** 分区里配置（`settings.json` 的 `markdownEditor` 组）：
  - **可配置缩进**——`Tab` / `Shift+Tab` 按一个缩进单位进退：默认插空格
    （每级 2 个，markdown 嵌套列表的可移植形态）而不是字面制表符；宽度可选
    （2/4/8），制表符模式一个开关即可切回
    （`markdownEditor.indentWithSpaces` / `indentWidth`）。
  - **智能回车**——在无序、编号、引用或任务项上按回车会延续标记（编号递增、
    已勾选的任务延续为未勾选）；空项上按回车退出列表
    （`markdownEditor.smartEnter`）。
  - **格式化快捷键**——编辑器聚焦时 `Ctrl+B` 加粗、`Ctrl+I` 斜体、`Ctrl+K`
    链接、`Ctrl+E` 行内代码。全局的 `Ctrl+B`（折叠侧栏）没有改键：编辑器只在
    持有焦点时经 shortcut-override 认领这个组合，别处一切照旧
    （`markdownEditor.formatShortcuts`）。
  - **自动配对**——输入 `*`、反引号、`~`、`[` 或 `(` 时补上闭合标记并把光标
    放到中间；再输入同一标记跳过闭合符，空标记对里再输入一次则升级
    （`*` → `**`、`~` → `~~`）（`markdownEditor.autoPair`）。
- **格式动作改为切换。** 加粗、斜体、删除线、下划线与行内代码在选区——或
  光标——已经位于同名标记对内时是去掉标记，而不是叠出第二对（`****x****`）。
  切换的判据与光标查询形式化的是同一套源码级标记配对。命令条、右键菜单与新
  快捷键走的是同一条路径，都只是一步撤销。

### 变更

- **工作台编辑器的 `Tab` 默认插空格**（每级 2 个）而不是字面制表符：markdown
  渲染器把制表符当代码缩进，会破坏嵌套列表。旧行为在 设置 → Markdown 里一个
  开关即可切回。
- **Markdown 编辑引擎独立成模块：`awb_markdownedit`。** `MarkdownEdit`（格式化
  动作、查找替换、Tab 语义）与 `MarkdownHighlighter` 从 `awb_tools` 迁入
  `src/markdownedit/`，编辑器右键菜单一并随迁；命名空间改为
  `awb::markdownedit`。该模块只依赖 `awb_theme`（外加 Qt，Qt Quick 为
  `PRIVATE` 链接），不再把提示词撰写域拽在身后，可以原样拷进另一个项目——集成
  步骤见 `docs/zh/development/Markdown编辑模块.md`。`awb_tools` 从此完全不依赖
  Qt Quick。行为不变：QML 单例仍叫 `MarkdownEdit`，各页面用法与之前完全一致。
  引擎的测试随迁为新的 `tst_markdownedit` 目标。
- **Markdown 工作台页拆成了组件。** 这个页面长到了三千行；能独立成件的区块现在
  各自成文件，并按「它到底依赖哪个门面」落位：工作区文件树面板与两个悬浮窗是
  `tools` 域的容器件，悬浮大纲是 markdown 域的展示件，可拖分隔条则是组件货架上
  的 `ASplitHandle`。组件的数据经属性传入、意图经信号报出，宿主复用它们时不必把
  某个域一起拽过来。页面只剩状态、动作与接线。
- **`check_architecture` 增加第六条规则**：组件货架里的文件不许引用领域门面
  （`tools.`、`markdown.`、`workbench.`、`agents.`、`web.`、`skills.`、
  `environment.`）。规则 1 只管 C++ 的 include，而所有 QML 编进同一个模块，
  货架此前完全没有门禁。
- **架构文档补上 QML 组件三层分工**（展示件 / 容器件 / 页面）——见
  `layers-and-dependencies.md` 第 5 节；`worktree-add.sh` 现在会初始化 SARibbon
  的嵌套子模块，否则每个新工作树都会在 CMake 配置期失败。

## [0.5.0] - 2026-10-13

本次版本把 Markdown 工作台做成应用的中心：提示词撰写与预览重新合回一扇窗口，
整条预览车道也随之进入 Qt 5 大版本；提示词页的命令条按 SARibbon 3.0 重建为
三个标签页；草稿变成一组，并带上版本历史、崩溃恢复、模板、收藏与体检面板；
外壳新增系统托盘与单实例守卫。

### 变更

- **提示词页的命令条重组为三个标签页。** **开始** 承载剪贴板、格式、列表、
  标题、插入与复制提示词动作，外加查找框；**提示词** 承载模板与收藏两个
  gallery、保存动作与体检/历史浮窗；**视图** 承载编辑器视图与 Ribbon 样式
  （三行样式退役）。主题桥改为把 `theme.chromeBg`（标签行底色）推给 SARibbon
  的 accent，深浅两套主题下命令条都跟应用外壳一致；Ribbon 图标是染色的
  data URL SVG。模板库改挂在 SARibbon 自己的 `RibbonGallery` 上（经上游新增
  的动态数据 API 填充），取代自绘面板；格式按钮不再可选中（选中态得跟随光标，
  而光标位置进不了 QML 绑定）；最左的应用按钮删除——它本无动作，标签栏也已经
  写明当前文档。页面这一侧：左栏只剩工作区文件树，右栏整体删除（体检与历史
  改为非模态浮窗），**新建草稿** 移到标签栏的 `+`，**重命名**/**删除** 移进
  标签右键菜单；查找统一——`Ctrl+F` 聚焦命令条上的查找框，草稿查找栏与预览
  搜索栏退役。
- **双击文件树的一行现在打开文件。** Markdown 文件落到新的工作台标签页里
  （按路径去重，超限以 toast 提示），其余文件——包括没有预览能力的构建里的
  `.md`——交给系统默认打开器。插入引用移到该行的右键菜单（**插入路径到光标
  处**）；拖放照旧插入。
- **工作区面板作为一个整体可拖宽、可整块折叠。** 宽度可在 180 到 520 px 之间
  拖动，拖过下界即折叠（带迟滞）；分区标题上的开关现在折叠整个左面板，而不是
  只折工作区分段——后者会清空面板却留下外框。边条上的按钮是唯一的恢复入口。
- **浮动大纲可以自己捏形状**：标题栏上的按钮把它折到只剩标题行（再点展开），
  角上的手柄拖高度、左边缘拖宽度（右边缘锚定不动）。几何与折叠状态属于页面
  记忆，与分屏比例一样不落盘。
- **不适用的菜单项改为置灰而不是隐藏。** 隐藏会在菜单里留下空位（菜单的
  contentItem 是 ListView），也让动作变得不可发现；非 Markdown 行上的
  **预览 Markdown**、文件标签上的 **重命名**/**删除** 现在就地灰掉。该规则
  已写进 `designs.md`。
- **Ribbon 标签页跟上游 3.0 的标签行为同步。** submodule 钉子再沿 dev-3.0 线
  前移（`7ecf2e6`）：双击标签页切换最小化模式、折叠状态下点击标签页预览该
  分类、上下文页标题恢复正常渲染，树内 QWindowKit 依赖换回上游原版 `1.5.0`
  发行版（被丢弃的本地补丁守的是一个析构顺序段错误，这里不可能发生——没有
  声明任何无边框 agent）。
- **Markdown 工作台回到了主窗口里。** 它不再是一扇独立窗口：侧栏的
  **Agent Tools** 分组回来了，其下的 **提示词编辑** 条目把工作台作为页面
  打开在主窗工作区里（常驻页，首次进入时创建——编辑器的撤销栈、滚动位置
  与分屏比例在切页之间原样保留）。**Markdown** 启动页薄壳删除；右键
  Markdown 文件照旧在工作台里打开，现在改为导航到页面。带着未保存的文档
  改动退出时由主窗的退出流把关（全部保存 / 放弃 / 取消）。右侧重新合并为
  一个带边框面板——大纲、体检与草稿历史作为边界清晰的段，不再是两块
  看起来像叠在一起的裸面板。
- **工作台 Ribbon 的上下文标签页恢复正常显示。** SARibbon 更新到上游
  dev-3.0 顶端（`d3d45d8`），修复了面板子项顺序错乱、面板内分隔线从不
  渲染、注册 category 的 hit-test 可见性三个问题；该版本硬依赖
  QWindowKit，由 SARibbon 自己的嵌套子模块链树内构建（工作台保留系统
  原生窗口边框——没有声明任何无边框 agent）。**提示词工具** 上下文页的
  颜色从 `theme.accent` 换成 `theme.success`：SARibbon 把整条标题行涂成
  accent 色，上下文色与之相同会让标签页与色带全部隐形——「上下文页与
  其它标签页混叠」的缺陷正源于此。
- **Ribbon 菜单跟随 SARibbon 3.0 的 QAction 命令层。** submodule 钉子沿
  dev-3.0 线前移（`423b8e8`），越过退役 2.x `RibbonMenuItem` 类型的重构
  ——命令条上的五个菜单按钮（标题、围栏代码、预览字号、正文宽度、Ribbon
  样式）改为以 `menuActions` 的 QAction 列表声明，Ribbon 重新装载得起来，
  不再静默退回普通工具栏。
- **Markdown 工作台页面在 Qt 5.15 车道重新装载得起来。** 它的三个上限
  列表（模板、收藏、草稿历史）用了 `ObjectModel`——Qt 6.5+ 才有的类型，
  Qt 5 兜底车道上「ObjectModel is not a type」直接整页打挂；现在改为普通
  JS 数组模型，两条车道行为一致。

### 新增

- **Markdown 预览进入 Qt 5 车道。** 此前它只存在于 Qt 6 + WebEngine，Qt 5.15
  构建因此永远停用一个 **视图** 标签页，也没有任何办法看到渲染结果。适配器、
  `:/markdown/` 运行时、预览面注册与预览冒烟测试现在两条大版本车道都构建。
  导航请求经兼容层应答（Qt 6 在请求对象上调 `accept()`/`reject()`，Qt 5 写它的
  `action` 属性——在 Qt 5 上直接调用是 `TypeError`，异常会静默放行这次导航），
  MIME 表去掉了 `; charset=utf-8` 后缀：Qt 5.15 的 job reply 只认裸 MIME，
  复合值会让整个响应不可解析（页面加载成空 HTML、所有脚本被丢弃）。
- **下划线**：Markdown 没有纯文本下划线记号，`<u>` 是唯一可移植的写法。预览
  仍保持 `html: false`（注入面不变）：恰好在 `<u>`/`</u>` 于同一内联片段内闭合
  时放行这一对，其余一律照常转义。编辑器负责写入这一对并报告光标处的下划线
  状态，高亮器用下划线字体把它画出来。
- **多个草稿**：工作台的提示词侧现在持有**一组草稿而不是一个**——各自占一个
  标签页（草稿标签与文档标签混排）、各自一份撤销历史、全部自动保存。
  **新建草稿**、**重命名**、**副本**与需确认且不可撤销的**删除**都在「开始」
  页上；关闭草稿的标签不会删掉草稿（只有「删除」会）。上限二十个，第二十一个
  会被拒绝而不是静默丢弃。**从文档生成**把当前文档的正文变成一份新草稿
  ——单向，草稿永不写回文件。
- **草稿版本与回滚**：复制、清空、替换或切走草稿时，应用会悄悄存一份快照；
  **保存版本** 可以动手标记一份。**历史** 面板列出这些版本（连同它们各自的
  产生原因），点一条即回滚——当前正文会先被存成新版本，所以回滚永不丢内容。
- **崩溃恢复**：应用意外退出后，下次启动会询问是否把上次会话的草稿正文恢复成
  一份新草稿，或丢弃它。只问一次，之后副本即清除。
- **格式化工具**：工作台窗口的**开始**页长出完整的 Markdown 工具栏——
  **粗体**、**斜体**、**删除线**与**代码**（各自反映光标处的真实状态）、
  **链接**、一级到三级标题、无序/有序/任务列表、引用、分隔线、带语言菜单的
  围栏代码块与表格骨架。每个动作都是一步撤销，按钮在文档标签的源码编辑器里
  同样可用，不只是提示词。
- **提示词内查找与替换**：`Ctrl+F`（或「开始」页的**查找**）从草稿编辑器上方
  滑入查找栏——下一个/上一个、替换一处并前进、全部替换（带计数、一步撤销），
  另有区分大小写与全词匹配开关，跨会话保留。文档标签里 `Ctrl+F` 照旧打开页面
  搜索。
- **提示词体检**：新的**体检**面板按十条常见提示词规则复查草稿——含糊措辞、
  缺少验收标准、Windows 风格路径、未填的 `{{占位符}}`、弯引号等——把发现列成
  可点击的建议，点一下跳到编辑器里的对应位置。不打分，只有清单。任何规则都能
  从它那条发现的 × 上关掉，并保持关闭。
- **令牌估算**：状态行显示草稿的 `~N tokens (estimated)`（打开的文档同样有一个
  这样的数字），颜色在上下文窗口的四分之一与五分之三处变化——明确是粗略估算，
  不承诺等于分词器结果。窗口基准默认 32000 令牌，可在应用的 `tools.json` 里改。
- **提示词模板**：工作台窗口左栏新增**模板**段——一个可搜索的提示词骨架架。
  内置十个（缺陷报告、功能需求、测试计划、代码评审、重构、文档、讲解代码、
  提交信息、复现问题、规划变更）；**自己的提示词可以存成模板**（标题、分类、
  描述），以纯 Markdown 文件形式放在应用数据目录里，一个模板一个文件——可手改、
  可备份、可分享，与内置模板同 id 的文件会覆盖内置。模板带
  **`{{占位符}}`**：插入带变量的模板会弹出一张小编表单（预填默认值，`Enter`
  插入、`Esc` 取消）；留空的字段视为有意跳过，`{{占位符}}` 原样留在正文里。
  两个占位符会自己填好——当前工作区与今天的日期；编辑器有选区时，选区内容
  作为 `{{selection}}` 的值。Ribbon 由此长出第一个**上下文标签页**
  **提示词工具**（仅在提示词活跃时可见）：**插入模板** 打开模板库，**存为
  模板** 把草稿存起来。无 Ribbon 的工具栏带同样三个按钮。
- **收藏**：已经定型、反复原样使用的提示词可以用**保存收藏**存下来，从模板
  下方的**收藏**面板一键插回（也可以直接在那一行复制到剪贴板）。存放在应用
  自己的存储里，最新在前，上限五十条。
- **Markdown 工作台的标签页与会话**：工作台窗口现在可同时打开**多个文档**
  ——一个文件一个标签，上限八个（第九个会被拒绝并说明原因，不会静默关闭任何
  东西）。标签带未保存圆点、各自记住 Preview/Split/Source，关闭标签只关视图、
  绝不关文件。切换标签是换掉那一个内嵌视图；关闭窗口会保留全部会话——再次
  打开时标签原样恢复，含未保存的改动。**草稿现在也有渲染预览**
  （Preview/Split/Source，默认分屏）：渲染文档的那条流水线同样渲染随写随变的
  提示词，长提示词可以就地检查结构、表格与图表。大纲面板同样服务草稿。
- **会话恢复与启动**：应用记住工作台打开的标签（含各标签的视图设置）、当前
  标签与窗口形状，下次启动后再次打开窗口时恢复——文件已被移动或删除的文档
  跳过，其余照常回来。「开始」页新增**最近文档**列表（六条，点一下打开）。
  默认启动时不打开工作台；新的**启动时打开 Markdown 工作台**开关（设置 →
  启动器，`markdown.autoOpenWorkbench`）为重度写作者打开它。
- **.md 文件关联**：在文件管理器里双击 `.md` 文件现在可以在工作台里打开它。
  应用已在运行时，文件作为新标签打开并把窗口带到最前；否则应用启动并显示它
  ——**完整应用**，刻意不做成轻量查看器。**关联 .md 文件**操作（设置 →
  启动器）写入注册项；默认应用仍由用户在 Windows 设置里自行选择，开关的说明
  也如实这么写。没有预览能力的构建会隐藏该操作，并在收到打开请求时说明原因。
- **Markdown 工作台窗口**：提示词撰写与 Markdown 预览现在是**同一扇独立窗口**
  的两副形态。**提示词形态**（从侧栏新增的 **Markdown** 启动页打开）承载原先
  提示词页的全部内容：左侧的工作区文件树、带高亮的草稿编辑器、拖放文件引用与
  格式化右键菜单。**文档形态**（在文件树上右键**预览 Markdown**）就是原先的
  预览窗口——大纲、文档内搜索、Preview/Split/Source、逐字节保存、导出——住在
  同一个外壳里，侧栏面板收起。**Full Tools** 开关在两副形态间切换；窗口存活
  期间两套界面状态互不干扰。窗口本身在**任何**构建里都能打开：没有 WebEngine
  的构建里提示词形态是纯编辑器，文档形态与预览面则根本不存在。仍然没有发送
  路径——窗口只以复制交付。窗口的 QML 住在 workbench 域
  （`src/workbench/qml/`），那是唯一允许横跨 tools 与 markdown 两个域的一层；
  预览面拆成 `MarkdownPreviewPane.qml`，只在注册了它的构建里按资源 URL 加载。
- **Ribbon 命令条**：工作台窗口的顶栏是单行模式的 SARibbon `RibbonBar`
  （经新的 `AWB_ENABLE_RIBBON` 选项构建；见后一条）。**开始**承载迁来的动作
  ——**复制提示词**、选区的剪切/复制/粘贴、撤销/重做、加粗/代码/项目符号列表、
  查找与保存；**视图**承载三种视图状态、大纲开关、Full Tools 形态开关、折叠
  开关、Ribbon 样式（单行/两行/三行）与**跟随主题**。应用按钮显示当前文档名。
  Ribbon 的配色经单向桥接跟随应用主题。`AWB_ENABLE_RIBBON=OFF`——或在没有注册
  SARibbon 的测试进程里——窗口退回带同样动作的普通工具栏。新增 `tools.json`
  的 `ribbon` 段（collapsed、style、followAppTheme、lastTabIndex）记住命令条
  状态；缺键取默认，没有迁移。
- **Ribbon 冒烟测试**：`tst_ribbonui`（仅在 `AWB_ENABLE_RIBBON=ON` 时构建）
  在 `saRibbonRegisterQmlTypes()` 之后把随包的 `WorkbenchRibbon.qml` 载入真实
  引擎，断言命令条布好版，且没有 SARibbon 警告（静态 qrc 初始化、注册时机、
  叶子创建）逃逸出来。
- **SARibbon 集成（仅构建）**：将来驱动 Markdown 工作台窗口的 ribbon 工具包
  现在是 git submodule（`third_party/SARibbon`，
  https://gitee.com/czyt1988/SARibbon，MIT，许可证随 submodule 一起走）。钉在
  `f89a02e`，那是最后一个 QML 模块只链接 Core/Gui/Qml/Quick/QuickControls2 +
  SARibbonCore 的提交；上游下一个提交会强制引入集成刻意不取的 QWindowKit 依赖。
  新增构建选项 `AWB_ENABLE_RIBBON`（默认 `ON`）把它作为静态库构建——只要 QML
  模块，不带 QWidget 栈、不加 DLL——并在引擎加载前经
  `saRibbonRegisterQmlTypes()` 注册类型；`OFF` 则让二进制不带 ribbon 且不破坏
  构建。目前还没有窗口用它；消费它的工作台窗口随后就到。离线初始化 submodule
  沿用 spdlog 的做法（`scripts/worktree-add.sh` 从已检出的副本克隆）。
- **提示词编写台重排版**：这一页（原「Agent 工具 · 提示词」，现名「提示词
  撰写」）左右互换——文件树在**左**，编辑区吃右侧工作区——工作区切换、
  选文件夹与刷新收进树顶部的一排紧凑控件（选文件夹是纯图标按钮，说明走
  tooltip；拖分割线调树宽时下拉框同步伸缩）。**复制** 移入页面头部，原工具
  栏整行删除；两个面板在分割线处的圆角去掉，树与编辑器以直角齐平相接。
  侧栏的「Agent 工具」分组保留，作为将来一族工具的预留位置；成员标题去掉
  冗余的「Agent 工具」前缀（注册 id 不变）。
- **Markdown 预览窗口**：在提示词撰写页的文件树里右键一个 `.md` 文件
  （**预览 Markdown**），它会在自己的窗口里打开。渲染完全离线，用的是一套内置
  的运行时（markdown-it 15.0.2、KaTeX 0.19.0、mermaid 11.17.2、highlight.js
  11.12.0），经本地的 `awbmd://` 协议供给：公式、图表、带语法高亮的代码，以及
  文档自己的相对路径图片。分块按时间预算分趟解析，屏外的块跳过布局，所以几百
  KB 的文档先出首屏（几百毫秒），不必等整篇排完。窗口还能改源码（`Ctrl+S`
  保存，未保存时有确认）、大纲导航、文档内搜索、**复制所选** / **复制
  Markdown 源码**、**从磁盘重新加载**、**在浏览器打开**与导出 HTML。设置键：
  `markdown.mode`（`preview` / `split` / `source`）、`markdown.fontSizePx`、
  `markdown.contentWidthPx`、`markdown.showOutline`——由窗口自己写入，设置页
  没有这一组开关。本版本起它在两个 Qt 大版本上都跑；**仅 WebEngine 构建
  可用**：`AWB_ENABLE_WEBENGINE=OFF` 时菜单条目不会出现，运行时也完全不进
  二进制（开启时约 4 MB，其中 2.6 MB 是 mermaid，只在文档含图表时才请求）。
- **系统托盘**：可选的托盘图标（显示/隐藏/退出菜单、左键切换显隐、一次性
  气泡提示）与「关闭选择」对话框——托盘可用期间应用不再随最后一个窗口
  关闭而退出；关闭窗口会先询问是退出还是收进托盘，直到选择被记住，
  所有退出都是显式的，agent 运行中依旧先询问。设置键：
  `launcher.trayEnabled`（默认 `true`）、`launcher.rememberCloseAction`（默认
  `false`）与 `launcher.closeToTray`（默认 `false`，记住的关闭动作），
  全部立即生效。shell 模块为此链接 `Qt::Widgets`。Windows 可能把新出现的
  图标收进「隐藏的图标」区，指引里写了怎么把它固定出来。
- **单实例**：可选的守卫（内置 SingleApplication 3.5.6），再次启动时把已有
  窗口带到最前而不是开出第二个。设置键：`launcher.singleInstance`（默认
  `true`），下次启动起生效。
- **Agent Tools 页**：面向 agent CLI 用户的提示词编写台——起草提示词
  （回车只换行，本页从不发送任何内容；Copy 把草稿放进剪贴板），旁边浏览
  工作区。最多记忆 20 个常用工作区文件夹（MRU、逐项移除、系统文件夹选择
  器），当前工作区渲染为懒加载文件树，带自动刷新（QFileSystemWatcher 防抖）
  与手动刷新；把树的条目拖进编辑区（插入到落点光标处）或双击文件行，即可
  插入 `` `./相对路径` `` 形式的文件引用；右键树的条目可复制它的相对路径或
  绝对路径。草稿跨页面切换与重启保留（`tools.json`）。（未发布期内这一页
  更名「提示词撰写」并重排版——见上面的「提示词编写台重排版」。）
- **可配置的文件树图标**：17 个文件类型图标与 8 个目录类型图标
  （`icons/filetypes/`、`icons/foldertypes/`），名字与后缀到图标的映射来自
  `config/default_file_icons.json`；`<dataRoot>/file_icons.json` 可以按键覆盖
  或追加，支持一个新后缀不需要改代码。
- **Web 页「主页」按钮**：标签栏工具栏新增主页入口，可回到运行中 agent 列
  表（无标签页状态）而不关闭任何已开标签——此前视图打开后就回不去列表，
  后续启动的 agent 无法从 Web 页打开。点击标签、从列表打开 agent 或
  `Ctrl+Tab` 轮换即离开该列表。
- **启动器卡片拆分按钮**：agent 运行中，"Open" 在**应用内打开 WebUI 并跳转
  到 Web 页**（此前标签在后台打开，还要手动切页）；旁边的箭头提供「在浏览
  器打开」（右键菜单亦有）。
- `tst_workbench` 测试目标：覆盖 `openWeb` 跳页契约、external surface 不建
  标签的路径与新的浏览器打开意图。

- **Agent Tools 页编辑区/文件树可拖拽分割**：中间分割条可拖动（主题化
  外观、加宽命中区），文件树保持首选宽度，编辑区吃剩余宽度。（未发布
  期内树移到左侧且两栏在分割线处去圆角——见「提示词编写台重排版」。）
- **全局字体设置**（settings.json 的 `appearance.fontFamily`，默认
  "Microsoft YaHei"）：引擎启动前经 `QGuiApplication::setFont` 应用、
  运行时经 `ApplicationWindow.font` 传播；本机没有该字体时自动回退系
  统默认。设置 → 外观里可选（"跟随主题" + 本机全部字体族）。动机：
  Win10 默认回退把整个 UI 渲染成宋体。
- **设置 → 环境里的运行时安装路径**：Python 与 Node.js 现在各占一行而不是挤在
  一起——上面是版本，下面是应答的那个可执行文件的完整路径（取运行时自己的
  `sys.executable` / `process.execPath`，取不到时回退到 `PATH` 解析结果），
  「重新检测」移进页面头部并带忙碌指示。
- **侧栏分组标签与缩进行**：侧栏的一行现在可以只是静态分组标题
  （`NavKind::Header`）而不是一个目的地，挂在它下面的条目按 `level`
  缩进。第一个分组是「Agent 工具」，其页面改名为「Agent 工具 · 提示词」
  并缩进挂在标题下（英文界面为 "Agent Tools" / "Agent Tools - Prompt"）——
  该标题不可点击、不会高亮、不占 `Ctrl+1…9` 的序号、不带徽标、当不了当前
  页，侧栏折叠成图标条时还会整行隐藏。页面的注册 id 仍是 `tools`，因此
  记住的「上次页面」照样恢复。新增 `tst_shell` 用例
  （`testSidebarGroupLabel`）覆盖。（未发布期内：成员标题去掉了冗余的
  「Agent 工具」前缀，读作「提示词撰写」——见上面的「提示词编写台重
  排版」。）

### 变更

- **技能卡悬停不再抬升**：悬停高亮（更亮的玻璃、更强的描边、指针聚光）现在
  就地生效，不再把整张卡片顶起一个间距单位——指针扫过卡片边缘时那一下跳变
  就是它造成的。
- **Python/Node 检测重写**（此前一次仅仅超时的探测就会显示红叉）：检测改到
  线程池上跑而不是 GUI 线程，依次尝试 `python`/`python3`/`py` 与
  `node`/`nodejs`（这样 Windows 商店的占位程序会输给真正的解释器），直接运行
  解析出的可执行文件而不经 `cmd /c`，并问运行时自己住在哪里。超时或启动失败
  不再等于「未安装」：保留上一次的判定，并按 3 s / 15 s / 60 s 重试，每轮细节
  进日志与设置行 tooltip。判定缓存进 `<dataRoot>/environment_cache.json`，
  状态栏与设置页启动即填入，只有重新检测真的发现不同时才变；还没有判定时徽标
  显示省略号而不是红叉。
- **设置页重构为分页**：左侧固定导航列（外观/启动器/环境/Skills/Web/
  插件/高级），右侧一次只显示一个分区（StackLayout，页面常驻，切页不
  丢半截输入）——此前七个分区挤在一个长滚动页里；对话框移入各自的分
  区页。
- **侧栏页脚重构**：系统页（设置）不再作为整行条目钉在折叠手柄上方——它们变成
  纯图标按钮（tooltip 显示标题，`AIconButton` 新增 `active` 状态，为当前目的地
  填 `surfaceBg`），与手柄共用一行页脚：展开时并排，折叠时上下排（图标在上、
  手柄在下、水平居中）。折叠后的侧栏宽度从 64 px 收到 42 px——正好一个导航图标
  的宽度。
- **字号阶梯统一为四级**：全部文字现在只用四个字号令牌——
  `fontSizePageTitle`（24）、`fontSizeSubtitle`（16）、`fontSizeBody`（13）、
  `fontSizeCaption`（11）。`fontSizeSmall` 与 `fontSizeCardTitle` 两个令牌删除
  （用法分别并入 caption 与 subtitle；caption 这一级本身从 10 提到 11，让辅助
  文字保持可读），阶梯连同图标渲染规则一起写进 `designs.md`（见「修复」）。
- **技能卡去掉复制按钮**：卡片页脚现在只显示文件夹路径——点卡片照样复制它，
  右键菜单保留全部复制动作，所以那个按钮纯粹是视觉负担。
- **侧栏顶部标题块移除**：应用图标 + 名称块（及其分隔线）不再渲染在侧栏顶部，
  侧栏现在直接以工作流导航开始——窗口标题栏已经承载了应用身份。
- **侧栏指针聚光**：鼠标在侧栏上移动时，会有一束淡淡的光跟着指针走过整块
  面板，并沿面板边缘描出 2 px 流光——与启动器卡片同一套效果（`ASpotlight`
  新增 `spotRadius` / `ringRadius` 覆写，供非方形的通高面板按宽度取半径；
  `spotAlpha` / `ringAlpha` 调两者的峰值不透明度）。两个主题都以 accent 为
  光源，只是各调一档：暗底上 accent 本身就是光，所以两个峰值都比卡片浓度低
  一档；近白的底已经没有亮度可涨，accent 的价值就剩下它带来的色相偏移，所以
  浅色的光斑是很淡的一层、并且收在栏宽以内（再浓就是浅底上的一块蓝渍）。导航
  行的「当前项 / 悬停」胶囊改成半透明是因为聚光画在内容之下：实心的它们正好
  把光挡在指针停住的地方。

### 修复

- **拖一下工作区分割线就把文件树折没了。** 宽度算式减掉的是面板当前宽度，而不是
  按下时记录的抓取偏移，于是每次拖动都落进折叠分支；手柄本身还会随面板缩放重新
  定位，宽度必须是指针的绝对位置减去按下时的抓取偏移。`tst_toolsui` 新增回归
  用例，用真实鼠标事件驱动（+80 加宽而不折叠，拖过阈值即折叠）。
- **文件标签上的「关闭标签」毫无动作**——它在那类标签上静默返回；现在与其它
  标签一样关闭。
- **Markdown 预览在 Chromium 87–97 上能把渲染进程拖崩。**
  `contain-intrinsic-size: auto …` 要 Chromium 98 才有；老引擎上这个复合值是
  非法的，被 display-lock 的屏外块因此塌成零高度（滚动高度是假的，滚动条因此
  短得没用），Chromium 87 则在由此产生的滚动驱动锁定切换上崩溃。页面现在在挂载
  第一块之前用 `CSS.supports('contain-intrinsic-size', 'auto 48px')` 探测，把这些
  引擎降级为普通布局——换来真实的滚动高度与活着的渲染进程，代价是屏外跳过的
  收益（那项收益只在能力完整的引擎上回来）。车道自检也不再每次启动都留一条
  控制台警告。
- **松散（两行）Ribbon 样式下，「开始」页的纯图标按钮被画得过大**——SARibbon
  把未设置的 proportion 解析成大按钮，而大号纯图标按钮会把图标拉伸到整个按钮
  高度。松散样式改为图标带文字；紧凑单行仍是纯图标。
- **「代码块」把单行选区围成了行内代码。** 现在一律围整行；光标在空行上时落一段
  骨架，并把光标停在两道围栏之间。
- **技能根扫描噪声**：不存在的 skill 根（插件缓存里升级后消失的旧版本路径、
  没有 `.claude/` 的项目）是正常跳过而不是异常——每次启动四行 WARNING 降为
  debug。
- **图标按钮渲染模糊且过大**：`AIconButton` 把图标的 `Image` 直接当作 Control
  的 `contentItem`，控件于是把它撑到 28×28，`PreserveAspectFit` 又放大 16 px 的
  位图——应用里每个图标按钮（设置齿轮、折叠箭头、Web 标签工具栏）都因此以
  1.75 倍大小经双线性过滤画出来。图标现在居中放在 `Item` 里，按 `sourceSize`
  1:1 渲染。Agent Tools 文件树的箭头补上了对应的 `sourceSize`（没有它的 SVG
  会按自然 24 px 栅格化再缩小，因而发虚）。
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
- **Agent Tools 文件树刷新不再整棵闪烁。**`refresh()` 原先 reset 模型，而
  TreeView 对 reset 的反应是销毁全部 delegate 并把整棵树收起，页面再按快照
  逐层展开回去。现在刷新对每个已读取的目录与磁盘做增量对账，只发真正变化的
  行——目录没变时一个信号都不发，展开状态原地保留。展开目录仍会让 TreeView
  回收并复用所有可见 delegate（这是 Qt 的行为），因此行被换绑到别的数据时
  不再播放箭头旋转动画。
- **把文件引用拖进提示词编辑区，现在落在光标处。**落点计算里加了编辑区的
  `contentX`/`contentY`，但 `TextArea` 不是 `Flickable`、没有这两个属性——
  相加得到 `NaN`，`positionAt()` 返回 0，于是每次都插到文首。

- **Qt 5 下 Agent Tools 的 "Add Folder" 点击无响应**：门面 Q_INVOKABLE
  的返回类型写的是短名 `core::OpResult`，而 gadget 的自动元类型注册名
  是全限定 `awb::core::OpResult`，QML 调用端按名字解析返回类型查不到
  就抛 "Unknown method return type"（SkillCard 的复制/打开操作同样静默
  失效）。全部声明改为全限定名；新增共享测试守卫
  （`awbUnresolvedQmlCallTypes`）在类型解析失败时让测试挂掉，另有
  QJSEngine 端到端测试复现脚本调用路径。Qt 6 走模板解析，不受影响。
- **提示词编辑区无法用鼠标选中文字**：`TextEdit.selectByMouse` 默认
  false 且 Controls 2 样式不覆盖；`ATextArea` 现在显式开启（有选区时
  Ctrl+C 一直可用）。
- **启动时 "Qt Quick Layouts: Detected recursive rearrange" 警告**：
  空态组件的内层容器是 ColumnLayout，「高度自动跟随 + WordWrap 描述文
  字 + 宿主首次拿到真实尺寸」叠出第三层同步重排（超出 Qt 5 允许的深度
  两层）。内层容器改为普通 Column positioner——tools/agents/skills/
  settings/web 各页反复启动验证零警告。
- **Skills 页刷屏 "Binding loop detected for contentHeight"**：
  SkillDetailFlyout 的高度读了 contentItem（Flickable）的
  contentHeight，与 contentItem 尺寸互馈。弹层改为从 ColumnLayout
  contentItem 取隐式高度、Flickable 经 Layout.preferredHeight 封顶，
  contentHeight 异步回填（Qt.callLater）。
- **设置页主题选择框重进后是空的**（且显示 "Catppuccin Mocha (Dark)"
  这类难懂名称）：`currentIndex` 绑了 `indexOfValue(themeId)`，在
  ComboBox 的 delegateModel 就绪前求值成 -1 且永不重算。改为遍历带
  NOTIFY 的 `theme.availableThemes` 计算 index；下拉显示 Dark/Light 短
  标签（完整名称进 tooltip）。
- **Agent Tools 的提示词编辑区无法滚动。**编辑区原来是一个裸 `ATextArea`，
  而 `TextArea` 自己的文本滚不动：提示词一超出可视区，剩下的内容就再也够
  不着——没有滚动条，滚轮和拖拽也都无效。现在编辑区是纵向 `Flickable`
  （`clip`、`StopAtBounds`），内容体经 `TextArea.flickable` 附加属性挂上
  `ATextArea`，并配 `ScrollBar.vertical: AScrollBar`。附加属性补齐了滚动需要
  的三件事：文本随内容高度增长、每次 `cursorRectangleChanged` 把光标滚回
  视野、以及把文本组件自身的背景（外框、焦点环）改挂到 `Flickable` 上并按
  它定尺寸——外框因此钉在视口上，不会跟着文本一起滚走。新增一条
  `tst_toolsui` 用例锁住滚动范围、滚动条的出现与实际位移。
- **侧栏页面标题从未被翻译**：Qt 6 的 `moc` 把带命名空间的完整类名写进
  meta-object，因此处在命名空间里的类调用 `tr()` 时，用的是
  `awb::workbench::BuiltinPages` 这一档上下文；而 `lupdate` 往 `.ts` 里记录
  的是裸类名 `BuiltinPages`。两把钥匙对不上，侧栏标题便一律静默回落成英文，
  构建与测试都察觉不到。现在 `BuiltinPages` 显式调用
  `QCoreApplication::translate("BuiltinPages", …)`，上下文与 `lupdate` 写出的
  一致，它的标题（含新的分组标签与「Agent 工具 · 提示词」）恢复翻译。其余带
  命名空间的类仍有同样的错配，已在 `docs/zh/development/国际化.md` 说明。

### 移除

- `examples/plugins/hello` 示例插件与 `AWB_BUILD_PLUGIN_EXAMPLES` 构建选项；
  插件文档改用内联示例。

### 文档

- 文档重组为三个板块，每一页都有对应的中文镜像：**使用指引**（`docs/guide/`）
  站在用户角度讲每个功能怎么用、怎么配；**架构**（`docs/architecture/`）讲分层、
  前端与 C++ 库设计原则、扩展点，配 Mermaid 图；**开发**（`docs/development/`）
  每个功能一篇，点名涉及的文件与类，讲清前端设计、后端设计与业务逻辑。原来的
  单文件 `docs/development.md` 变成 `docs/development/index.md`。
- 新增 `docs/AGENTS.md` 固化文档撰写规范：目录骨架、英文为源 / 中文必须同步、
  各板块的读者与语气、全文档不写行号、Mermaid 与链接约定，以及「改了代码要同步
  改哪些文档」的对照表。顶层 `AGENTS.md` 已引用它，并把「过一遍文档」写进
  「做完」的定义。

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
