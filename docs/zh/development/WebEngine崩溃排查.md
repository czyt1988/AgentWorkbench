# WebEngine 崩溃排查

内嵌浏览器的崩溃信号是什么含义、如何拿到引擎自己的判决、以及实际遇到过的崩溃原因。内嵌标签
死掉时，先读本文，再去怀疑宿主侧代码。

用户视角见 [Agent Web UI](../guide/网页界面.md) 与 [问题排查](../guide/问题排查.md)。
实现见 [WebEngine 适配层](WebEngine适配层.md) 与 [Web 标签页](Web标签页.md)。

## 第一个事实：渲染进程不是应用本身

内嵌标签由独立的 Chromium 进程渲染——Release 是 `QtWebEngineProcess.exe`，Debug 构建是
`QtWebEngineProcessd.exe`（见下文）。它死掉时宿主应用继续运行，
`WebEngineView.renderProcessTerminated(status, exitCode)` 触发；表面把
`WebEngine: render process terminated <status> <exitCode>` 落进日志，并把标签置为 `crashed`
态。覆盖层绝不自动重载：崩溃循环比手动重载更糟。

浏览器进程侧的代码（`awb_web`、`awb_web_webengine` 与表面 QML 的全部）无法破坏渲染进程的
内存。宿主侧改动只有一种可能成为原因：跑在**页面内部**的代码——注入脚本
（`compat-polyfills.js`）或 `runJavaScript` 调用。

## 读懂 status 与 exit code

`status` 是 `WebEngineView.RenderProcessTerminationStatus`（core 侧的
`WebContentsAdapterClient::RenderProcessTerminationStatus` 顺序相同）：

| 值 | 名字 | Qt 从哪个 Chromium `base::TerminationStatus` 映射而来 |
|---|---|---|
| 0 | `NormalTerminationStatus` | `NORMAL_TERMINATION` |
| 1 | `AbnormalTerminationStatus` | `ABNORMAL_TERMINATION`、`LAUNCH_FAILED` |
| 2 | `CrashedTerminationStatus` | `PROCESS_CRASHED`、`INTEGRITY_FAILURE`（Windows） |
| 3 | `KilledTerminationStatus` | `PROCESS_WAS_KILLED`、`OOM` |

`exitCode` 是操作系统层的原始退出码。Windows 上由 Chromium 的 `base::GetTerminationStatus`
（`base/process/kill_win.cc`）分桶：0 → 正常退出；外部终止码（0x1 任务管理器、0x40010004
调试器终止、0xC000013A control-C、0xC0000354 调试器不在场）→ was-killed；OOM 码（7012
沙箱内存上限、`kOomExceptionCode`）→ OOM；**其余一律 → crashed**。

**不要过度相信 status 的名字。** 本项目实测（2026-10-20，Qt 6.7.3）：一次经 stderr 证实为
Blink `DCHECK` 的渲染进程自杀（见下面的已定性案例），上报的却是 `status 3`
（KilledTerminationStatus）+ `exitCode -2147483645`——按上面的源码链条，该退出码应落
status 2。Windows 的 child-process-launcher 路径上某处重分类了桶。可靠的信号是
**exit code** 与 **stderr 的 FATAL 行**：

| exitCode | 含义 |
|---|---|
| `-2147483645` = `0x80000003` | `STATUS_BREAKPOINT`——Chromium 命中 `CHECK`/`DCHECK`，主动自杀 |
| `-1073741819` = `0xC0000005` | `STATUS_ACCESS_VIOLATION`——引擎内真实的内存错误 |
| `-1073740791` = `0xC0000409` | `STATUS_STACK_BUFFER_OVERRUN`——栈溢出保护 / fast-fail |
| `1`、`0x40010004` 等 | 被外部终止（任务管理器、`taskkill`、调试器）——不是 bug |

## Debug 与 Release：DCHECK 分界（先读这节）

Qt 在 `<Qt>/bin` 下**并排发行两个** WebEngine 渲染进程二进制：`QtWebEngineProcess.exe`
（Release）与 `QtWebEngineProcessd.exe`（Debug）。应用的 Debug 构建加载
`Qt6WebEngineCored.dll`，派生的是带 **d** 的那个——`DCHECK_IS_ON()` 的 Chromium。Blink、V8
与 content 层的所有 `DCHECK` 都是活的，而 DCHECK 失败就是致命的、主动的进程自杀
（`STATUS_BREAKPOINT`）。

推论：**只在 Debug 构建下崩溃的页面，绝大多数是踩中了 Chromium 的自校验断言——不是应用
bug。** 同一页面在 Release 构建下整段校验根本不编译，照常运行，最多有细微的渲染差异。

日志会话来自哪个构建，日志自己就能看出来：skill 扫描器会把它按可执行文件位置推导的
「项目目录」根落进日志——`SkillScanner: root <build-dir>/.agents/skills ...`。Qt Creator 的
Debug 会话显示 IDE 的影子构建目录（如 `build-AgentWorkbench-Qt_..._-Debug`）；
`scripts/build.sh --release` 的会话显示 `build-release`。

## 抓取引擎的判决（stderr）

应用日志只记 status 与 exit code。Chromium 自己的解释——
`FATAL:<file>(<line>) Check failed: <expression>`——走 **stderr**，GUI 方式启动时直接丢弃。
要抓到它，用一个批处理文件启动应用：

1. 加载 MSVC 环境（`vcvars64.bat`）——Debug CRT 需要；
2. 把 `<Qt>/bin` 放进 `PATH`；
3. 设 `QTWEBENGINE_CHROMIUM_FLAGS=--enable-logging=stderr`；
4. 运行 `<build-dir>/AgentWorkbench.exe > <log-file> 2>&1`。

复现崩溃后在日志文件里搜 `FATAL` / `Check failed`。FATAL 行点名 Chromium 源文件与失败的
表达式——通常足以认定子系统（样式、合成器、V8、网络……），无需再深挖。两个注意点：FATAL
行里的文件名可能被显示截断（按表达式匹配，别按文件名）；Chromium 行号只对该 Qt 内嵌的
版本有效（Qt 6.7.3 是 118，Qt 5.15 是 87）。

## 已定性案例：Chromium 118 样式引擎 DCHECK（仅 Debug）

症状（2026-10-20，qwen web shell 0.25.2）：标签加载正常（`[web-load] succeeded`、状态
`ready`），10~45 秒后在空闲期死掉；崩溃窗口内宿主日志完全静默；重载后确定性复现；Release
构建不受影响（已验证——同一页面在 Release 下运行数分钟无恙）。

stderr 判决（原文照录；文件名被显示截断——实为
`third_party/blink/renderer/core/css/resolver/style_resolver.cc`，行号以 Chromium 118 为准）：

```text
FATAL:er.cc(2350)] Check failed: g_null_atom == ComputeBaseComputedStyleDiff(applied_style, *correct_style) (<null> vs. "Field diff: box_shadow_(was ..., should be ...)")
```

该校验位于 `StyleResolver::ApplyPropertiesFromCascade` 的 `#if DCHECK_IS_ON()` 内：Blink 验证
「仅继承属性走缓存」快路径产出的 `ComputedStyle` 与完整 cascade 重算一致。qwen shell 的 CSS
让 `box_shadow` 字段出现分歧——这是 Chromium 118 的真实 bug——Debug 引擎把校验失败变成
致命崩溃。

处置：日常使用换 Release 构建；Debug 开发会话期间用外置浏览器打开该 agent。根治靠升级 Qt
（更新的 Chromium 已修上游 bug）。不要去翻宿主代码、不要清 profile、不要禁 service
worker——都与它无关。

## 其它候选原因（清单）

本项目尚未实测到，但当 stderr 的 FATAL 指向别处（或根本没有 FATAL）时的标准嫌疑：

- **GPU / 合成器**——与显卡驱动相关的崩溃或白屏。缓解：`web.chromiumFlags` 加
  `--disable-gpu`（该设置注入 `QTWEBENGINE_CHROMIUM_FLAGS`，下次重启生效）。
- **渲染进程 OOM**——重负载 agent UI 开太多活标签。由 `web.maxLiveTabs`（LRU 释放非激活
  标签）约束，可选配 `web.freezeInactiveTabs`。退出码落 OOM 桶，不是 `STATUS_BREAKPOINT`。
- **页面侧引擎**——现代 agent shell 的 WASM、worker、audio worklet 对内嵌 Chromium
  （Qt 6.7.3 是 118；Qt 5.15 是 **87**）。语法级缺口先表现为白屏——表面的白屏探测兜住
  这类（见适配层文档）；更深的不兼容会杀死渲染进程。
- **注入脚本 bug**——唯一跑在渲染进程里的宿主侧代码是 `compat-polyfills.js`
  （DocumentCreation、MainWorld）。它必须保持 Chromium 87 可解析（见适配层文档的 polyfill
  规则）；一个语法错误就会在旧引擎上打挂页面。
- **外部终止**——任务管理器、`taskkill`、调试器脱离：退出码 1 或 0x40010004。根本不是
  bug。

## 诊断手册

1. **在日志里定位崩溃**：`render process terminated <status> <exitCode>` 行与
   `WebTabs: tab <id> state ... -> crashed` 迁移。记下它与最后几条 `[web-load]`/`[web-nav]`
   的时间距离——空闲期崩溃意味着宿主代码没有在执行（表面只在加载、导航、控制台事件上
   动作）。
2. **认定构建类型**（Debug 还是 Release），用 skill 扫描指纹。Debug + `STATUS_BREAKPOINT`
   ⇒ 几乎必是 Chromium DCHECK；先换 Release 构建复测，再谈深挖。
3. **抓 stderr**（上面的配方），复现，读 FATAL 行——它点名子系统。
4. **动代码前先 A/B**：在干净的 `dev` 构建上复现。渲染进程崩溃在被证明之前都算页面/引擎
   侧的问题。
5. **检查 `web.chromiumFlags`**——用户设置里残留的实验性 flag 能造出任何构建都复现不了的
   崩溃。

## 相关

- [WebEngine 适配层](WebEngine适配层.md)——表面、profile、polyfill、白屏探测与加载日志。
- [Web 标签页](Web标签页.md)——标签状态机；`crashed` 态绝不自动重载。
- [设置](设置.md)——`web.chromiumFlags`、`web.maxLiveTabs`、`web.freezeInactiveTabs`。
- 调研：[WebEngine 内嵌](../research/webengine-embedding.md)。
- [开发板块索引](index.md)。
