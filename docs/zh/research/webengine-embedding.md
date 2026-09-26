# 用 Qt WebEngine 内嵌 Agent WebUI 调研

> 调研日期：2026-09-26
> 调研对象：AgentLauncher 0.3.0（Qt 6.7.3 / msvc2019_64 / Windows 10.0.26200 x64）
> 结论：**可行**。真实 agent WebUI 已在内嵌视图中完整渲染并可交互；主要代价是便携包体积（35 MB → 约 110–130 MB）与引擎版本落后（Chromium 118 vs 本机 Edge 153）。
> 配套整改计划见 `docs/superpowers/plans/2026-09-26-webengine-embedding-integration.md`。

## 1. 调研的问题与方法

### 1.1 三个问题

1. 用 Qt 的 WebView 把 agent 的 WebUI 直接显示在应用内，方案是否可行？
2. 交互上会不会出现问题？
3. 性能上和直接用 Chrome / Edge 有多大区别？

### 1.2 方法

不修改本仓库，在项目外建了一个一次性探针工程（Qt Quick + `WebEngineView`），用四组实测回答上述问题：

| 实验 | 做法 | 回答 |
| --- | --- | --- |
| 能力探测 | 页面内 JS 探测引擎身份、编解码器、JS/CSS 特性、WebGL | 4.1 节 |
| 真实 WebUI | 启动 `opencode web`，把它的 SPA 加载进内嵌视图并截图 | 2.2 节 |
| 同页面 A/B | 一个模拟聊天 UI 负载的基准页，分别在 QtWebEngine 与 Edge 中执行 | 4.3 节 |
| 打包实测 | 对探针 exe 跑 `windeployqt`，在剥掉 Qt 环境的 PATH 下运行 | 2.4 节 |

另有针对性实验：跨端口 Cookie / localStorage 隔离（3.1 节）、进程结构（4.2 节）。

### 1.3 环境

| 项 | 值 |
| --- | --- |
| Qt | 6.7.3，`C:/Qt/6.7.3/msvc2019_64`（已含 WebEngine / WebView 组件） |
| 编译器 | MSVC 2019（VS 16 Community）+ Ninja |
| 浏览器对照 | Edge 153.0.4234.48（WebView2 运行时同为 153） |
| 被内嵌的 WebUI | OpenCode（npm 全局 `opencode`，`opencode web`） |
| 显示 | 单显示器、100% 缩放（DPR = 1，故分数缩放的结论留待人工验证） |

## 2. 可行性（问题 a）

### 2.1 环境就绪

本机 Qt 6.7.3 已经装了全部所需组件，无需额外安装：

- `Qt6WebEngineQuick` / `Qt6WebEngineCore` / `Qt6WebEngineQuickDelegatesQml`
- `QtWebEngineProcess.exe`、`resources/`（`icudtl.dat` + `*.pak`）、`translations/qtwebengine_locales`
- QML 模块 `qml/QtWebEngine`（含 `qmltypes`：`WebEngineView`、`WebEngineProfile`、`WebEngineSettings`、`WebEngineScript` 等）

### 2.2 真实 WebUI 实测

把 OpenCode 的 WebUI（`http://127.0.0.1:4199/`）加载进内嵌视图：

- 页面 **0.87 s** 完成加载（`loadStarted` 507 ms → `LoadSucceeded` 872 ms，TTFB 39 ms）。
- SPA 正常启动：标题由 `127.0.0.1:4199` 变为 `OpenCode`，侧栏项目列表来自后端（WebSocket 已连上）。
- 浏览器控制台 **零报错**，无 renderer 崩溃。
- 截图见探针工程 `shot-opencode.png`：中文界面、图标、布局完整。

同一探针加载自制渲染测试页（`caps.html`）也完全正常：中文/emoji/生僻字、inline SVG、渐变、圆角阴影、`textarea` 原生控件、逐字追加的长文本重排均无异常（`shot-caps.png`）。

### 2.3 接口可用性（以本机 6.7.3 的 `plugins.qmltypes` 为准）

| 能力 | 6.7.3 QML API | 说明 |
| --- | --- | --- |
| 视图 | `WebEngineView` | `url`、`zoomFactor`、`loadProgress`、`renderProcessTerminated` … |
| 配置档 | `WebEngineProfile` | `offTheRecord`、`storageName`、`persistentCookiesPolicy`、`httpCacheType` |
| 上下文菜单 | `contextMenuRequested` | 可自定义菜单（默认显示 Chromium 菜单） |
| 新窗口 | `newWindowRequested` | **不处理则请求直接失败**（见 3.2） |
| 下载 | `WebEngineProfile.downloadRequested` | 不处理则下载不落盘（见 3.2） |
| 对话框 | `authenticationDialogRequested` / `javaScriptDialogRequested` / `fileDialogRequested` | 未接管时显示 Qt 默认对话框 |
| 全屏 | `fullScreenRequested` | 需接管，否则页面全屏请求被忽略 |
| DevTools | `devToolsView` / `devToolsId` | **可在应用内嵌一个 DevTools 视图** |
| 查找 | `findText` | 页面内查找需自建 UI |
| 打印 | `printToPdf` | — |

初始化要求（Qt 文档）：`QtWebEngineQuick::initialize()` “**has to be done before QGuiApplication is created**”。当前 `src/main.cpp` 里 `QGuiApplication` 是第一个对象，接入时必须调整顺序。

### 2.4 打包与独立运行

对探针 exe 执行 `windeployqt --release --no-translations --no-system-d3d-compiler --qmldir ..`：

- 产物 **242 MB**（未压缩）/ **111 MB**（zip），其中 `Qt6WebEngineCore.dll` 单项 **149.6 MB**，`resources/` 13.9 MB，`QtWebEngineProcess.exe` 0.7 MB。
- `--no-translations` 只复制 `qtwebengine_locales/en-US.pak`（404 KB），避开了 Qt 安装目录里 33 MB 的整份 locale 目录。
- 在**不含任何 Qt 路径**的环境变量下直接运行部署目录内的 exe：渲染成功、截图正常 → 便携包路径成立。
- 对比现状：`dist/AgentLauncher` 88 MB / `dist/AgentLauncher-0.3.0-win64-Portable.zip` 35.0 MB。预计接入后 zip 约 110–130 MB、解压约 250–265 MB。

## 3. 交互风险（问题 b）

### 3.1 Cookie 会跨端口共享（本项目的重点风险）

QML 里未显式指定 profile 时使用默认 profile（off-the-record，内存内存储），**所有视图共用同一个 Cookie jar**。而 Chromium 的 Cookie 按主机名匹配、**不区分端口**（RFC 6265）。实测取证：在 `127.0.0.1:8741` 设置 cookie，导航到 `127.0.0.1:8742` 后服务端收到的是：

```
NOTE from 127.0.0.1:8741 :: {"js_cookie":"alapp=port8741; session=agent-A","ls":"port8741"}
NOTE from 127.0.0.1:8742 :: {"js_cookie":"alapp=port8741; session=agent-A","ls":null,
                            "server_saw_cookie":"alapp=port8741; session=agent-A"}
```

即 cookie（含 `session=agent-A`）被原样带到了另一个端口；`localStorage` 则正确隔离（按 origin，含端口）。AgentLauncher 的内置 agent **全部在 `127.0.0.1` 上**（58627 / 4096 / 4170 / 18789 / 3080），所以只要某个 WebUI 用 cookie 存会话（哪怕只是 `session` 这种通用名），就会和其他 agent 互相覆盖。

**对策**：每个 agent 一个独立 `WebEngineProfile`（持久化 + 各自 `storageName` + 独立存储目录），顺带决定 localStorage 是“重启保留”还是“每次重来”。

### 3.2 必须写代码接管的默认行为

| 场景 | 未接管时的行为（Qt 文档 / 本机核对） | 影响 |
| --- | --- | --- |
| `target="_blank"` / `window.open` / OAuth 弹窗 | **请求直接失败**（“If this signal is not handled, the requested load will fail”） | 登录、外链会没反应 |
| 下载（导出会话、保存文件） | `downloadRequested` 未处理则不落盘 | 用户点了没反应 |
| HTTP Basic 认证 / `alert` / `<input type=file>` | 显示 Qt 默认对话框（调用 `request.accepted = true` 可改为自绘） | 可接受，建议统一风格 |
| 页面全屏请求 | 忽略 | 视频全屏失效 |

### 3.3 快捷键与焦点

视图获得焦点后，F5 / Esc / Ctrl+W / Ctrl+F / Ctrl+P 等归 Chromium 处理，与 QML 侧的应用级快捷键冲突。需要明确“哪些键归应用”，用 `Shortcut { context: Qt.ApplicationShortcut }` 抢回；页面内查找（`findText`）要自建 UI 才可用。

### 3.4 后台节流与视图生命周期

Chromium 对被遮挡/隐藏的页面做节流。实测（Edge，窗口被遮挡）：`document.hidden = true`，`requestAnimationFrame` 掉到 **1 fps**。QtWebEngine 使用同一套节流逻辑，且视图可见性跟随 QQuickItem / 窗口状态，因此同类行为可预期（本机未单独复现，建议实现后回归验证）。影响：切到别的页面时被隐藏视图的渲染暂停，WebSocket/SSE 连接与消息处理不受影响，切回来会补齐；但若某个 WebUI 依赖 `setInterval` 轮询，隐藏时可能被降到约 1 次/分钟。

内存方面更要紧：单个视图实测约 **+250–350 MB** 工作集（见 4.2），5 个视图全开可达 1.5 GB 量级，因此需要明确的保活策略（Qt 6.5+ 的 `lifeCycleState` 支持 `Frozen`，可冻结隐藏视图释放内存）。

### 3.5 需人工验证的清单

以下项目无法自动化，实现后需手工回归一次：

- [ ] 中文输入法（IME）候选词、内嵌输入、光标跟随
- [ ] 非 100% 缩放的分数 DPI（125% / 150%）下的清晰度与命中区域
- [ ] 视图内复制/粘贴、与 QML 侧输入框的双向剪贴板
- [ ] 从资源管理器拖放文件进页面；页面内拖放上传
- [ ] 页面全屏、打印/导出 PDF、桌面通知（`Notification`）、摄像头/麦克风权限请求
- [ ] 页面缩放（Ctrl+滚轮 / `zoomFactor`）与应用的缩放策略是否统一

## 4. 性能对比（问题 c）

### 4.1 引擎与能力矩阵

内嵌引擎身份：`QtWebEngine/6.7.3 Chrome/118.0.5993.220`（Chromium 118，2023-09）。本机 Edge：153（2026）。**差距约 2.5 年 / 35 个大版本。**

实测特性（同一页面内 JS 探测）：

| 类别 | 结果 |
| --- | --- |
| 可用 | WebGL 1/2、`navigator.gpu`（API 存在）、WebSocket/EventSource、Worker/SharedWorker/ServiceWorker、WebAssembly、`crypto.subtle`、`navigator.clipboard`、`Notification`、`toSorted`/`toReversed`、`Object.groupBy`/`Map.groupBy`、`structuredClone`、`Intl.Segmenter`、`scrollend`、CSS `:has` / container query / `text-wrap: balance` / subgrid / `oklch`、`startViewTransition`、`showPopover` |
| **缺失** | `Promise.withResolvers`（Chrome 119+）、`URL.canParse`（Chrome 120+） |
| **缺失** | **H.264 / H.265 视频解码**：`canPlayType('video/mp4; codecs="avc1…"')` 返回空；VP9 / AV1 报告可用 → 官方 Qt 二进制只带自由编解码器，WebUI 里嵌 MP4 会播不了（Edge 正常） |
| 其它差异 | UA 里 `Windows NT 6.2`、品牌串为 `Not=A?Brand 99, Chromium 118`；无 Chrome/Edge 扩展、无浏览器账号同步等特性 |

> 注：UA 中 `NT 6.2` 与品牌串是 QtWebEngine 的固有写法。若某个 WebUI 用 UA 判断浏览器版本（例如要求 Chrome ≥ 120），会误判，属于需要逐工具验证的风险点。

### 4.2 资源占用（实测）

| 场景 | 进程数 | 工作集 (WS) | 私有内存 |
| --- | --- | --- | --- |
| 现状：AgentLauncher 0.3.0 启动器 | 1 | 181 MB | 161.5 MB |
| 探针（最小 Qt Quick 应用）+ 1 个视图（空白页） | 2 | 347.8 MB（应用）+ 78.9 MB（renderer） | 290 + 29.7 MB |
| 同上，加载 OpenCode WebUI | 2 | 324.2 + 102.4 MB | 263.7 + 45 MB |
| 同上，加载基准页（重 DOM 负载） | 2 | 301.3 + 111.6 MB | 249.4 + 61.7 MB |
| Edge（全新配置档、单标签页、同一基准页） | 16–18 | 674 MB – 1.1 GB | 388 – 631 MB |

结论：**QtWebEngine 比“开一个 Edge 实例”更省**（2 个进程 vs 16–18 个），但 Chromium 的浏览器/GPU/网络/存储逻辑跑在**应用进程内**——实测只有一个 `--type=renderer` 子进程，没有独立的 GPU/utility 进程。这意味着每开一个内嵌视图，要在启动器 181 MB 的基础上再加约 250–350 MB。

渲染进程是独立的：页面崩溃不会带崩主程序。但 GPU 驱动级故障**不在独立进程内**，隔离度低于浏览器。

### 4.3 同页面 A/B 基准

同一个基准页（模拟聊天 UI：流式 token 追加、1200 节点列表重渲、JSON 往返、正则转换、强制重排、20 万数字排序、滚动、帧率），两边各跑 2–3 轮取中位数，单位 ms：

| 基准 | QtWebEngine 118（可见） | Edge 153（被遮挡） |
| --- | --- | --- |
| 流式 DOM 更新 3000 次 | 1.8 / 1.9 | 1.6 – 2.1 |
| 渲染 1200 节点 | 1.7 | 1.5 – 2.6 |
| JSON 往返 20 次（~300 KB） | 14.1 / 15.3 | 13.4 – 20.1 |
| 正则转换 10 次 | 20.3 / 20.8 | 16.7 – 22.6 |
| **强制重排 2000 次** | **1129 / 1243** | **580 – 656** |
| 20 万数字排序 | 37.8 / 43.1 | 68.7 – 140.3 |
| 滚动 60 步 | 0.1 / 0.2 | 0 – 0.2 |
| 帧率（1.5 s 窗口） | **60 fps** | 0–1（被节流，无意义） |
| 页面加载到 `LoadSucceeded` | 0.42 – 0.97 s | 同量级 |

**方法与偏差说明（重要）**：Edge 侧的窗口在本环境中无法被置于前台（`SetForegroundWindow` 返回 false，页面自报 `document.hidden = true`），因此它的 CPU 数字受到后台优先级/节流影响，帧率不可采信；上表 Edge 列只能作为下界参考。可采信的结论是：

1. DOM/JS 吞吐两边在同一噪声范围内（都是 Chromium），日常聊天型 WebUI **不会明显比 Edge 卡**；
2. 唯一稳定的差距是**强制重排慢约 2 倍**（118 → 153 之间 Blink 有大量 layout/样式重算优化），这是“老引擎”最可能被感知到的地方；
3. 可见状态下内嵌视图稳定 **60 fps**（受垂直同步限制），GPU 合成与 WebGL 正常。

## 5. 代价与风险登记

| 项 | 现状 | 接入后 | 风险等级 |
| --- | --- | --- | --- |
| 便携包（zip / 解压） | 35 MB / 88 MB | 约 110–130 MB / 250–265 MB | 中（用户下载体验） |
| 单视图内存 | — | +250–350 MB WS | 中（多视图叠加） |
| 构建工具链 | MSVC 或 MinGW 均可 | **必须 MSVC**（Qt 不为 MinGW 提供 WebEngine） | 低（现有 kit 已是 msvc2019_64） |
| 引擎版本 | — | Chromium 118，落后约 2.5 年；缺 H.264；缺个别新 JS API | 中（需逐工具验证） |
| 安全更新 | — | Chromium 118 的安全修复止于 2023-09；本地内容风险低，若内嵌视图加载远端内容需重新评估 | 低—中 |
| GPU 依赖 | 纯 Qt Quick | WebEngine 需要可用的 GPU/OpenGL；远程桌面、老旧驱动、软件渲染环境下可能需 `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu` 或软件 OpenGL 回退 | 中 |
| 打包 | `--qmldir qml` 已覆盖 | 需按 2.4 节验证；`--no-translations` 只带 en-US locale | 低 |

## 6. 推荐路线

**推荐：用 QtWebEngine 内嵌，同时始终保留“在浏览器中打开”作为兜底。**

- 兜底不可省：OAuth 登录、需要浏览器扩展、需要 H.264 播放等场景仍要真浏览器。
- 页面崩溃/加载失败/渲染进程终止（`renderProcessTerminated`）时，卡片应引导用户走兜底路径。
- 若体积与引擎新鲜度成为硬约束，备选是 **Edge WebView2**：体积只 +约 2 MB、引擎永远跟随 Edge、自带 H.264；但它是 HWND 重控件，嵌入 Qt Quick 场景做叠加/动画很别扭，且没有 QML API。注意：**在 6.7.3 上用 Qt 自带的 `QtWebView` 模块并不省体积**——本机 `plugins/webview/` 里只有 `qtwebview_webengine.dll`，它只是 WebEngine 的包装。
- **无论是否内嵌，都建议先把 Qt 升到 6.9 / 6.10**：Chromium 130 / 134（来源：<https://wiki.qt.io/QtWebEngine/ChromiumVersions>），把版本差从 35 个大版本缩到约 20 个，代码改动几乎为零。

## 7. 整改计划概览

针对上文 3.x 与 5 节暴露的问题，整改计划拆成 10 个任务（完整任务书含文件清单、步骤与验收标准，见 `docs/superpowers/plans/2026-09-26-webengine-embedding-integration.md`）：

| # | 任务 | 解决的问题 | 验收要点 |
| --- | --- | --- | --- |
| 1 | WebEngine 作为可编译选项 + 正确的初始化顺序 | `initialize()` 早于 `QGuiApplication`；保留 MinGW/无 WebEngine 构建 | 开/关两种构建都能起，测试目标不受影响 |
| 2 | 每 agent 独立 `WebEngineProfile` + 存储目录 | Cookie 跨端口共享（3.1） | 两个端口之间 cookie 不串、localStorage 按预期保留或清除 |
| 3 | 接管新窗口、下载、对话框、全屏 | 3.2 的默认行为 | 外链/OAuth 有明确出口；下载落盘可见 |
| 4 | 快捷键、焦点、页面内查找 | 3.3 | F5/Esc/Ctrl+W 等按设计归属；查找可用 |
| 5 | 视图生命周期与内存预算 | 3.4、4.2 | 离开页面后内存回落；多视图有上限 |
| 6 | 打包：MSVC 守卫、locale、体积预算、部署验证 | 2.4、5 节 | 部署目录在无 Qt 环境下可运行；zip 体积入档 |
| 7 | 能力兜底：加载失败回退、UA/引擎版本说明 | 4.1、5 节 | WebUI 起不来时用户有明确下一步 |
| 8 | 人工验证矩阵（IME/DPI/剪贴板/拖放/全屏/通知/打印） | 3.5 | 清单逐项签字 |
| 9 | 修 OpenCode 端口冲突 | 见下 | 新默认端口不与他人冲突；改默认配置后存量安装自动生效 |
| 10 | 文档与变更记录 | — | AGENTS.md / README / CHANGELOG 同步 |

> **顺带发现的独立问题**：OpenCode 的默认端口 `4096` 在本机被 VS Code 的 Kilo Code 扩展（`kilo.exe`）占用，`opencode web --port 4096` 启动即失败（`ServeError`，换 4199 正常）。修法是直接改 `config/default_agents.json` 里的 `webUrl` 与 `command` 并重新编译：内置 agent 现在每次启动都由随包默认配置重新生成（`AgentConfig::withBuiltinDefaults()`），端口变更会覆盖存量安装；用户在设置页自建的 agent 不走这条路径，需要自行在设置页修改。

### 7.1 需要拍板的决策点

1. **Qt 版本**：留在 6.7.3（Chromium 118）还是升到 6.9/6.10（Chromium 130/134）？
2. **默认行为**：点卡片默认内嵌打开，还是弹窗询问“内嵌 / 用浏览器打开”？兜底入口放哪里？
3. **视图保活**：离开页面就销毁视图，还是冻结（`Frozen`）保活以保留会话状态？
4. **是否需要“无 WebEngine”的便携包变体**（编译期开关默认值）？

## 附录 A：复现步骤

探针工程位于仓库外：`C:\src\Qt\_al_webengine_probe`（可整体删除，按下列文件可重建）。

| 文件 | 用途 |
| --- | --- |
| `CMakeLists.txt` / `main.cpp` / `main.qml` | 探针应用（可传 URL、截图路径、退出延时、第二个 URL） |
| `caps.html` | 渲染与特性探测页 |
| `fair.html` + `bench-server.js` | 同页面 A/B 基准与回报服务 |
| `set.html` / `echo.html` / `cookie-server.js` | 跨端口 Cookie / localStorage 隔离实验 |
| `build.bat` | MSVC + Ninja 构建 |
| `activate.ps1` / `measure.ps1` / `edge-focus.ps1` | 窗口置前、进程内存测量、窗口状态诊断 |

```bash
# 构建（MSVC 2019 + Ninja + Qt 6.7.3）
cd C:/src/Qt/_al_webengine_probe && cmd //c build.bat

# 加载任意 URL：<url> <截图路径> <退出延时 ms> [第二个 URL]
export PATH="/c/Qt/6.7.3/msvc2019_64/bin:$PATH"
./build/al_probe.exe "http://127.0.0.1:4199/" shot.png 25000

# 打包体积
cp build/al_probe.exe deploy/ && C:/Qt/6.7.3/msvc2019_64/bin/windeployqt.exe \
    --release --no-translations --no-system-d3d-compiler --qmldir . deploy/al_probe.exe
```

## 附录 B：数据来源与不确定性

- **可直接复现**：引擎身份、能力矩阵、加载耗时、内存、进程结构、部署体积、跨端口 Cookie 行为、部署包独立运行。
- **单机单轮、方向性结论**：4.3 节基准（含 Edge 侧被节流带来的偏差）。
- **未验证、需人工**：3.5 节清单（IME、分数 DPI、剪贴板、拖放、全屏、通知、打印）。
- **需回归确认的推断**：QtWebEngine 的后台节流行为（依据是同一套 Chromium 逻辑 + Edge 实测，未在 Qt 侧单独复现）。
