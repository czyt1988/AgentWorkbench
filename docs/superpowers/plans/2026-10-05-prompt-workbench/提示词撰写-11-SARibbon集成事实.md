# SARibbon 集成事实与约束

> 索引见 `提示词撰写-00-总览与索引.md`。本文是**第三方库 SARibbon 对当前项目的集成事实**，
> 全部来自对 `C:\src\Qt\SARibbon\` 源码的阅读（`src/qml/`、`src/core/`、顶层 `CMakeLists.txt`、
> `examples/qml/QmlMainWindowExample/`、`docs/superpowers/specs/2026-04-26-singlerow-layout-design.md`）。
> **尚未在本项目里真编译过一次**——标记「需实测」的条目，动手第一步要验证。
>
> **引入方式已定（决策 D-5）**：git submodule，`third_party/SARibbon`，
> 远端 https://gitee.com/czyt1988/SARibbon 。§五按 submodule 重写。

## 一、它是什么形态

| 维度 | 事实 |
|---|---|
| 版本 / 命名空间 | `SARibbon 3.0`（QML 模块 URI `SARibbon`，`qmlRegisterModule("SARibbon", 3, 0)`），C++ 命名空间 `SARibbonQml` |
| 模块划分 | `SARibbonCore`（`src/core`，只链 Core + Gui）、`SARibbonWidgets`（`src/widgets`，QWidget 系）、**`SARibbonQml`（`src/qml`）** |
| QML 模块的链接 | `SARibbonQml` 链 `Qt::Core Gui Quick Qml QuickControls2` + `SARibbonCore`。**不依赖 SARibbonWidgets**（所以嵌入本应用不会把 QWidget 拖进来） |
| 注册方式 | **命令式**，没有 qmldir 插件：应用在 `engine.load()` **之前**调 `saRibbonRegisterQmlTypes(&engine)`（`SARibbonQmlGlobal.h` 声明，已带 export 宏） |
| 构建开关 | 顶层 option `SARIBBON_BUILD_QML`（**默认 OFF**），需显式打开；嵌入场景建议同时 `SARIBBON_BUILD_WIDGETS=OFF` 与 `SARIBBON_BUILD_EXAMPLES=OFF`、`SARIBBON_INSTALL=OFF`（顶层只在自身为根工程时才进 examples/tests，但 install/export 规则要显式关） |
| 库类型 | `SARIBBON_BUILD_STATIC_LIBS` 决定 STATIC/SHARED（默认 SHARED）；静态时 `saRibbonRegisterQmlTypes` 内部走 `Q_INIT_RESOURCE` |
| 最低 Qt | `SARIBBON_MIN_QT_VERSION 5.12`（本项目 Qt 6 主线 + 5.15.16 兜底都在范围内） |
| C++ 标准 | C++17（与本项目一致） |

## 二、QML 面（可直接用到的类型与属性）

### 2.1 结构宿主

| 类型 | 关键属性 / API |
|---|---|
| `RibbonBar` | `ribbonStyle`（`Ribbon.RibbonStyle*`）、`currentIndex`、`applicationLabel`、`tabAlignment`、`minimumMode`（收起态）、`tabBarHeight` / `titleBarHeight` / `categoryRowY`、`contextBands`、`applicationMenuItems`、`applicationWindowItem`；`Q_INVOKABLE insertCategory/removeCategory/moveCategory/showCategory/hideCategory`；信号 `currentIndexChanged`、`ribbonStyleChanged`、`applicationButtonClicked`、`applicationMenuTriggered`、`layoutChanged` |
| `RibbonCategory` | `title`（未配 `RibbonTab` 时自动生成同名 tab）、`registerPanel` 等；动态可加（示例里用 `Repeater` + `ListModel` 演示增删） |
| `RibbonPanel` | `panelTitle`、`layoutMode`、`enableShowPanelTitle`、`hasOptionAction`（面板右下角斜角按钮）、`smallIconSize` / `largeIconSize`（`size`）、`titleGeometry` / `optionButtonRect`；`triggerOptionAction()` |
| `RibbonTab` | 显式声明 tab（与 `RibbonCategory` 配对）；不声明则由 bar 自动建 |
| `RibbonContextCategory` | `contextTitle`、`contextColor`、`active`；包若干 `RibbonCategory`（“上下文页”，激活时才出现） |

### 2.2 按钮与菜单

| 类型 | 关键属性 |
|---|---|
| `RibbonToolButton` | `text`、`iconSource`、`proportion`（`Ribbon.Large/Medium/Small`）、`checkable` / `checked`、`popupMode`（`Ribbon.DelayedPopup/MenuButtonPopup/InstantPopup`）、`menuItems`（`QQmlListProperty<RibbonMenuItem>`）、`toolTip`、`iconRightText`、`wordWrap`、`flat`、`smallIconSize`/`largeIconSize`；`click()`、`openMenu()`/`closeMenu()`；信号 `clicked`、`toggled`、`menuTriggered(item)` |
| `RibbonMenuItem` | `text`、`iconSource`、`checkable`/`checked`、`separator`、`shortcut`、`submenu` |
| `RibbonQuickAccessBar` | 标题行上的小按钮条（示例里放 Save/Undo/Redo），`exclusive` 可做单选组 |
| `RibbonButtonGroup` | 右侧按钮组（示例放 Help / Visible 切换） |
| `RibbonSeparator` | 面板内分组分隔 |
| `RibbonControlContainer` | `text`（标签条）、`suffixText`（尾随单位）、`iconSource`、`proportion`、`enableShowIcon` / `enableShowTitle`、**`control`（单个 QQuickItem）**——把自定义控件塞进 ribbon 的唯一通道 |
| `RibbonGallery` / `RibbonGalleryGroup` / `RibbonGalleryItem` | `groups`（`QQmlListProperty`）、`currentGroupIndex`、`displayRow`、`gridMinimumWidth`、`captionStyle`（`Ribbon.GalleryIconOnly/WithText/WithWordWrapText`）、`stretchFactor`、`currentItemIndex`、`scrollRow`、`totalRows`；`scrollUp/scrollDown/activateItem(index)`；信号 `triggered(item)`、`currentItemChanged`、`scrollRowChanged`、`gridMetricsChanged` |
| `RibbonColorToolButton` / `RibbonColorMenu` / `RibbonColorGrid` | 颜色按钮族（`colorStyle` = `Ribbon.ColorUnderIcon` / `Ribbon.ColorFillToIcon`、`color`、`onCustomColorRequested`） |
| `RibbonCustomizeDialog` / `RibbonActionRegistry` / `RibbonCustomizer` | 运行时自定义（命令目录 + ribbon 树预览，改动 staged 到 OK 才应用）——按 URL 实例化的对话框叶子 |

### 2.3 单例

| 单例 | 属性 / 方法 |
|---|---|
| `RibbonTheme` | `currentTheme`（int，取值见 `Ribbon.RibbonTheme*`：Windows7 / Office2013 / Office2016Blue / 2016Green / 2016Dark / Office2021Blue / Green / Dark / Dark / Dark2 / UserDefine）、`dark`、`accent`、`textColor`、`contentBg`、`contentHoverBg`、`contentPressedBg`、`subtitle`、`separator`、`borderColor`、`tabAccent`、`tabAccentHover`、`accentHover`、`accentPressed`、`menuBorder`、`inputBorder`、`inputFocus`、`selectionBg`、`sysButtonHover` / `sysButtonPressed`、`rtl`、`customPaletteSource`、`hasCustomPalette`、`systemDarkMode`、`followSystemDarkMode`；`Q_INVOKABLE tokenColor(name)`、`setAccentColor()`、`setContentBgColor()`、`setTextColor()`、`loadPaletteFromJson()`、`loadPaletteFromFile()`；信号 `paletteChanged`、`currentThemeChanged`、`rtlChanged` |
| `RibbonMetrics` | `fontPointSize`、`fontFamily`、`commonFontFamilies()`（方法）等度量；改它会重建度量并重排所有宿主 |

内置调色板以 JSON 资源随模块走（`qrc:/SARibbon/palettes/*.json`：office2016/2021、dark、dark2、win7、office2013）。

## 三、六种 ribbonStyle 与「单行模式」

`Ribbon.RibbonStyle*`：

| 常量 | 行数 | tab 位置 | 面板标题 | 按钮文字 |
|---|---|---|---|---|
| `RibbonStyleLooseThreeRow` | 3 | 标题行下 | 显示 | 图标下 |
| `RibbonStyleCompactThreeRow` | 3 | 标题行上 | 显示 | 图标下 |
| `RibbonStyleLooseTwoRow` | 2 | 标题行下 | 隐藏 | 图标下 |
| `RibbonStyleCompactTwoRow` | 2 | 标题行上 | 隐藏 | 图标下 |
| **`RibbonStyleLooseSingleRow`** | **1** | 标题行下 | **隐藏** | **图标右侧** |
| **`RibbonStyleCompactSingleRow`** | **1** | 标题行上 | **隐藏** | **图标右侧** |

`setRibbonStyle()` 是一次**完整布局重置**，QML 侧只暴露 `ribbonStyle` 一个写入口，其副作用（从 `SARibbonQmlBar.cpp::propagateRibbonStyle()`）：

- 行数 → `applyRibbonStyle(rows, showPanelTitle = rows != 1, wordWrap = rows == 3, iconRight = rows == 1)`；
- 单行模式下 `wordWrap` 关、**面板标题关**、`iconRightText` 开。

对本窗口的意义与新发现的两条约束：

1. **单行模式下「面板分组」的视觉分组会消失**（标题不显示）。若仍要看出分组，只有两条路：
   - 在每个 `RibbonPanel` 上显式写 `enableShowPanelTitle: true`（`RibbonPanel` 有这个属性），
     但**要实测**它在 `ribbonStyle` 变更引发的传播之后是否还保持（传播会覆盖面板标题开关，
     声明顺序 / 后设都可能被冲掉）；
   - 或者接受无标题，用 `RibbonSeparator` 与按钮顺序表达分组（更省高度，也更接近 Outlook 2025 的观感）。
2. **单行模式下 `proportion` 失去意义**：`Large`/`Medium`/`Small` 全部按 `Small`（图标 20×20 + 右侧文字）渲染。
   也就是说 ribbon 里的按钮**不能靠大小表达主次**——主次得靠位置、顺序与分组。

## 四、必须遵守的集成约束

1. **注册时机**：`saRibbonRegisterQmlTypes(&engine)` 必须在 `engine.load()` 之前调用
   （`app/main.cpp` 构造 `QQmlApplicationEngine` 之后、`load()` 之前那一段）。函数内部有
   `static bool once` 守卫，重复调用无害；但要保证**在真正使用它的那个进程里至少被调用一次**。
2. **不要在 `RibbonBar` 内部急切实例化 `Popup`**：上游明确记录过——在 bar 的 componentComplete
   期间创建的 `Popup` 会变成独立原生窗口，把主窗场景搞坏（表现为主窗空白 + 冒出一个游离小窗）。
   上游自己的做法是 `Loader { active: false }` 惰性创建。**我们的窗口 QML 不要往 `RibbonPanel` 里直接放
   `Popup` / `Dialog` / `Menu`**，需要弹窗时挂 `Loader` 惰性创建。
3. **不放进 `Loader` 惰性加载**：`RibbonBar` 的叶子在 `Component.onCompleted` 里创建，
   叶子创建本身走 `qmlEngine(host)` + 根上下文（上游注释里有一整族「叶子上下文早死 → 堆校验崩溃」
   的历史问题）。所以 ribbon 应作为**窗口 QML 里直接声明的常驻项**，不要包在 `Loader.active=false`
   或 `Repeater` 里按需创建。
4. **模块依赖方向不因此变化**：`awb_tools`、`awb_markdown`（C++ 库、只依赖 core + theme）
   **不链 SARibbon**。Ribbon 的集成点在 `app/`（注册 + 链接）与 QML（`import SARibbon`）。
   统一窗口 QML 在 `src/workbench/qml/`（L3，见 `提示词撰写-12` §八），**可以** import SARibbon
   而不新增领域 C++ 依赖；窗口背后的 C++ 依旧是 workbench 域的对象，不 include SARibbon 头。
5. **主题是两套**：ribbon 用 `RibbonTheme` 令牌，全仓其他 UI 用 `theme.*`。`check_architecture`
   的「QML 不许字面颜色」规则扫的是本仓源码，**管不到 `qrc:/SARibbon/` 里的叶子**——
   但本仓自己的 QML 里给 ribbon 写的 `RibbonTheme.setAccentColor(theme.accent)` 这类桥接表达式是合法的
   （规则允许 `Qt.rgba(theme.…)` 这类表达式，禁的是字面量）。
6. **菜单体系是两套**：`RibbonToolButton.menuItems` 只接受 `RibbonMenuItem`，本仓的 `AMenu` /
   `AMenuItem` 不能塞进去。二者可以共存（ribbon 用 `RibbonMenuItem`，窗口内右键菜单继续用 `AMenu`），
   但**同一个动作不要在两处各写一份**，否则文案与启用态必然漂移。
7. **图标来源**：`iconSource` 吃 URL（本仓 `qrc:/icons/*.svg` 可以直接喂）。ribbon 的小图标默认
   20×20、单行模式下全部用 20×20；本仓现成 SVG 是 24×24 画布，**要实测栅格化后是否发糊**
   （`sourceSize` 由宿主控制，我们无法在 QML 侧覆盖）。
8. **SVG 依赖是新增的**：应用此前不依赖 `Qt::Svg`；`SARibbonQml` 的 CMake 里 `find_package(... Svg)`，
   发布包需要 windeployqt 带上 `Qt6Svg.dll` 与 SVG 图像格式插件。
9. **本地化不在本仓的 `.ts` 里**：ribbon 自带字符串用的是它自己的 `qsTr()` 上下文
   （如 `qrc:/SARibbon/RibbonCustomizeDialog.qml`），lupdate 扫的是本仓的 `AWB_TS_SOURCES`，
   **不会**收进去。后果：`RibbonCustomizeDialog` 一类 ribbon 自带界面在中文环境下是英文。
   对策见 `提示词撰写-09` 决策记录 D-9（第一版不使用 `RibbonCustomizeDialog`）。
10. **样式基线**：上游示例统一把 QuickControls2 样式设成 `Basic`（Qt 6）/`Default`（Qt 5）
    （`QQuickStyle::setStyle`）并设一个基准字体（示例用 Microsoft YaHei 9pt），因为 ribbon 度量是
    **字体派生**的。本应用有自己的 `A*` 控件视觉，**不要为了 ribbon 去改全局样式**；
    但要注意 `RibbonMetrics` 的字体派生会不会让行高与本应用其它区域不协调（需实测）。
11. **高度管理**：`RibbonBar` 是 `QQuickItem`，会把隐式高度发布出来
    （`minimumMode` 时只有 tab 行高度，展开时是 tab 行 + 分类内容高度）。窗口里应让它
    `Layout.fillWidth` + 用隐式高度，`minimumMode` 就是天然的「折叠 ribbon 省垂直空间」开关。

## 五、本仓集成方案（已定：git submodule）

决策 D-5。**submodule 而非同级目录或 find_package** 的理由：版本随仓库走、可复现、
可锁定 SHA；与 `third_party/spdlog` 同惯例（`.gitmodules` 登记、目录命名一致）。

### 5.1 submodule 注册与离线初始化

```gitmodules
[submodule "third_party/SARibbon"]
	path = third_party/SARibbon
	url = https://gitee.com/czyt1988/SARibbon
```

- **内网不可达问题与 spdlog 同源**（都挂在 czyt1988 的 gitee）：新工作树里裸跑
  `git submodule update --init` 必然失败——**不要在 worktree 里手动跑它**。
  `worktree-add.sh` 创建时用 `git -c` 把 submodule URL 临时指向主仓库已有的检出、
  从本地完成克隆（不污染 git config；每个工作树的 submodule gitdir 独立），对 SARibbon 同样适用。
- **前提**：主仓库的 `third_party/SARibbon` 需先在有网环境初始化一次
  （本机 `C:/src/Qt/SARibbon` 检出可直接作克隆源）；base 移到更新过 submodule 的提交时，
  先在主工作树 `git submodule update`，否则新工作树离线拿不到新 SHA。
- 注册提交：`.gitmodules` 一行 + submodule 指针 + CMake 接线 + `CHANGELOG.md`/第三方许可说明，
  与窗口迁移彻底分开（见 `提示词撰写-10` §三）。

### 5.2 CMake 接线

```cmake
# 新增 CMake 选项 AWB_ENABLE_RIBBON（默认 ON；submodule 恒在树里，开关只裁构建）
# submodule 缺失时的行为：配置期报「请先在有网环境初始化 submodule」并失败，
# 不静默把 ribbon 裁掉（静默降级会让人以为功能还在）。
if(AWB_ENABLE_RIBBON)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SARibbon/CMakeLists.txt")
        message(FATAL_ERROR "SARibbon submodule is not initialized; see AGENTS.md on offline submodule init")
    endif()
    set(SARIBBON_BUILD_QML        ON)
    set(SARIBBON_BUILD_WIDGETS    OFF)
    set(SARIBBON_BUILD_EXAMPLES   OFF)
    set(SARIBBON_BUILD_TESTS      OFF)
    set(SARIBBON_INSTALL          OFF)
    set(SARIBBON_BUILD_STATIC_LIBS ON)   # 与 awb 目标同为静态时更省部署；需实测 Q_INIT_RESOURCE 路径
    add_subdirectory(third_party/SARibbon ${CMAKE_BINARY_DIR}/saribbon)
endif()
target_link_libraries(AgentWorkbench PRIVATE SARibbon::Qml)   # 仅当开关打开
```

```cpp
// app/main.cpp：与 qmlRegisterSingletonInstance 段同一位置，在 engine.load() 之前
#ifdef AWB_ENABLE_RIBBON
    saRibbonRegisterQmlTypes(&engine);
#endif
```

QML 侧唯一的桥接点是 `RibbonTheme` ↔ `theme`：本仓主题变化时把 accent / contentBg / textColor /
borderColor 推过去一次（`setAccentColor` / `setContentBgColor` / `setTextColor`，它们都是
`Q_INVOKABLE`），并让 ribbon 走它自己的 office2021 系调色板。
`followSystemDarkMode` 与本仓 `theme.followSystem` / `theme.variant` 的关系需实测后再定，
**不要两边各留一套自动跟随逻辑**。

### 5.3 发布与打包

- 静态链接（`SARIBBON_BUILD_STATIC_LIBS=ON`）时随包无额外 DLL，但 qrc 的
  `Q_INIT_RESOURCE(saribbon_qml)` 路径要实测（§六.6）；
- 动态链接的退路：`scripts/package.sh` 的 windeployqt 不会自动带上非 Qt 的第三方 DLL，
  若最终选动态，需要显式复制（静态优先就是为了绕开这个）。

## 六、需实测清单（动手第一步）

1. submodule 指针就位后，`add_subdirectory` 能否用本项目 `scripts/build.sh` 的 Qt 前缀与生成器（Ninja）一次配通；
   Qt 5.15.16 兜底路线是否同样能配通（`SARIBBON_MIN_QT_VERSION` 是 5.12，理论上可以）。
2. `saRibbonRegisterQmlTypes` 在 Qt 6.5+/6.7 实机上的注册与 QML 解析是否正常（尤其与
   `qt_add_qml_module` 生成的 `AgentWorkbench` 模块共存时）。
3. 单行模式 + `enableShowPanelTitle: true` 的存活情况（传播是否覆盖）。
4. `iconSource` 喂本仓 24×24 SVG 在 20×20 小图标下的清晰度。
5. `RibbonBar` 的隐式高度与窗口 `ColumnLayout` 配合是否稳定（拖动窗口 / 切 tab / 折叠时）。
6. 静态链接（`SARIBBON_BUILD_STATIC_LIBS=ON`）下的 qrc（`Q_INIT_RESOURCE(saribbon_qml)`）是否生效。
7. ribbon 在本应用深浅两套主题下的可读性（尤其 `dark2` 调色板与 mocha-dark / latte-light 的搭配）。
8. **`RibbonContextCategory` 的实际形态**（`提示词撰写-08` §2.3 用它做文档模式的三 tab 隐藏）：
   `active: false` 时 tab 是否彻底消失、`contextTitle` 在单行模式下是否渲染、切换是否有闪烁。
   若实测不符，退路是「文档模式下对三个 `RibbonCategory` 调 `hideCategory()`」
   （它是 `Q_INVOKABLE`），但要在窗口状态里记住 tab 归属。

## 七、版本与许可证注意

- 本仓要记录 SARibbon 的引入版本（3.0.0）与来源地址（gitee submodule，
  `third_party/SARibbon`）；发布包若含其 DLL/静态库，
  `CHANGELOG.md` 与第三方许可说明要保持一致——与 `third_party/spdlog` 的先例同一形态
  （引入方式已从「同级目录」定为 submodule，决策 D-5）。

## 八、实施记录补注（2026-10-05，引入提交时追加）

规划落笔时的事实核对基于本地检出 `C:/src/Qt/SARibbon`；引入时该检出的**顶端已前移**，
带来一处必须知情的选择：

- **submodule 钉在 `f89a02e`**（"合并：并入远端 dev-3.0 分支改动"），不是顶端。顶端
  `c7f9bfd`（"build(cmake): QML 模式强制接入 QWindowKit 无边框方案"）在
  `SARIBBON_BUILD_QML=ON` 时**强制**启用 `SARIBBON_USE_FRAMELESS_LIB`，从而引入第二个
  第三方依赖 QWindowKit（其嵌套 submodule 远端在 GitHub，内网同样不可达）。本文 §一
  「只链 Core/Gui/Quick/Qml/QuickControls2 + SARibbonCore」的事实正是 `f89a02e` 的状态，
  QWindowKit 强制接入在 `c7f9bfd` 才发生。工作台窗口保留系统原生边框，不接 QWindowKit，
  因此按规划文档的事实基准钉住 `f89a02e`。后续若要升级 submodule，先重读顶端 QML 模块的
  链接面并重新决策。
- **§六.1 已验证**：`f89a02e` + `SARIBBON_BUILD_QML=ON` + `SARIBBON_BUILD_STATIC_LIBS=ON`
  经 `scripts/build.sh`（Qt 6.7.3 + MSVC + Ninja）一次配通、构建通过，链接进
  `AgentWorkbench.exe` 后 `bash scripts/build.sh --test` 14/14 全绿（含 `check_architecture`）。
  `AWB_ENABLE_RIBBON=OFF` 两态构建同批验证。
- **§四.8 的 Svg 说法来自顶端提交**：`f89a02e` 的 SARibbonCore 与 SARibbonQml 源码里
  **没有任何 Svg 引用**（顶层 `find_package` 要求 Svg 组件存在仅为配置期检查，widgets
  模块才真用它，而本仓 `SARIBBON_BUILD_WIDGETS=OFF`）。因此静态引入**没有**给发布包
  增加新的 Qt6Svg 运行时负担；应用自身 SVG 图标的既有部署路径（windeployqt 的 QML 扫描）
  不变，M0 的发布会话验证照做即可。
- §六的其余条目（单行模式行为、图标清晰度、静态 qrc 运行期生效、主题可读性、
  `RibbonContextCategory` 形态）依赖窗口里真的实例化 RibbonBar，全部顺延到
  M0 的「Ribbon 装配」步骤随窗口验证（`提示词撰写-10` 的 M0 风险行已如此要求）。
