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

Windows + MSVC 下，从 Git Bash 调用编译器需要一个加载 `vcvars64.bat` 的 `.bat` 包装：用 `eval "$(cmd /c ... set)"` 把该环境导入 Git Bash 是行不通的，cmd 收到的是转义后的引号，`cl.exe` 始终进不了 `PATH`。脚本会自动生成这个包装脚本（`build/.build-agentlauncher.bat`）。

手工构建依然可行，但需要自己先准备好 MSVC 环境（例如在「x64 本机工具命令提示符」中运行）：

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build
```

`bash scripts/package.sh` 会构建 Release 产物、用 `windeployqt` 部署依赖，并打包成 `dist/AgentLauncher-<版本>-win64-Portable.zip`。

## 项目结构

```
src/         C++ 后端
  main.cpp            注册 model + launcher，加载 QML
  AgentConfig         加载/保存 agents.json（内置项来自随包默认配置）
  AgentModel          暴露给 QML 的 QAbstractListModel
  AgentLauncher       启动(QProcess)、健康检查(HTTP)、打开浏览器/目录
qml/         QML 界面
  main.qml            首页卡片网格 + StackView
  AgentCard.qml       单卡片（启动/配置 + 运行中高亮）
  AgentEditPage.qml   单 agent 设置页（命令、web 地址、配置目录）
  SettingsPage.qml    启动器管理（添加/编辑/删除）
config/      default_agents.json（打包为 Qt 资源）
icons/       SVG 图标（打包为 Qt 资源）
docs/        MkDocs 站点（英文 + zh/）
```

## 架构

- **配置化驱动**：`AgentConfig` 持有 agent 列表；UI 从不硬编码 agent 条目。
- **Model**：`AgentModel`（`QAbstractListModel`）把 agent 字段以 QML role 暴露
  （`agentId`、`name`、`command`、`webUrl`、`configDir`、`icon`、`color`、`running`）。
- **Launcher**：`AgentLauncher` 负责启动（`QProcess::startDetached`）、周期 HTTP 健康检查
  （`QNetworkAccessManager`）、打开 web 端/配置目录（`QDesktopServices`）。
- **运行状态**通过 `dataChanged` 回传 model，卡片据此做颜色动画。

## 文档站点

```bash
pip install mkdocs mkdocs-material mkdocs-static-i18n
mkdocs serve
```

打开 `http://127.0.0.1:8000`。站点为双语（英文默认，中文在 `/zh/`），使用
基于文件夹的 i18n 插件。

## 约定

- 通过 `agents.json` 新增 agent，不要在 C++ 中硬编码。
- 编辑 QML 时保持深色主题调色板（Catppuccin Mocha）。
- 运行态通过向 `webUrl` 做 HTTP 健康检查来检测，不要新增进程嗅探逻辑。
- 停止按钮只会结束本次会话内由此启动器启动的进程树；对于其它途径启动、仅被健康检查识别为运行中的 agent，保持其自管生命周期。
