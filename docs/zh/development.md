# 开发

## 前置依赖

- **Qt 6.5+**，模块：`Core`、`Gui`、`Qml`、`Quick`、`QuickControls2`、`Network`。
- **CMake 3.16+**
- **C++17** 编译器（MSVC 2019+、GCC 9+ 或 Clang 10+）
- （可选）**Ninja** 生成器，构建更快。

## 构建

`scripts/build.sh` 一条命令完成配置与编译：它会自动探测 Qt 与 MSVC 工具链、复用已有构建目录的生成器与 Qt 前缀、在项目目录被移动后清理陈旧的 CMake 缓存，并生成供编辑器索引使用的 `compile_commands.json`。

```bash
bash scripts/build.sh              # Debug 构建到 build/
bash scripts/build.sh --test       # 构建后运行单元测试
bash scripts/build.sh --release    # Release 构建到 build-release/
bash scripts/build.sh --help       # 查看全部选项
```

Windows + MSVC 下，从 Git Bash 调用编译器需要一个加载 `vcvars64.bat` 的 `.bat` 包装：用 `eval "$(cmd /c ... set)"` 把该环境导入 Git Bash 是行不通的，cmd 收到的是转义后的引号，`cl.exe` 始终进不了 `PATH`。脚本会自动生成这个包装脚本（`build/.build-agentworkbench.bat`）。

手工构建依然可行，但需要自己先准备好 MSVC 环境（例如在「x64 本机工具命令提示符」中运行）：

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

`bash scripts/package.sh` 会构建 Release 产物、用 `windeployqt` 部署依赖，并打包成 `dist/AgentWorkbench-<版本>-win64-Portable.zip`。

## 项目结构

```
app/          可执行文件：只做组装（main.cpp、QML 模块、资源）
src/
  core/         L0 基础设施：Paths、JsonStore、Settings、Logging、
                ProcessRunner、ScriptRunner、HttpProbe、PluginHost、LegacyImport
  plugin_api/   L0 插件 ABI（仅头文件，外部仓库链接它）
  theme/        L1 主题引擎：JSON 主题 → 语义令牌 → QML
  agentcatalog/ L2：外部 agent 工具目录（定义、持久化、进程、健康检查、CRUD）+ QML
  shell/        L2 UI 框架：导航、窗口骨架、toast、A* 组件
  skillcatalog/ L2：本机 skill 目录（SKILL.md frontmatter、扫描器、模型、门面）+ QML
  tools/        L2：Agent Tools 页（工作区记忆、懒加载文件树、图标映射、提示词草稿）+ QML
  web/          L2：标签、表面、内存策略 + QML
    webengine/  L2 适配器（唯一链接 Qt WebEngine 的目标）
  workbench/    L3：跨域意图、内置页面、环境探测、插件服务
cmake/        公共构建选项（AwbOptions.cmake、AwbTranslations.cmake）
resources/    内置主题 JSON
config/       default_agents.json（打包为 Qt 资源）
icons/        SVG 图标（打包为 Qt 资源）
tests/        每模块一个测试目标 + check_architecture
scripts/      build.sh、package.sh、check-architecture.sh
docs/         MkDocs 站点（英文 + zh/）
```

## 构建选项

| 选项 | 默认 | 含义 |
|---|---|---|
| `AWB_ENABLE_WEBENGINE` | `ON` | 内嵌 Web 视图（仅 MSVC；MinGW + ON 在配置期报错） |
| `BUILD_TESTING` | `ON` | 单元测试目标（需要 Qt Test 模块） |

在 `--` 之后传额外的配置参数，例如：

```bash
bash scripts/build.sh -- -DAWB_ENABLE_WEBENGINE=OFF
```

## 运行测试

```bash
bash scripts/build.sh --test
```

ctest 每个模块一个可执行文件，外加架构守门：

| 测试 | 覆盖 |
|---|---|
| `check_architecture` | QML 无字面色值（十六进制与数字 `Qt.rgba`）、无反向/横向模块依赖、源串全英文、core/theme 不含 UI、QML 单例方法调用均为 `Q_INVOKABLE`、属性赋值均有 `WRITE` 访问器 |
| `tst_core` | 路径、JSON 存储、设置、日志、进程执行器、脚本执行器、HTTP 探测、插件清单、旧目录接管 |
| `tst_agentcatalog` | 仓库同步语义、模型角色、门面 CRUD、脚本日志、URL、运行时启动/停止/强制停止 |
| `tst_theme` | 加载器校验规则、注册表覆盖行为 |
| `tst_shell` | 导航注册、徽标、窗口持久化、剪贴板结果 |
| `tst_web` | 标签复用、关闭语义、离线/在线转换、LRU 释放（不链接 WebEngine） |
| `tst_workbench` | 跨域意图：`openWeb` 跳页、external surface 不建标签、浏览器打开的 URL 交接 |
| `tst_skillcatalog` | frontmatter 解析、扫描、插件版本去重、过滤 |
| `tst_tools` | 工作区存储语义、懒加载树模型（roles/fetch/增量刷新）、图标映射、门面接线、QML 可调用面 |

可以按名字运行单个用例，例如 `./build/tst_core testRoundTrip`。

## 架构

分层与依赖规则简述：

- **分层**：`app → workbench → {shell, agents, skills, tools, web, theme} → core`。
  领域模块之间零依赖，跨域行为写在 `awb_workbench`。
- **配置化驱动**：状态在 `agents.json` 与 `settings.json` 里；UI 既不硬编码
  条目、也不直接写文件。
- **QML 契约**：页面只用语义令牌（`theme.surfaceBg` 等）——字面色值会被
  `check_architecture` 拒绝。C++ 全局注册在 `AgentWorkbench.App` URI 上，
  类型名大写，经窗口根别名以小写契约名（`theme`、`nav`、`agents`…）暴露。
- **健康检查**：运行态来自对 `webUrl` 的 HTTP 探测（任何响应 = 运行中），
  不要新增进程嗅探。
- **日志**：`core::Logging` 按 5 MB × 3 文件轮转；命令记录真实执行的命令行。

## 文档站点

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

打开 `http://127.0.0.1:8000`。站点为双语（英文默认，中文在 `/zh/`），使用
基于文件夹的 i18n 插件。

## 约定

- 通过 `agents.json` 新增 agent，不要在 C++ 中硬编码。
- QML 只用主题令牌（`theme.*`），绝不写字面颜色——`check_architecture`
  测试会拒绝。
- 领域模块之间不许互相 include（也不许依赖 shell/workbench）；跨域行为写在
  `awb_workbench`。
- 运行态通过向 `webUrl` 做 HTTP 健康检查来检测，不要新增进程嗅探逻辑。
- 停止按钮只会结束本次会话内由此启动器启动的进程树；对于其它途径启动、仅被健康检查识别为运行中的 agent，保持其自管生命周期。
- 代码风格、命名与注释规则见[编程规范](standards/coding-standard.md)。
