# AGENTS.md

面向在本仓库中工作的 AI 编码 agent 的指导说明。

## 项目简介

AgentWorkbench 是一个用 Qt6/QML + C++ 开发的 AI 编码 agent 工作台：左侧边栏 + 右侧工作区的外壳，内嵌 Web 标签页、Skill 浏览、配置文件驱动的主题与实验性插件。它**配置化驱动**：agent 的定义（命令、Web 地址、配置目录、颜色）都在 `agents.json` 里，应用设置在 `settings.json` 里，而不是写在 C++ 中。

**界面设计原则固化在仓库根目录的 `designs.md`**（布局骨架、侧栏钉底规则、组件复用目录与已知重复清单）。**任何涉及 UI/QML 的任务，动手前先读 `designs.md`**——本文只管 QML 技术契约，布局与视觉一致性以它为准。

**代码风格与注释规范固化在 `docs/zh/standards/coding-standard.md`**（文件与命名、C++/Qt/QML 写法、Qt 最佳实践——`Q_OBJECT` 必写、一律大写 `Q_SIGNALS`/`Q_SLOTS`/`Q_EMIT` 宏、单语句 `if`/`for` 也带花括号、非 const Qt 容器经 `std::as_const()` 范围迭代、模块内部用异常不用 `std::optional`、线程不直接操作 GUI、高 DPI 下 `QPixmap` 尺寸除以 `devicePixelRatio()`——以及 Doxygen 注释规范：注释用中文，头文件成员函数写简短普通注释、`.cpp` 函数实现前写完整 `/** ... */` Doxygen 注释，信号/枚举/成员变量（`///<`）写在头文件）。**任何写代码的任务，动手前先读它并按它写**——新代码与你改动到的类、函数都必须符合该规范，不要凭习惯另起一套风格。英文版见 `docs/standards/coding-standard.md`。

## 构建

```bash
bash scripts/build.sh              # Debug 构建到 build/
bash scripts/build.sh --test       # 构建后运行单元测试（含 check_architecture）
bash scripts/build.sh --release    # Release 构建到 build-release/
bash scripts/build.sh --help       # 全部选项
```

日常编译请用 `scripts/build.sh`，不要直接手写 cmake 命令：它自动探测 Qt 与 MSVC、复用构建目录里已有的生成器与 Qt 前缀、在构建目录属于旧路径时清掉陈旧的 CMake 缓存，并生成 `compile_commands.json`。Git Bash 下无法用 `eval "$(cmd //c ... set)"` 把 vcvars64 环境导入当前 shell（cmd 收到的是转义后的引号，`cl.exe` 不会出现在 PATH 里），所以脚本改为生成一个 `.bat` 把 vcvars + cmake 包起来执行——这是在本仓库里从 Git Bash 驱动 MSVC 唯一可靠的做法，不要在其它写法上反复试错。构建目录、生成器与 Qt 前缀的解析顺序都写在脚本头部的注释里，需要手工排查时可直接读 `build/.build-agentworkbench.bat` 看实际执行的命令。

其它常用选项：`--target NAME` 只构建一个目标、`--no-tests` 配置时关掉测试目标（`-DBUILD_TESTING=OFF`，打包用）、`--run` 构建后启动应用、`--clean` 删除构建目录重建、`--print-exe` 打印可执行文件路径。

Qt 探测顺序：`QT_PREFIX`/`--qt` → 构建目录 CMake 缓存里的前缀 → 各盘常见根目录（含 Qt 在线安装器的 `<盘>/Qt/<组>/<版本>/<编译器>` 嵌套布局）→ `PATH` 上的 `qmake`/`qtpaths` → 逐盘限时扫描（`AWB_QT_DEEP_SEARCH=0` 跳过，`QT_DEEP_TIMEOUT` 设每盘秒数）。**一个可用的 Qt 6 都找不到时它不会静默停留**：会列出探测到的每个安装及不可用原因（版本低于 6.5、无 `lib/cmake/Qt6` 等），有终端时询问前缀，非交互运行（agent/CI）则以该报告直接失败退出。

需要手工执行时的等价命令（前提是自己已经准备好 MSVC 环境，例如在「x64 本机工具命令提示符」中运行）：

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

- 需要 Qt 6.5+，或 Qt 5.15.16 LTS 兜底（模块同名：Core、Gui、Qml、Quick、QuickControls2、Network、LinguistTools；内嵌 Web 另需 WebEngineQuick——Qt 5 里组件叫 WebEngine）。两个大版本的差异全部集中在 `cmake/AwbQtCompat.cmake` 与 `#if QT_VERSION` 分支：Qt 5 路线的验证基准是 **5.15.16 LTS**（内嵌 Web 依赖的 lifecycleState/对话框请求等 WebEngine backport 在 LTS 补丁版里，开源 5.15.0/2 未必齐全）。Qt 5 构建的 QML 源在配置期自动改写补上 import 版本号（Qt 5 编译器要求库 import 带版本），资源/qmldir 用生成的 qrc 镜像 Qt 6 的 `/qt/qml/AgentWorkbench/` URL。
- 需要 CMake 3.16+，C++17。
- 生成器：Ninja（推荐）或 MSBuild。脚本新建构建目录时优先用 Ninja；构建目录已配置过则沿用其生成器，因此 `--release` 不需要 MSVC 环境也能跑。
- 构建选项（`cmake/AwbOptions.cmake`）：`AWB_ENABLE_WEBENGINE`（默认 ON，MinGW + ON 在配置期报错）、`BUILD_TESTING`（默认 ON）。额外参数经 `bash scripts/build.sh -- -D…` 传入。`BUILD_TESTING` 由 build.sh 的两个互斥开关显式设置：`--test` 配成 ON、`--no-tests` 配成 OFF，所以同一个构建目录在两种用法之间来回切也能正常工作（`--` 里再传一次则以最后一次为准）。
- 发布打包用 `bash scripts/package.sh`：它调用 build.sh 完成 Release 构建（带 `--no-tests`，测试目标不进包，省掉这部分编译时间），然后 windeployqt + zip 出 `dist/AgentWorkbench-<version>-win64-Portable.zip`。要改 Qt 前缀只改一处——`package.sh` 通过 `build.sh --print-qt` 取同一个值。
- 测试目标：`tst_core`、`tst_agentcatalog`、`tst_theme`、`tst_shell`、`tst_web`、`tst_skillcatalog`、`tst_tools`、`tst_workbench` 与 `check_architecture`；`./build/tst_core testRoundTrip` 这样按名字跑单个用例（约定见下文「测试」）。

## 目录结构

```
app/           可执行文件：组装（main.cpp、app.rc）与资源清单；.qml 本体在各模块的 qml/ 下
src/
  core/          L0 基础设施：Paths、JsonStore、Settings、Logging、ProcessRunner、ScriptRunner、HttpProbe、PluginHost、LegacyImport、IconResolver、EnvExpander、TextUtils、OpResult
  plugin_api/    L0 插件 ABI（仅头文件；外部仓库链接它）
  theme/         L1 主题引擎：ThemeFile/ThemeLoader/ThemeRegistry/Theme
  agentcatalog/  L2：外部 agent 工具的目录（只编目与启动，不是 agent 实现）：AgentDefinition/AgentState/AgentStateStore、AgentRepository、AgentModel、AgentRuntime、AgentScripts、AgentHealthMonitor、AgentUrls、AgentsFacade + qml/
  shell/         L2 UI 框架：NavigationModel、ShellController、UiServices、Notifications、PageDescriptor + 窗口骨架 QML 与 qml/components/ 的 A* 组件（不认识 agent/skill/web）
  skillcatalog/  L2：本机 skill 的目录：SkillDefinition、SkillFrontmatter、SkillRoot/SkillRoots、SkillScanner、SkillModel、SkillsFacade + qml/
  tools/         L2：Agent Tools 页（提示词编写台）：ToolsStore（工作区记忆 + 草稿，tools.json）、FileTreeModel（懒加载文件树 + watcher + 增量刷新）、FileIcons（名字/后缀 → 图标）、ToolsFacade + qml/
  web/           L2：WebTab、WebTabsModel、WebSurfaceRegistry、WebProfilePaths、WebTabsFacade + qml/
    webengine/   L2 适配器（唯一链接 Qt WebEngine 的目标，含 WebEngineSurface.qml）
  workbench/     L3：WorkbenchContext、BuiltinPages、EnvironmentService、PluginServices
cmake/         AwbOptions.cmake、AwbTranslations.cmake（可翻译源清单）
resources/     内置主题 JSON（mocha-dark、latte-light）
config/        default_agents.json、default_file_icons.json（打包为 Qt 资源）
icons/         SVG 图标（打包为 Qt 资源）；filetypes/ 与 foldertypes/ 是文件树的类型图标
translations/  只有一份 agentworkbench_zh_CN.ts（编译为 .qm 后以 :/i18n/ 嵌入）
docs/          MkDocs 站点（英文 + zh/）与调研记录（research/）
tests/         每模块一个测试目标 + check_architecture（多类套件经 tests/awbtest.h 注册）
scripts/       build.sh、package.sh、check-architecture.sh、worktree-add.sh、generate_icon.py
```

## 配置结构

数据目录是 `~/.AgentWorkbench/`（`core::Paths::dataRoot()` 是唯一来源，`themes/`、`plugins/`、`log/`、`webprofiles/` 等子目录也一律经 `Paths` 取得，不要在别处拼路径）。首次启动用 `core::LegacyImport::runOnce` **复制**旧 `~/.AgentLauncher` 的数据——复制而非搬移，旧目录不删，导入过就提示一次。

单元测试在 `QStandardPaths::setTestModeEnabled(true)` 下运行——测试模式不重定向 `HomeLocation`，因此 `dataRoot()` 改用测试模式对应位置；需要固定目录的测试用 `Paths::setDataRootForTesting()` 注入 `QTemporaryDir`。**任何测试都不许读写开发者真实的数据目录。**

`settings.json` 的键位表就是 `core::Settings` 里的那组结构体（`WindowSettings` 等），读写只经 `core::Settings`，禁止在别处直接读这个文件；缺键取默认值、未知键记警告，**没有迁移代码，也不要加**。`agents.json` 根级 `title` 字段已停用（窗口标题来自 `window.title`，残留值会记一条 INFO）。根级可选的 `removed` 数组记录用户删除的内置 agent id；内置 agent 每次启动都按随包默认重新生成，靠这个列表保持删除状态。

每个 agent 对象包含：`id`、`name`、`command`、`webUrl`、`configDir`、`icon`、`color`、`cardColor`、`installCommand`、`updateCommand`、`versionCommand`、`setupCommand`、`tokenFile`。**内置** agent 的定义只来自 `config/default_agents.json`，改它并重新编译即可，不要在 C++ 中硬编码 agent 条目。与默认完全一致（无自建、无删除）时 `save()` 逐字节写入内置文件，保持可 diff。

`icon` 解析在 `core::IconResolver`（fallback 由调用方给出，core 不写死应用资源路径）；环境变量展开在 `core::EnvExpander`（`%VAR%` 与 `~`）。`color` 留空时从**当前主题**的 `agentPalette` 按位置循环分配（`AgentRepository::paletteColorAt` 是回退）。配了 `tokenFile` 时，最终打开的 URL 一律由 `agentcatalog::AgentUrls::finalUrl()` 生成（追加 `#token=` 片段，不落服务器日志）——内嵌视图与外部浏览器都走它，不要另拼。

文件树的图标也是数据不是代码：`config/default_file_icons.json` 的三张表（`fileNames`、`suffixes`、`folderNames`，键一律小写）把名字映射到图标 URL，`defaults` 给兜底；`tools::FileIcons` 负责查表（完整文件名 → 后缀 → 默认），值经 `IconResolver` 归一，因此用户能在 `<dataRoot>/file_icons.json` 里按键覆盖或追加（支持 `%VAR%`、`~` 与本机文件路径）。加一种图标 = 往 `icons/filetypes/`（或 `foldertypes/`）放一个 SVG + 在 JSON 里加一行 + 在 `app/CMakeLists.txt` 的资源清单里登记；**不要在 C++ 或 QML 里写后缀判断**。

## 约定

### 依赖方向与门禁

`app → workbench → {shell, agentcatalog, skillcatalog, web, tools, theme} → core`；领域模块之间零依赖，跨域行为写在 `awb_workbench`（或经 `WorkbenchContext` 的意图方法）。`scripts/check-architecture.sh` 挂成 ctest 的 `check_architecture`，共 5 条规则，违反即构建失败：

1. 反向/横向 include：`src/{agentcatalog,skillcatalog,web,tools}` 不许 include `shell/`、`workbench/` 或彼此的目录；
2. QML 字面颜色：不许 `#rrggbb`/`#rgb`，也不许 `Qt.rgba(<数字>, …)`（`"transparent"` 与 `Qt.rgba(theme.…)` 这类表达式允许）；
3. i18n：`tr()`/`qsTr()` 的源串必须是 ASCII；
4. core/theme 纯净：`src/core/`、`src/theme/` 不许出现 `QtQuick`、`QQuick*`、`QQml*`、`Qt6::Quick`、`QtWebEngine`；
5. QML → C++ 可调用性（见下）。

### QML 契约

- 页面只用 `theme.*` 语义令牌，绝不写字面颜色；深浅色都必须可读。
- C++ 全局注册在 `AgentWorkbench.App` 这个纯 C++ URI 上，且**类型名必须大写**（Qt ≥6 拒绝小写单例名）。不要往 `AgentWorkbench` URI 手工注册单例（那是 `qt_add_qml_module` 生成的有 qmldir 的模块，会报 protected module），也不要用 `setContextProperty`。
- QML 侧的契约名是 `MainWindow.qml` 根部的**小写别名**：`theme`、`nav`、`shell`、`ui`、`toasts`、`agents`、`web`、`skills`、`tools`、`workbench`、`environment`（`WebProfiles` 只在 `WebEngineSurface.qml` 内部直接用，没有别名）。页面 QML 用 `import AgentWorkbench` 拿 `components/` 里的共享组件。
- **QML 要调用的每个方法都必须 `Q_INVOKABLE`（或槽/信号），要赋值的每个属性都必须有 `WRITE`**：裸方法/裸写入器不在 meta-object 方法表里，QML 调用即抛「is not a function」/「read-only property」，而按页面加载的冒烟（改 `lastPageId` 注入）从不点击，抓不到——已有三处此类缺陷（切页、Web 表面切换、Chromium flags）因此长期静默失效。`check_architecture` 规则5 在构建期挡，`tst_shell::testQmlCalledMethodsAreInvokable` 用 `QMetaObject::invokeMethod` 复现 QML 的真实解析路径。
- **delegate 的 `required property` 按「属性名 = role 名」匹配**：模型 role 叫 `color`，属性就必须叫 `color`——改名后 delegate 拿不到 role、整个实例静默不渲染。此时若 delegate 根是 `Rectangle`/有同名视觉属性的类型，`color` 会**遮蔽**视觉属性、主题绑定落到字符串上、视觉永远保持默认值（Web 标签按钮因此长期渲染成白块）。两者都要顾到的写法：根用 `Item` + required property，视觉背景放内层子项（`AgentCard`、WebTabsPage 的 tab delegate 均如此）。
- 构建日志里 `AgentWorkbench.App` 的 unresolved-import 警告是 qmlcachegen 对纯 C++ URI 的**预期现象**，不要为了消除它改设计。
- 新增/移动 `.qml` 要同时改两处：`app/CMakeLists.txt` 的清单（`QT_RESOURCE_ALIAS` 决定 URL，形如 `qrc:/qt/qml/AgentWorkbench/<area>/<Name>.qml`）与 `cmake/AwbTranslations.cmake` 的 `AWB_TS_SOURCES`（若文件里有 `qsTr()`）。

### 门面与状态

- QML 只与门面对象和模型交互——agent 操作走 `agents.*`（`AgentsFacade` 保留 0.3.0 的 Q_INVOKABLE/信号名），跨域动作走 `workbench.*`，通知走 `workbench.notify`/`toasts`；QML 不直接读写文件、不直接调 `Qt.openUrlExternally`。
- 运行状态通过向 `webUrl` 发 HTTP 健康检查来检测（任何 HTTP 响应 = 运行中；连接被拒绝/超时 = 已停止）。不要新增进程嗅探逻辑。`AgentHealthMonitor::runningChanged` 是**边沿触发**（状态不变不发）：它经 `BuiltinPages` 驱动 `WebTabsFacade::markOnlineForAgent`，每轮重发会把 error 标签无限拉回 loading 重载。
- Python / Node.js 版本由 `workbench::EnvironmentService` 检测，状态栏徽标绑定 `environment.*`；缺失时显示红色 ×。
- 面向 QML 的 API 按异步设计（`refresh()` 立即返回 + `xxxFinished` 信号），这样以后挪到工作线程不用改 QML。

### 启动与一次性命令

- 启动用 `core::ProcessRunner::startDetached`（先 `findExecutable` 解析 PATHEXT，`.cmd`/`.bat` 垫片经 `cmd /c` 包装），agent 在启动器关闭后仍继续运行；一次性命令（install/update/version/setup）统一走 `core::ScriptRunner`，输出实时上卡片。
- `launch()` 同时把 agent 的 stdout+stderr 重定向到 `<logsDir>/output/<agentId>.log` 并轮询 60 s：token 门禁的 harness（dsh）把**每进程随机**的带 token URL 打到 stdout 而非写 token 文件，`AgentUrls::sessionUrlFromOutput` 提取指向 `webUrl` 同一服务器的第一条 URL 作为 session URL。它经 `AgentsFacade::sessionUrlChanged` 交给 `BuiltinPages` → `WebTabsFacade::retargetTabForAgent` 换掉已开标签的 URL 重载；`WorkbenchContext::openWeb` 优先用它。停止 agent 时丢弃（token 已随进程死亡）。**带 token 的 URL 一律不进日志**（`redactedUrl` 同时抹 `?token=` 与 `#token=`）。
- `setupCommand` 成功（退出码 0）后写 `agent_state.json`（`AgentStateStore`），之后不再重复运行，除非卡片右键「重新初始化」。

### Web 与 Skills

- **每个 agent 一个持久 profile（`web::WebProfilePaths`，`<dataRoot>/webprofiles/<agentId>`）是硬要求**：Chromium 按 host 索引 cookie 且忽略端口，共享 profile 会让不同端口的本地服务互相串号（实测证据见 `docs/research/webengine-embedding.md`）。profile 必须是 **`QQuickWebEngineProfile`**（`WebEngineView.profile` 的类型，也是 QML 可解析返回类型的唯一注册类），且构造后要显式 `setOffTheRecord(false)`——Qt 6.7 的公开构造函数用空名字建 adapter、适配器构造时即据此定为隐身，`setStorageName` 不会翻转它，漏了这条 profile 会静默全内存、cookie 重启即失。
- WebEngineView 的 `LifecycleState` 是 **scoped 枚举**：QML 里写 `WebEngineView.LifecycleState.Active`，裸 `WebEngineView.Active` 是 undefined，赋值会静默失效。
- Skill 扫描根来自 `settings.json` 的 `skills.roots`，空数组 = 内置默认（`~/.agents/skills`、`~/.claude/skills`、`~/.codex/skills`、ZCode 插件缓存的通配路径、项目目录）。根路径**原样存取**（`~`、`%PWD%`、通配符在扫描时展开，展开后的形式不许回写），插件缓存按版本去重。

### 测试

- 「做完」的定义是 `bash scripts/build.sh --test` 全绿（8 个测试目标 + `check_architecture`）。
- 一个模块一个可执行；多个测试类经 `tests/awbtest.h` 的 `AWB_TEST(Class)` 注册，由 `awbtest_runner.cpp` 依次执行（`tst_shell` 单类且需要 `QApplication`，用 `QTEST_MAIN`）。
- **用例必须写在 `private Q_SLOTS:` 里**（大写宏，见编程规范；小写 `slots` 已禁用）：写在尾部 `private:` 之后的用例能编译、套件依然报 100% 通过，但根本不会执行，且没有任何警告。新加用例后用 `./build/tst_core -functions` 确认已注册。
- 测试不许依赖网络、本机已安装的 agent 工具或真实数据目录。

### Qt 5 / Qt 6 双版本兼容

本项目同时支持 **Qt 6（6.5+，主线）** 与 **Qt 5.15.16 LTS（兜底）**，任何新增或修改的代码都要让两条路线**都能编译通过**，而不是只保证自己手上那条。版本判定一律走 `QT_VERSION_MAJOR` / `#if QT_VERSION` 宏分支（构建期差异见 `构建` 一节与 `cmake/AwbQtCompat.cmake`），不要静默只写 Qt 6 可用的代码。

- **写 Qt 6 代码时同步想 Qt 5**：用到 Qt 6 的 API、属性或 QML 类型时，先确认 Qt 5.15 是否有对应物，没有就补一条 Qt 5 分支。Qt 5 路线的失败方式通常是**整页打挂或静默失效**，而 Qt 6 的构建与测试依旧全绿，所以不能只靠一次构建判断。
- **差异集中在两处，别散落**：构建期差异（组件改名、`qt_add_qml_module`、qrc 别名、`/utf-8` 等）写在 `cmake/AwbQtCompat.cmake` 的包装函数里；编译期差异在用到的源文件就近 `#if QT_VERSION` 分支。不要用 `setContextProperty`、QML 里的版本判断之类的旁路来绕开。
- **兼容不等于降级**：若某个组件/能力 Qt 6 有而 Qt 5.15 没有（或明显更好），**不要让 Qt 6 也退回低效方案**。这种情况 Qt 6 就用它的最优实现，Qt 5 单独写兼容分支（可以更朴素，甚至功能略少），两分支各自完整清晰。为了「一份代码走两边」而让 Qt 6 牺牲新能力是得不偿失的。
- Qt 5 分支只负责兜底：其验证基准是 5.15.16 LTS，新特性优先按 Qt 6 设计，Qt 5 侧的降级实现要显式、可辨认，而不是悄悄改变 Qt 6 的行为。

## 国际化（i18n）

这是一个国际化的开源项目，所有面向用户的字符串都必须可翻译。

- **源语言是英文。** 绝不要在 `tr()` 或 `qsTr()` 里写中文（或任何其它非英文语言）——源字符串必须是英文，`check_architecture` 规则3 会拒绝非 ASCII 源串。中文及其它语言的翻译放在 `translations/` 下的 `.ts` 文件里。
- **C++**：每个面向用户的字符串都用 `tr()` 包裹。
- **QML**：每个面向用户的字符串都用 `qsTr()` 包裹。
- 翻译文件位于 `translations/`。构建时通过 `cmake/AwbTranslations.cmake` 里的源清单运行 `lupdate`（从源码同步 `.ts`）和 `lrelease`（编译 `.qm`）。编译出的 `.qm` 以 Qt 资源形式嵌入在 `:/i18n/` 下。**新增可翻译文件时把它加进 `AWB_TS_SOURCES`**，不要改 `qt6_create_translation` 调用本身。
- `app/main.cpp` 安装 `QTranslator`，根据系统区域设置自动加载（前缀 `agentworkbench`）。
- 添加新语言：创建 `translations/agentworkbench_<locale>.ts`，把它加入 `AWB_TS_SOURCES`，然后构建（lupdate 会填充内容）。填写翻译后重新构建。
- **注释用中文，其余一律英文**：标识符、日志信息与提交信息用英文；代码注释（含 Doxygen 文档注释）按 `docs/zh/standards/coding-standard.md` 的规定用中文。注释不是面向用户的字符串，与上一条不冲突。
- 本地化文档（`docs/zh/`、`README-zh.md`）以及 `mkdocs.yml` 中的语言名称标签**不是**源代码——它们是正当的本地化内容，不受本规则约束。

## 分支与并行开发（多工作树）

**在同一台电脑上开多个工作树、让多个 agent 同时并行开发是本项目的常态**，不是例外。所有工作的终点只有一个：合并回 `dev`。

- **分支模型**：`dev` 是唯一集成分支，一切工作的终点；`main` 受保护，不在其上直接开发。功能开发用 `feat/<domain>-<topic>`，修复用 `fix/<topic>`，合并回 `dev` 后删除工作分支。
- **新任务开工作树**：用 `bash scripts/worktree-add.sh <分支> [base]`（如 `bash scripts/worktree-add.sh feat/web-xyz`，默认从最新的 `dev` 切出；分支已存在则直接检出）。工作树统一放在主仓库的 `.worktree/` 下，目录名是分支名把 `/` 换成 `-`（`.worktree/` 已 git-ignore），每个任务/agent 一个。删除工作树用 `bash scripts/worktree-add.sh --remove <名字>`——本机 git 2.7.2 没有 `git worktree remove`，脚本等价于 `rm -rf` + `git worktree prune`，只删已注册的工作树、不动分支（分支合并后自行 `git branch -d`）；目录被占用（shell/编辑器还在里面）时会删失败，离开该目录重试即可。已有哪些工作树以 `--list`（即 `git worktree list`）的实时输出为准（本机已知另有 `C:/src/Qt/AgentLauncher`、`C:/src/Qt/agent-workbench-dev2` 等旧工作树）。
- **submodule 必须离线初始化**：`third_party/spdlog` 的远端在 gitee，内网不可达，新工作树里裸跑 `git submodule update --init` 必然失败，**不要在 worktree 里手动跑它**——`worktree-add.sh` 创建时用 `git -c` 把 submodule URL 临时指向主仓库已有的检出、从本地完成克隆（不污染任何 git config；每个工作树的 submodule gitdir 独立，位于 `.git/worktrees/<id>/modules/`，互不共享状态）。前提与例外：主仓库的 submodule 尚未检出时（无本地克隆源）需先在有网环境初始化一次；base 移到更新过 submodule 的提交时，先在主工作树 `git submodule update`，否则新工作树离线拿不到新 SHA。
- **状态随时在变，合并前必须实时核对**：`git status` 快照和「`dev` 检出在某处」这类前提只代表看到它的那一刻——并行会话可能在你任务中途切走分支、产生在途改动。每次合并前重新执行 `git -C <目标工作树> branch --show-current` 与 `git -C <目标工作树> status --porcelain`，以实时结果为准。
- **合并回 `dev` 按实时核对结果选路径**：
  1. `dev` 检出在某个工作树、且该树干净 → `git -C <该工作树> merge <工作分支>`；
  2. 该工作树有在途改动或已切到别的分支 → **不要动它**（在那里切分支 / checkout / reset 会毁掉对方的在途改动）；只要 `dev` 此刻没被任何工作树检出，就用临时工作树合并、用完即删：
     `git worktree add <临时路径> dev && git -C <临时路径> merge --ff-only <工作分支> && bash scripts/worktree-add.sh --remove <临时路径>`（临时路径建议也放 `.worktree/` 下，如 `.worktree/_merge-dev`；本机 git 2.7.2 没有 `git worktree remove`）；
  3. `dev` 被占、又无法走上述路径 → 先与用户协调，不要抢别人正在用的工作树。
- **降低并行冲突**：动手前先把 `dev` 最新改动合并进工作分支；收尾合并前**再**合并一次 `dev`，让冲突提前暴露，而不是攒到最后一次；配合下节的原子提交纪律。
- **冲突就地解决**：按双方改动的**意图**合并而不是机械取一侧；解决后必须重跑 `bash scripts/build.sh --test` 全绿再提交。`translations/*.ts` 的行号差异是每次构建 lupdate 重写 location 造成的噪声，取任一侧即可。
- **「任务完成」的定义包含「已合并回 `dev`」**——不许把冲突、分叉或「没合并回 dev」的状态留给下一个任务。

## 提交

- 每完成一个完整任务就提交一次，不要只改不提交；
- 提交信息使用 **Conventional Commits**（`feat` / `fix` / `docs` / `style` / `refactor` / `perf` / `test` / `build` / `ci` / `chore` / `revert`）。
- 版本遵循 **SemVer**，每次发版更新 `CHANGELOG.md`。
- 只提交本次任务相关的文件——这个仓库的工作区经常有其它在途改动，不要用 `git add -A`。除非用户明确要求。

在进行代码提交时，应避免将一个大任务的全部改动积压到最后一次性提交。每个提交应尽量保持原子性，并尽可能保证可独立构建、测试通过、审查和回滚。这样可以缩小变更范围，降低合并时产生大量冲突的概率。

## 不要做

- 不要在 C++ 中硬编码 agent 定义。
- 不要在 `tr()`/`qsTr()` 里写非英文的源字符串。
- 不要在 QML 里写字面颜色（`check_architecture` 会拒绝）。
- 不要让领域模块互相 include；不要用 `setContextProperty`；不要往 `AgentWorkbench` URI 手工注册单例。
- 不要给 `settings.json`/`agents.json` 加版本迁移代码——内置定义每次启动都按随包默认重新生成。
- 不要为了消除 qmlcachegen 的 unresolved-import 警告去改 QML 注册设计。
- 不要在注释里复述代码（「设置名称」这类），也不要为了统一注释格式去做整文件/全库的机械重排——并行工作树下会制造大面积冲突；只规范你改动到的类与函数。
- 除非明确要求，不要运行 `git push`（提交规则见上一节「提交」）。

## 停止 agent

agent 运行期间，卡片右上角会显示一个低调的 ×。点击它会结束**本次会话中由此启动的**进程树——`AgentRuntime::launch()` 记录 `startDetached` 返回的 PID，`stop()` 经 `core::ProcessRunner::killTree` 运行 `taskkill /F /T /PID …`，使整棵 `cmd → .cmd → node` 进程树终止。PID 只保存在内存中，因此：

- 通过 HTTP 健康检查检测到运行中、但**并非由本启动器启动**的 agent 没有 PID；`stop()` 只显示一条提示，不会结束任何进程。这类 agent 请用它们自己的命令停止（或用右键菜单的「强制停止」——`AgentRuntime::forceStop` 按端口经 `netstat -ano` 找 PID 再杀）。
- 启动器重启后 PID 丢失——被健康检查识别为运行中的 agent，在重新从这里启动之前，无法从卡片上停止。

对 dev server 启动器而言，强制结束是有意为之；需要优雅退出的 agent 仍应通过它们自己的命令停止。

## Windows 下的启动

`AgentRuntime::launch()` 先用 `core::ProcessRunner::findExecutable` 解析裸程序名（它会应用 PATHEXT），再通过 `cmd /c` 运行 `.cmd`/`.bat` 垫片——单靠 `CreateProcess` 找不到 `qwen.cmd` 这类 npm 风格的垫片。启动失败会发出 `launchFailed(id, message)`；界面会在卡片原位闪一次红色，并弹出说明原因的弹窗。
