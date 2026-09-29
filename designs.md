# designs.md — AgentWorkbench 界面设计原则

任何 agent（或人）在本仓库做 UI 改动**之前**必须先读这份文件。AGENTS.md 管 QML 技术契约（注册、Q_INVOKABLE、i18n），本文件管**布局骨架与视觉一致性**。两者冲突时以 AGENTS.md 为准并回来修订本文。

## 1. 布局骨架（不可协商）

整个应用是**左侧边栏 + 右侧主工作区**的外壳，外加一条底部状态栏：

```
┌──────────┬──────────────────────────────┐
│ 侧栏      │ 主区（页面内容）               │
│          │ ┌──────────────────────────┐ │
│ header   │ │ 页面顶部工具栏（可选）      │ │
│──────────│ └──────────────────────────┘ │
│ 工作流    │                              │
│ 导航      │                              │
│ （滚动）  │   主体内容                    │
│──────────│                              │
│ 钉底footer│                              │
│（system页│                              │
│ 图标+手柄）│                             │
└──────────┴──────────────────────────────┘
│ 状态栏（横跨全宽）                        │
```

### 侧栏解剖（自上而下，固定结构）

1. **header**：应用图标 + 名称；
2. **可滚动的工作流导航**：`main` 区（内置高频页）+ `extensions` 区（插件页，之间画分割线）；
3. 一条**分割线**；
4. **钉底 footer**：`system` 区的页面（如 Settings）**永远钉在侧栏最底部**，与顶部导航隔离、不随列表滚动；它们渲染为**纯图标按钮**（tooltip 显示标题，当前目的地经 `AIconButton.active` 填充 `surfaceBg`），与折叠手柄同排——展开时图标在左、手柄在右；收起时垂直堆叠（图标在上、手柄在下，水平居中）。

### 侧栏规则

- **位置由 `section` 决定，不由代码决定**：注册页面时 `section = "main" | "extensions" | "system"` 就选好了落点。`system` = 钉底。想新增钉底页（About、日志查看器之类）→ 注册为 `system`，不要另写 QML 布局。
- 只有**系统级、低频、全局**的页面才进 `system`；日常工作流页进 `main`/`extensions`。
- 侧栏只回答「去哪里」，**永远不放业务动作**（不启动 agent、不删除东西）。
- Ctrl+1…9 按 `NavigationModel` 的模型顺序切换，钉底页排在最后，属预期行为。

### 右侧主区规则

- 主区顶部**可以**有一个工具栏式的区域（`PageHeader`：标题 + 副标题 + 页面级动作；或 Web 页那种标签栏），但**不是每个页面都必须有**——空状态页、简单列表页可以没有。取舍标准：该页是否有页面级的动作或过滤需求。
- 工具栏下方才是主体（卡片网格、表单、列表）。页面状态必须能毁掉重建（切换页面时 Loader 销毁旧页），跨页状态放 C++。**唯一例外**：注册时声明 `PageDescriptor::keepAlive` 的页（目前只有 Web 页）由 Workspace 常驻托管——切换只隐藏不销毁，因为 WebEngineView 的页面状态搬不进 C++、销毁即整页重载；常驻页的 `ApplicationShortcut` 必须自行在非当前页时禁用（参考 WebTabsPage 的 `pageCurrent`）。常驻页**以 0x0 创建、变可见后才拿到真实尺寸**，必然经历一次布局重排——布局子项的尺寸必须经 `implicitWidth`/`implicitHeight` 提供，直接绑定 `width`/`height` 会被重排覆盖成 0（Web 页 tab bar 曾因此整条消失）。
- 页面外围留白统一 `theme.spacingL`，不要自造边距。

## 2. 视觉语言

- 页面与组件只用 `theme.*` 语义令牌；**绝不写字面颜色**（`check_architecture` 规则 2 在构建期强制，包括 `Qt.rgba(<数字>)`）。
- 深色（mocha-dark）与浅色（latte-light）两套主题下都必须可读——改完 UI 切另一套主题检查一遍。
- **状态永远不只是颜色**：状态点/徽标配 tooltip 或文字（色弱用户与深浅主题都需要）。
- 动效只用 `theme.durationFast / durationNormal`，不发明新的时长。

## 3. 组件复用规则（Reuse-first）

改 UI 时**先翻组件货架，再动手写**。`src/shell/qml/components/` 里的 `A*` 组件是唯一合法的通用件：

| 组件 | 用途 | 什么时候用 |
|---|---|---|
| `AButton` | 文字按钮 | 一切文字按钮（variant: primary/secondary/ghost/danger；primary 可经 `accentColor` 注入运行期颜色，如 agent 着色） |
| `AIconButton` | 图标按钮 | 一切图标按钮（tooltip 必填；导航场景用 `active` 填充当前目的地） |
| `ATextField` | 单行输入框 | 一切文本输入（含 `invalid` 红边与 2px 焦点环；高度 32 与 AButton 对齐） |
| `ATextArea` | 多行编辑器 | 一切多行文本输入（surface 背景/焦点环/invalid；与 ATextField 的 surface-alt 有意区分——亮色下长文编辑区需要更白的底） |
| `AFormLabel` | 表单标签行 | 表单字段标签（labelText + 必填星号 + 信息 tooltip） |
| `ASearchField` | 搜索框 | 列表过滤输入（带图标与清除键） |
| `AComboBox` | 下拉选择框 | 一切下拉选择（主题化背景/内容/弹层——Default 样式的调色板是硬编码浅色系，深色主题下不可读）；要定制省略方向或行内控件就在使用点覆写 contentItem/delegate |
| `AColorField` | 颜色输入行 | 一切「#RRGGBB 输入 + 色卡」的表单字段（文本框是真相源，点色卡弹 AColorPicker；AgentEditDialog 的 Color / Card color） |
| `AColorPicker` | 颜色选择器 | Office/WPS 式弹层：主题色 10 列×深浅阶、固定标准色、自定义色（原生对话框 + 最近 10 个记忆，进程内）；经 `openBelow(锚点)` 弹出 |
| `AColorSwatch` | 色块 | 选择器网格的一格 / 迷你色卡（空值 = 中性底 + 斜线；checked 环 + 勾标记当前色） |
| `ACard` | 卡片容器 | 卡片外框（surface/圆角/边框/hover） |
| `ASpotlight` | 指针聚光覆盖层 | 玻璃卡的悬停强调（光斑跟随指针 + 渐变描边流光）；宿主提供 `active`（**合并后的**悬停判据，见 hover 独占投递约定）与 `spotX/spotY`，组件自身不接收输入、不探测 hover |
| `AListRow` | 列表行 | 设置页/列表的单行（圆角矩形 + 注入式内容 RowLayout；默认高 48） |
| `APill` | 徽标胶囊 | 计数、来源标签 |
| `AEmptyState` | 空状态 | 「无数据/无匹配」整块占位（`extra` 插槽可放列表等附加内容） |
| `ADialog` | 模态弹窗骨架 | 一切模态弹窗（`danger` 变体：红标题 + 红边框） |
| `AConfirmDialog` | 确认弹窗 | 危险/常规确认（确认+取消双按钮，`detailData` 插槽放上下文警告） |
| `AAlertDialog` | 提示弹窗 | 错误/通知（消息 + 可滚动 mono 详情 + 关闭按钮） |
| `ASectionHeader` | 设置分组标题 | 表单/设置页分节（`extra` 尾部动作右对齐） |
| `AStatusDot` | 状态点 | on/off 指示（尺寸/颜色/tooltip 可配，永不只靠颜色） |
| `AToastStack` | 通知栈 | 右下角 toast |
| `AgentAvatar` | 图标 + 状态角标 | agent 的可视化入口 |
| `PageHeader`（非 A*） | 页面标题栏 | 各页顶部 |

硬规则：

- **不要手写裸 `Button` + 自定义 background**——必须用 `AButton`/`AIconButton`。primary 需要着色就设 `accentColor`，新变体（尺寸等）不够用时给 A 组件加属性，而不是旁路它。页面私有的微型交互件（如 AgentCard 卡内的 16px 下载/更新/关闭角标）除外。
- **不要手写弹窗骨架**（`Popup` + overlayBg 背景 + ColumnLayout + 标题/正文/按钮那套）——确认走 `AConfirmDialog`，错误/通知走 `AAlertDialog`，特殊形态（退出确认的三按钮）直接基于 `ADialog`。
- **不要重复实现状态点**——用 `AStatusDot`。
- **不要手写主题化 TextField**——用 `ATextField`（焦点/无效态已内建）。
- **列表行**用 `AListRow`，注入图标/文本列/尾部控件。
- tooltip 一律用**附加式** `ToolTip.x`，遵守全局惯例：`delay: 300`、`timeout: 10000`，delegate 类宿主加 `Component.onDestruction: ToolTip.hide()`（防宿主销毁后 tooltip 冻结在屏上；dev 分支 2026-09 已全量整改）。
- 新的通用件放 `components/`、名字以 `A` 开头、只用 theme 令牌、**同时登记** `app/CMakeLists.txt` 的 `_component_qml` 与 `cmake/AwbTranslations.cmake` 的 `AWB_TS_SOURCES`（有 `qsTr()` 时）。页面私有件（如 `AgentCard` 的控制台面板）留在各模块 qml/ 下，不进货架。

## 4. 页面模板速查

新增一个典型页面 = `ColumnLayout` + `PageHeader`（可选工具栏行）+ 主体：

```qml
ColumnLayout {
    anchors.fill: parent
    PageHeader { title: qsTr("..."); subtitle: qsTr("...") }
    // 可选工具栏：ASearchField + 过滤按钮行，左右 margin spacingL
    ScrollView { /* 卡片网格 Flow 或列表 */ }
}
```

空状态用 `AEmptyState`，加载中用骨架屏（参考 `SkillGridPage.qml`）。

## 5. 已知重复与待提炼（2026-09 审计结论及处置）

2026-09 审计发现的共性重复，本轮已按下列方式收敛；遗留项做相关区域时顺手处理，不要加剧：

| # | 审计发现 | 状态 |
|---|---|---|
| 1 | 7 处手写模态弹窗 | **已收敛**：`ADialog` 加 danger 变体，新增 `AConfirmDialog`/`AAlertDialog`，7 处全部迁移 |
| 2 | 裸 Button 手写主题样式 ×9 | **已收敛**：AgentCard/AgentEditDialog/WebTabsPage 全部换 `AButton`/`AIconButton`（AButton 新增 `accentColor`） |
| 3 | 4 处手写状态点（尺寸不一） | **已收敛**：统一 `AStatusDot` |
| 4 | 列表行卡片同构 ×4 | **已收敛**：统一 `AListRow` |
| 5 | 主题化 TextField 缺位、FormLabel 内联 | **已收敛**：`ATextField`/`AFormLabel` 入货架 |
| 6 | `kindLabel()` 双份维护 | **已收敛**：下沉 `SkillsFacade::kindLabel()`（含测试） |
| 7 | 附加式 tooltip 未主题化、`AToolTip` 死代码 | **已决策**：dev 分支删除 `AToolTip`，全仓改用附加式 ToolTip 的统一惯例（delay 300 / timeout 10000 / delegate 销毁时 hide，见 §3） |
| 8 | 过滤按钮组 ×2 | **不提炼**：两处语义不同（单选 displayFilter vs 多选 facets），强行共享是坏抽象；约定为「ASearchField + ghost/primary 切换 AButton 行」的模式，见各页 |
| 9 | Web 页空状态手写 | **已收敛**：`AEmptyState` 增加 `extra` 插槽后迁移（列表带高度上限的滚动） |

遗留的已知小项（不紧急）：

- `ACard` 仍零使用（AgentCard/SkillCard 自带状态化边框着色，暂无恰切落点；出现第三个卡片形态时再评估）。
- AgentCard 卡内 16px 微型交互件（下载/更新/输出关闭角标）仍是页面私有 Item+MouseArea——有意保留，见 §3 硬规则的例外条款。

## 6. UI 改动提交前检查清单

- [ ] 布局没破坏「侧栏 + 主区 + 状态栏」骨架；钉底区仍钉底。
- [ ] 没有新增字面颜色 / `Qt.rgba(<数字>)`；深浅主题都检查过。
- [ ] 通用件先查 §3 的表；没有旁路 `AButton`/`ADialog` 写裸件。
- [ ] 状态不只靠颜色表达；tooltip 文案是英文源串 + `qsTr()`。
- [ ] 若页面声明了 `keepAlive`：非当前页时该页的 `ApplicationShortcut` 已全部禁用；布局内子项尺寸经 `implicitWidth`/`implicitHeight` 提供（0x0 创建后的重排会踩掉直接 `width`/`height` 绑定）。
- [ ] `bash scripts/build.sh --test` 全绿（含 `check_architecture`）。
- [ ] 若新增/移动了 `.qml`：同步 `app/CMakeLists.txt` 清单与 `AWB_TS_SOURCES`（AGENTS.md「QML 契约」）。
