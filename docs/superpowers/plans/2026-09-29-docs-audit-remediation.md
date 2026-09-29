# 文档反查发现的整改计划（2026-09-29）

> 这是面向维护者与 agent 的**工作文档**，不是站点页面：它不进 `mkdocs.yml` 的导航，也**不做中英文镜像**（`docs/AGENTS.md` 的双语规则只约束 `architecture/`、`development/`、`guide/` 三个板块）。做完一项就把该项划掉；全部做完后本文件可以删除或归档。

**背景**：2026-09 补写 `docs/architecture|development|guide` 时逐页对着源码核对，发现一批「文档想写、代码却不支持」或「配置项存在但没人消费」的问题。文档已按**现状**如实撰写（没有描述不存在的行为），这份计划是把它收敛掉的动作清单。

**范围**：只处理下面 6 项。不改文档结构、不重命名文件（中文文件名改造已于 `cdd1abb` 完成）、不加新功能、不改 UI 骨架。

**通用验收**：每项完成后 `bash scripts/build.sh --test` 全绿，并按 `docs/AGENTS.md` 第 6 节的对照表同步文档（行为 → 中英 `development/` + `guide/`；设置键 → `configuration.md` 中英 + `guide/设置.md`/`settings.md` + `development/设置.md`/`settings.md`）。

---

## T1 状态栏的 `Tabs:` 标签永远不显示（P1，用户可见）

**证据**：`src/shell/qml/StatusBar.qml` 的第二个统计标签 `visible: nav.countInSection("web") > 0 && tabs.length > 0`；`NavigationModel::countInSection()` 统计的是页面的 `section` 字段，而 `BuiltinPages::registerPages()` 把 Web 页注册成 `section = "main"`，没有任何页面是 `web`，条件恒假。

**边界要说清**：**标签数本身是好的**——`BuiltinPages::wireBadges()` 把标签数写进 `nav.setBadge("web", …)`，`Sidebar.qml` 通过模型的 `badgeText` 角色显示为侧栏徽标。死掉的只有状态栏这一个标签。

**两个方案**：

- **A（推荐）删掉这个标签**。理由是同一信息侧栏已经显示，状态栏再放一份是重复；删掉 `StatusBar.qml` 里那个 `Label` 即可，`nav.badges["web"]` 与 `wireBadges()` 不动（侧栏还要用）。
- **B 修条件**：改成绑门面的真实标签数（如 `web.tabCount > 0`），在状态栏显示 `Tabs: N`。需要 `StatusBar.qml` 能拿到 Web 门面（现在只依赖 `nav`/`environment`），等于给状态栏加一个跨模块依赖，代价比 A 大。

**验收**：状态栏行为与中英 `guide/index.md` 的描述一致（该页已改成只提运行数与运行时徽标）；`tst_shell` 补一条断言（若选 B，断言标签在 `tabCount > 0` 时可见）。

**需要你拍板**：状态栏到底要不要显示标签数？

## T2 三个设置键有读写、无消费者（P1–P2）

**证据**：全仓 grep 只命中 `src/core/Settings.*` —— `appearance.followSystem`、`web.homeUrl`、`launcher.startupVersionCheck`。三者都出现在 `docs/configuration.md`（中英）的键表里，`web.homeUrl` 还没有任何 UI 控件。

逐项建议：

- `appearance.followSystem`（**实现**，工作量中等）：让主题跟随系统深浅色。Qt 6.5+ 有 `QStyleHints::colorScheme()` 与 `colorSchemeChanged`，Qt 5.15 没有 → 必须按双版本分支，Qt 5 侧显式降级（忽略该键或把设置项置灰）。涉及 `src/theme/Theme.cpp`（订阅系统配色并触发 `changed()`）、`SettingsAppearancePage.qml`（开关）、以及「用户显式选过主题后是否还跟随」的语义定义（建议：该键为真时忽略 `appearance.theme`）。
- `web.homeUrl`（**接上或删键**）：`WebTabsPage.qml` 已有 Home 动作（回到空态）。若接上，就是让 Home 打开这个 URL（空值保持现在的空态行为），并在 Web 设置页加输入框；若判定没用，就删键并同步删掉 `configuration.md` 里的示例行。
- `launcher.startupVersionCheck`（**门控或删键**）：`AgentsFacade::start()` 目前无条件调用 `checkVersions()`。接上就是门控（关掉则不查版本、卡片不显示版本号与安装状态——要确认这个副作用可接受），否则删键。

**验收**：`docs/` 里「read and written; no consumer today」这类标注消失或改成「已实现」；设置页新控件与 `configuration.md` 键表一致；若删键，验证一次「未知键记警告」的行为符合预期（`core::Settings` 的未知键处理）。

**需要你拍板**：三个键各选「实现 / 删键 / 保留现状并明确标注」中的哪一个。

## T3 `window.sidebarWidth` 只能手改文件（P2）

**证据**：`src/core/Settings.h` 与 `src/shell/ShellController.h` 都只有 getter，没有 setter；`Sidebar.qml` 读 `shell.sidebarWidth`（`> 0` 时用它，否则回退 `theme.sidebarWidth`），界面没有任何地方写回。

**方案**：给 `ShellController` 加 `Q_INVOKABLE void setSidebarWidth(int)`（带钳制，例如 180–480，写入后 `Settings::save()`），并让侧栏宽度可调。两种做法二选一：

- 侧栏右缘加拖拽手柄（`SplitView` 或 `DragHandler` + 光标变化）——手感好，改动集中在 `Sidebar.qml`/`MainWindow.qml`；
- 设置页「外观」加数值输入——改动小，但不好用。

**坑**：拖拽如果把 `width` 直接绑给布局子项会被重排覆盖，侧栏宽度必须继续经 `implicitWidth` 提供（`Sidebar.qml` 顶部注释已说明这个陷阱）；QML 调用的方法必须 `Q_INVOKABLE`（`check_architecture` 规则 5）。

**验收**：拖（或输入）之后重启仍保持；`tst_shell` 补一条持久化断言；中英 `guide/index.md`「窗口大小和侧栏状态会被记住」这类表述要能覆盖新行为。

**需要你拍板**：拖拽还是设置页输入？

## T4 `docs/research/webengine-embedding.md` 中英不一致（P3）

**现象**：英文 10 个二级标题、中文 9 个（缺一节）；两份都用本机绝对路径（`C:\…`）作为实验证据。

**方案**：补齐中文缺的那节（对着英文逐节核对）；把绝对路径换成不含用户名的写法（如 `C:\Users\<你>\…`）或换成可复现的描述（「本机 Qt Src 树的 `chrome/VERSION`」）。改完重建站点确认零 warning。

**注意**：`research/` 是「一次性结论、冻结不改写」的板块，本次只修**中英不一致**与**个人路径**，不改结论本身。

## T5 `docs/superpowers/plans/` 的构建噪声（P3，可选，一行）

**现象**：每次 `mkdocs build` 都会列一遍 `superpowers/plans/*.md`「exist but not in nav」。

**方案**：在 `mkdocs.yml` 的 `not_in_nav` 里加一条 `/superpowers/`，明确声明「计划文档刻意不入站」。比把计划搬出 `docs/` 更省事。

## T6 给中英同步加一道自动门禁（P2，本次改名之后尤其需要）

**背景**：中文文件名改成中文后，**两边不能再靠同名比对**判断是否齐平，`docs/AGENTS.md` 第 1 节的对应关系表只能靠人肉维护——这正是文档最容易漂的地方（本次就有 25 组）。而且顶层 `AGENTS.md` 已经把「过一遍文档对照表」写进「做完」的定义，却没有可执行的检查。

**方案**：新增 `scripts/check-docs.sh`（纯 shell + python3，不引 pip 依赖），做四件事：

1. 解析 `docs/AGENTS.md` 第 1 节的对应关系表，逐个检查英文页与中文页都存在（表就是唯一清单，不维护第二份）；
2. 每对中英页的 `##` 标题数与 ```mermaid 数量一致；
3. 所有站内 `.md` 链接可解析（英文侧用英文名、中文侧用中文名）；
4. 禁行号（`` `某文件.cpp:123` ``）、禁本机绝对路径、`guide/` 内禁代码标识符。

挂进 ctest 作为 `check_docs`（与 `check_architecture` 并列），`build.sh --test` 自然带上；`AGENTS.md` 的「做完」定义里补上它。

**验收**：故意删一个中文页、故意写一个坏链接、故意加一行行号，脚本分别报错并返回非零；正常状态下 `check_docs` 绿。

## 建议顺序

T1 → T3 → T6 → T4 → T5 → T2

先做用户可见且改动小的（T1）、再做机械但闭环的（T3、T6），文档收尾（T4、T5）之后处理需要先定取舍的 T2。

## 建议的提交切分

每项一个提交（`fix(ui): …` / `fix(shell): …` / `docs: …` / `test: …`），其中 T6 单独一个 `test(ci): add check_docs`。不要把这些攒成一个大提交——仓库的并行工作树模式下，小提交的合并成本低得多。

## 三个需要你拍板的点（未定，先不动手）

1. T1：状态栏的标签数——删掉，还是修好？
2. T2：`appearance.followSystem`、`web.homeUrl`、`launcher.startupVersionCheck` 各自选「实现 / 删键 / 保留并标注」？
3. T3：侧栏宽度——拖拽，还是设置页数值输入？
