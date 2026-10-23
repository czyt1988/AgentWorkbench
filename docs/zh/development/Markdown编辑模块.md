# Markdown编辑模块

`awb_markdownedit`（`src/markdownedit/`）是 Markdown 编辑引擎：命令条与右键菜单背后的格式化动作、语法高亮器，以及查找/替换原语。它与其它领域模块一样遵守分层规则，但多一条别的模块没有的性质：**它刻意做到自足，可以整体拷进另一个项目**，只带 `awb_theme` 就能消费。本页记录它的边界、公开能力面，以及宿主项目的集成步骤；工作台页面怎么驱动它见《[Markdown工作台](Markdown工作台.md)》，让它保持树叶位置的分层规则见《[分层与依赖](../architecture/分层与依赖.md)》。

本模块没有单独的用户指引页：用户可见的行为（按钮、快捷键、右键菜单）属于工作台页面，那个视角见《[工作台使用指引](../guide/Markdown工作台.md)》。

## 模块里有什么

| 文件 | 类 / 组件 | 职责 |
|---|---|---|
| `src/markdownedit/MarkdownEdit.{h,cpp}` | `MarkdownEdit` | QML 侧入口：`attach()` 给 `TextArea` 的文档挂高亮；`apply*`/`insert*` 家族执行格式化动作（行内标记类是**切换**语义：对已加粗的文字再执行加粗是去掉标记，不是叠成四层）；`insertMarkdown()` 是通用的插入/替换原语，用来放置图片引用这类整段 Markdown（一次编辑、一步撤销）；`handleTab`/`handleBacktab`/`handleEnter`/`autoType` 是输入动作（按调用方给的单位缩进、回车延续列表、输入时配对标记）；行与块操作（`moveLine*`、`duplicateLine`、`deleteLine`、`selectLine`、`selectWord`、`convertCase`、`promoteHeading`/`demoteHeading`）以同一形状住在旁边；`is*At`/`headingLevelAt` 查询喂给可勾选按钮；`find` / `replaceAndFindNext` / `replaceAll` / `findCount` / `findIndexOf` 是查找/替换原语。每个动作都是静态 `QTextCursor` 操作包一层 `Q_INVOKABLE`，各占一个编辑块 |
| `src/markdownedit/MarkdownHighlighter.{h,cpp}` | `MarkdownHighlighter`、`MarkdownColors` | 轻量逐块高亮器：围栏代码块（跨块状态机）、标题、分隔线、引用、列表标记，以及行内的代码/粗体/斜体/删除线/链接。刻意不做 CommonMark 全量正确。配色经 `MarkdownColors` 注入——高亮器不认识 `Theme`。`setSearch()` 另在语法规则之后给每个命中画一层背景色，查找因此不依赖选区也可见 |
| `src/markdownedit/qml/MarkdownContextMenu.qml` | `MarkdownContextMenu` | 编辑器右键菜单：格式化工具栏一行 + 撤销/重做/剪切/复制/粘贴条目 + 行操作（上移/下移/复制/删行/选行）与大小写转换条目。**粘贴**条目发 `pasteRequested()` 而不是直接调编辑器，宿主因此能先把图片剪贴板交给自己的导入器、再回落原生粘贴。它是宿主 UI 外壳而不是引擎逻辑——只转发给单例与编辑器自身的成员 |

引擎半边（`MarkdownEdit`、`MarkdownHighlighter`）不依赖 `awb_core`、不依赖任何其它领域、不依赖应用本身：链接清单是 `awb_theme` + Qt Core/Gui，Qt Quick 是 `PRIVATE` 链接且只为 `MarkdownEdit` 读 `QQuickTextDocument` 而存在。高亮器单独看是纯 Qt Gui——测试直接构造一组 `MarkdownColors`，不需要主题。

两条对复用要紧的设计性质：

- **动作是静态光标操作。** `applyBoldToCursor()` 及其同族只吃一个 `QTextCursor`，因此能对着裸 `QTextDocument` 测试、无需焦点照常工作（菜单抢走焦点后仍能操作编辑器的选区正是因此）、也不依赖 QQuick 类型。`Q_INVOKABLE` 实例方法只是薄适配：读 `TextArea` 的文档与选区、把结果写回去。
- **偏好是参数不是状态。** 大小写敏感与全词匹配是 `find` / `replaceAll` 的参数；缩进单位是 `handleTab`/`handleBacktab` 的 `IndentOptions` 参数（`useSpaces` + `spacesPerIndent`——本应用从 `settings.json` 的 `markdownEditor` 组喂进来）；输入动作 `smartEnterToCursor()` / `autoTypeToCursor()` 返回 `bool`（「这次按键我接了吗」），吞不吞按键由宿主决定，调不调用也由宿主决定。本模块不持久化任何东西、不读任何配置。宿主想把这类偏好记在哪是宿主的事（本应用把查找偏好存 `tools.json` 的 `editor` 段、把输入偏好存 `settings.json`）。

## 测试

`tst_markdownedit` 在 `QTextDocument` 层驱动引擎：每个动作的光标语义（空选区、单行选区、多行选区、行尾换行处的边界纪律——包括单行选区按 Tab 是给整行加缩进、而不是替换选区）、`insertMarkdown` 的按位置插入与选区替换（含多行块必须一步撤销）、行/块与大小写操作、标题升降级、命中计数与 1 起序号、查找高亮（含标题行上与语法色的合并）、高亮器的逐块状态机，以及含回卷行为的查找/替换原语。它只链接 `awb_markdownedit` 和 Qt——不需要主题、不需要 QML 引擎，所以 Qt 5 与关掉 WebEngine 的每个构建配置都跑。

## 在其它项目里使用本模块

本模块按源码引入消费——把 `src/markdownedit/` 与 `src/theme/`（它链接的主题引擎）拷过去或以 subtree/checkout 引入，然后接四件事：

1. **构建**：两个目录都 `add_subdirectory`（或把源码编进你自己的目标）。模块的 `CMakeLists.txt` 以 `${CMAKE_SOURCE_DIR}/src` 为 include 根，目录布局不同就调 `target_include_directories`。`awb_theme` 还链 `awb_core`——把 `src/core/` 一并带上；或者干脆不依赖主题：用你自己的 `MarkdownColors` 直接构造 `MarkdownHighlighter`、跳过 `MarkdownEdit` 的 `attach()` 配色管线。
2. **构造并注册**：`MarkdownEdit` 在宿主手里是普通 `QObject` 单例。用一个 `theme::Theme *` 构造它，注册到你自己 QML 用的任意 URI：

```cpp
awb::theme::Theme theme; // 你的主题实例
awb::markdownedit::MarkdownEdit markdownEdit(&theme);
qmlRegisterSingletonInstance("YourApp.App", 1, 0, "MarkdownEdit", &markdownEdit);
```

3. **挂编辑器**：每个要高亮的 `TextArea` 里调一次 `MarkdownEdit.attach(textArea)`（重复调用无害）。工具栏/菜单动作转发给 `apply*`/`insert*`；想要 VSCode 风格的行级缩进/反缩进，就把 `Keys.onTabPressed`/`Keys.onBacktabPressed` 接到 `handleTab`/`handleBacktab`（传入你的缩进偏好）——任何选区（包括只选中一行的一部分）都按行范围处理，只有光标才插入一个单位；可选地，把 `Keys.onReturnPressed` 接到 `handleEnter`、把单字符的 `Keys.onPressed` 接到 `autoType`，获得列表延续与标记配对——两者都返回「是否接管了这次按键」，只在 `true` 时接受事件。
4. **右键菜单**：`MarkdownContextMenu.qml` 依赖本应用的 shell 组件（`AMenu`、`AIconButton` 等）与它的 `theme.*` 令牌，不能原样带走。要么在同一个 `Q_INVOKABLE` 能力面上写你自己的菜单（它只需要编辑器引用），要么用你自己的菜单原语移植这个组件。

单例暴露的一切都是 `Q_INVOKABLE` 或信号，所以「QML 调用的每个方法都必须可调用」这类校验（`check_architecture` 规则）在宿主同样适用；这条规则在本应用里抓出过三处静默失效，见《分层与依赖》。

## 相关

- 《[Markdown工作台](Markdown工作台.md)》——页面怎么驱动本模块：命令条、查找输入框、两个编辑器。
- 《[分层与依赖](../architecture/分层与依赖.md)》——领域模块为什么不许互相依赖，`awb_markdownedit` 在依赖图里的位置。
- 《[主题引擎](主题引擎.md)》——Qt 之外唯一的依赖：高亮配色的来源。
