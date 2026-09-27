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
│ 导航      │   主体内容                    │
│ （滚动）  │                              │
│──────────│                              │
│ 钉底区    │                              │
│ （system）│                              │
│──────────│                              │
│ 折叠手柄  │                              │
└──────────┴──────────────────────────────┘
│ 状态栏（横跨全宽）                        │
```

### 侧栏解剖（自上而下，固定结构）

1. **header**：应用图标 + 名称；
2. **可滚动的工作流导航**：`main` 区（内置高频页）+ `extensions` 区（插件页，之间画分割线）；
3. 一条**分割线**；
4. **钉底系统区**：`system` 区的页面（如 Settings）**永远钉在侧栏最底部**，与顶部导航隔离，不随列表滚动——列表再长它也纹丝不动；
5. 一条分割线；
6. **折叠手柄**。

### 侧栏规则

- **位置由 `section` 决定，不由代码决定**：注册页面时 `section = "main" | "extensions" | "system"` 就选好了落点。`system` = 钉底。想新增钉底页（About、日志查看器之类）→ 注册为 `system`，不要另写 QML 布局。
- 只有**系统级、低频、全局**的页面才进 `system`；日常工作流页进 `main`/`extensions`。
- 侧栏只回答「去哪里」，**永远不放业务动作**（不启动 agent、不删除东西）。
- Ctrl+1…9 按 `NavigationModel` 的模型顺序切换，钉底页排在最后，属预期行为。

### 右侧主区规则

- 主区顶部**可以**有一个工具栏式的区域（`PageHeader`：标题 + 副标题 + 页面级动作；或 Web 页那种标签栏），但**不是每个页面都必须有**——空状态页、简单列表页可以没有。取舍标准：该页是否有页面级的动作或过滤需求。
- 工具栏下方才是主体（卡片网格、表单、列表）。页面状态必须能毁掉重建（切换页面时 Loader 销毁旧页），跨页状态放 C++。
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
| `AButton` | 文字按钮 | 一切文字按钮（variant: primary/secondary/ghost/danger） |
| `AIconButton` | 图标按钮 | 一切图标按钮（tooltip 必填） |
| `ASearchField` | 搜索框 | 列表过滤输入 |
| `ACard` | 卡片容器 | 卡片外框（surface/圆角/边框/hover） |
| `APill` | 徽标胶囊 | 计数、来源标签 |
| `AEmptyState` | 空状态 | 「无数据/无匹配」整块占位 |
| `ADialog` | 模态对话框骨架 | 一切模态弹窗 |
| `ASectionHeader` | 设置分组标题 | 表单/设置页分节 |
| `AToolTip` | 主题化 tooltip | …（见 §5 待办：当前无人使用） |
| `AgentAvatar` | 图标 + 状态角标 | agent 的可视化入口 |
| `PageHeader`（非 A*） | 页面标题栏 | 各页顶部 |

硬规则：

- **不要手写裸 `Button` + 自定义 background**——必须用 `AButton`/`AIconButton`。新变体（颜色/尺寸）不够用时给 A 组件加属性，而不是旁路它。
- **不要手写弹窗骨架**（`Popup` + overlayBg 背景 + ColumnLayout + 标题/正文/按钮那套）——用 `ADialog`。需要确认弹窗就基于它做 `AConfirmDialog`（见 §5）。
- **不要重复实现状态点**——用 `AgentAvatar`（带状态）或提炼 `AStatusDot`（见 §5）。
- 新的通用件放 `components/`、名字以 `A` 开头、只用 theme 令牌；登记进下表。页面私有件（如 `AgentCard` 的控制台面板）留在各模块 qml/ 下，不进货架。

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

## 5. 已知重复与待提炼（2026-09 审计结论）

以下是审计发现、**尚未**提炼的共性点，做相关区域时顺手收敛，不要加剧：

1. **7 处手写模态弹窗** vs 零使用的 `ADialog`：`MainWindow`（退出确认、导入提示）、`SettingsPage`（删除确认、保存失败）、`AgentGridPage`（启动失败）、`AgentCard`（强制停止确认）、`AgentEditDialog`（保存失败）。→ 提炼 `AConfirmDialog`（危险确认）与 `AAlertDialog`（错误提示），全部迁到 `ADialog` 之上。
2. **裸 Button**：`AgentEditDialog`（Cancel/Save/Back）、`AgentCard`（Open/Start、Configure、强制停止弹窗内两个）、`WebTabsPage`（tab 菜单按钮）手写了与 `AButton`/`AIconButton` 重复的主题样式。→ 直接替换。
3. **4 处手写状态点**（尺寸 8/10/12 各不相同）：`SettingsPage` 启动器行、`AgentCard` 头部、`WebTabsPage` 标签、`AgentAvatar`。→ 提炼 `AStatusDot { running, color, tooltip }`。
4. **列表行卡片**重复 4 处（`SettingsPage` 启动器/技能根/插件行、`WebTabsPage` 运行中列表）：圆角矩形 + RowLayout(图标 + 名称/副标题列 + 尾部控件)。→ 提炼 `AListRow`。
5. **主题化 TextField 缺位**：`SettingsPage` 两处、`AgentEditDialog` 的 `FormTextField` 手写同样的背景。→ 提炼 `ATextField`（含 invalid 状态），并把 `FormLabel`（标签 + 必填星号 + 信息 tooltip）一并进货架。
6. **`kindLabel()` 重复**：`SettingsPage` 与 `SkillGridPage` 各自维护同一个 switch。→ 下沉到 `SkillsFacade`（C++ `tr()`，字符串只留一份）。
7. **附加式 `ToolTip.` 51 处全部未主题化**（走 Basic 样式默认外观），而 `AToolTip` 组件零使用。→ 统一附加样式或删掉死组件。
8. **过滤按钮组**重复：`AgentGridPage` 与 `SkillGridPage` 的「All + 若干 facet」AButton 行。→ 提炼 `AFilterBar`。
9. **Web 页空状态**未用 `AEmptyState`（手写标题/描述 + 列表）。→ 给 `AEmptyState` 加 `extra` 插槽后迁移。

## 6. UI 改动提交前检查清单

- [ ] 布局没破坏「侧栏 + 主区 + 状态栏」骨架；钉底区仍钉底。
- [ ] 没有新增字面颜色 / `Qt.rgba(<数字>)`；深浅主题都检查过。
- [ ] 通用件先查 §3 的表；没有旁路 `AButton`/`ADialog` 写裸件。
- [ ] 状态不只靠颜色表达；tooltip 文案是英文源串 + `qsTr()`。
- [ ] `bash scripts/build.sh --test` 全绿（含 `check_architecture`）。
- [ ] 若新增/移动了 `.qml`：同步 `app/CMakeLists.txt` 清单与 `AWB_TS_SOURCES`（AGENTS.md「QML 契约」）。
