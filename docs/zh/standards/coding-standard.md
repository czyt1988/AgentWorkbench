# 编程规范

本文规定 AgentWorkbench 的代码写成什么样子：**C++ / Qt / QML 的写法、命名、文件组织，以及 Doxygen 注释规范**。

它与另外两份文档共同构成完整约束，分工如下：

| 文档 | 管什么 |
|---|---|
| `AGENTS.md` | 怎么构建、怎么测试、怎么提交、怎么开工作树；各模块的硬性契约与已知陷阱 |
| `designs.md` | 界面骨架、侧栏规则、组件复用目录、视觉语言 |
| **本文** | 文件与命名、C++/Qt/QML 写法、**注释规范** |

## 0. 效力与适用范围

- 适用于 `src/`、`app/`、`tests/`、`examples/` 下的全部 C++、QML 与 CMake 代码。
- **新写的代码必须完全遵守本文。** 其中一部分由 `scripts/check-architecture.sh`（ctest 的 `check_architecture`）在构建期强制：QML 不许有字面颜色、模块依赖方向、`tr()`/`qsTr()` 源串必须是 ASCII、`src/core/` 与 `src/theme/` 不许出现 UI 类型、QML 调用的每个方法必须 `Q_INVOKABLE`、赋值的每个属性必须有 `WRITE`。
- 修改既有代码时，**只**规范你改动到的类与函数。不要发起整文件或全库的机械重排——本仓库经常在多个工作树里并行开发，全文件重写会制造大面积冲突，让审阅无法进行。
- 本文与既有代码里偶然出现的写法冲突时，以本文为准。

## 1. 文件与基本格式

- 源码文件一律 **UTF-8（无 BOM）**、**LF 换行**、**4 空格缩进**，不使用 Tab。MSVC 侧由 Qt6 的 CMake 目标自动注入 `/utf-8`，编译器按 UTF-8 解读源文件，中文注释因此是安全的。
- 单行长度 **不超过 100 字符**，尽量控制在 90 以内；超长表达式换行时，续行与本行首个参数对齐。
- 文件名与被包装的主类同名，用 PascalCase：`AgentHealthMonitor.h` / `AgentHealthMonitor.cpp`。一个文件一个主类，不要在一个 `.h` 里塞多个不相关的类。
- **不写文件头注释块**：作者、日期、版权、修改记录一律不写，git 已经记录了这些信息。文件或类型的职责写在该类自己的 Doxygen 注释里。
- 头文件必须自包含：单独 include 它就能编译。用前置声明代替 include，减少头文件之间的传染（`AgentsFacade.h` 是范例）；Qt 容器与值类型（`QString`、`QVariantMap`）直接 include。
- include 顺序固定为三段，段间空一行：

  ```cpp
  #include "agentcatalog/AgentsFacade.h"   // 1. 本文件的对应头，永远第一行

  #include "agentcatalog/AgentModel.h"     // 2. 项目内其它头，按路径字母序
  #include "core/Settings.h"

  #include <QDir>                          // 3. Qt 与标准库，按字母序
  #include <QFile>
  ```

- 命名空间统一为 `awb::<模块目录名>`：`awb::core`、`awb::theme`、`awb::agentcatalog`、`awb::skillcatalog`、`awb::shell`、`awb::web`、`awb::workbench`、`awb::plugin`。命名空间用 C++17 的嵌套写法 `namespace awb::core {`，右花括号后必须带注释：`} // namespace awb::core`。
- 头文件保护宏为 `AWB_<模块>_<文件名>_H`，全大写，如 `AWB_CORE_PATHS_H`。重命名模块时**不要**去改既有文件的保护宏——没有收益，只制造 diff。
- **禁止在头文件中使用 `using namespace`**——它会污染所有 include 该头的翻译单元；`.cpp` 里同样不使用。需要缩短名字时，在 `.cpp` 里用单名声明：`using awb::core::EnvExpander;`。
- `.cpp` 里的私有辅助函数放进匿名命名空间（`namespace { ... }`），不要用 `static` 函数。

## 2. 命名

| 对象 | 规则 | 示例 |
|---|---|---|
| 类 / 结构体 / 枚举类型 | PascalCase | `AgentModel`、`OpResult`、`WebProfilePaths` |
| 枚举值 | PascalCase | `IdRole`、`NameRole`（模型 role 一律以 `Role` 结尾） |
| 函数 / 方法 / 局部变量 / 参数 | camelCase | `sessionUrlFromOutput`、`dataRoot` |
| 成员变量 | `m_` 前缀 + camelCase | `m_definitions`、`m_health` |
| 静态成员变量 | `s_` 前缀 | `s_testRoot` |
| 编译期常量 | `k` 前缀 + PascalCase，用 `constexpr` | `kSessionUrlTickMs`、`kMaxVisible` |
| 宏 / 保护宏 / 构建选项 | 全大写，`AWB_` 前缀 | `AWB_TEST`、`AWB_ENABLE_WEBENGINE` |
| 布尔量 | `is` / `has` / `can` / `should` 起头 | `isDataRootOverridden`、`hasLaunchedAgents` |
| QML 共享组件 | `A` 前缀，放 `src/shell/qml/components/` | `AButton`、`ACard`、`ATooltip` 风格 |
| QML 页面 | 模块目录内 PascalCase + `Page` 后缀 | `AgentGridPage.qml`、`SettingsPage.qml` |

补充约定：

- getter 不带 `get` 前缀：`model()`、`sessionUrl()`、`configFilePath()`。
- 信号是"已经发生的事实"，用过去式或状态变化名：`runningChanged`、`launchFailed`、`installFinished`、`sessionUrlChanged`。不要在信号名里写 `on` 或祈使动词。
- 槽与 `Q_INVOKABLE` 方法是"要求做的事"，用动词起头：`launch`、`stop`、`refresh`、`addAgent`。
- 变量名不用拼音、不用无意义缩写；`ctx`、`cfg`、`mgr` 这类缩写在超出本文件范围时写出全名。

## 3. C++ 规范

语言基线是 **C++17**。以下规则按"写代码时会遇到的顺序"排列。

### 3.1 基本写法

- 指针用 `nullptr`，不用 `0` 或 `NULL`。
- 重写虚函数必须写 `override`；不要重写时保留 `virtual` 前缀的旧写法。
- 单参数构造函数加 `explicit`（拷贝/移动构造除外）。
- 禁止裸 `new` / `delete`：`QObject` 派生对象用父子所有权，其它对象用值语义或 `std::unique_ptr`。
- 类型转换只用 `static_cast` / `qobject_cast`，禁止 C 风格强转。
- `auto` 只在与具体类型无关或类型冗长到影响阅读时使用（迭代器、`std::make_unique`）；拿到的类型会影响理解时必须写出来。
- 成员在类内给默认值：`bool ok = true;`，不要写在构造函数初始化列表里重复一遍。
- 传参：`QString`、容器与自定义类型按 `const &` 传（小到 8 字节以内、或需要在函数内修改副本时按值传）；返回值依赖 RVO，不要写 `return std::move(x)`。
- 能加 `const` 就加：不修改成员的成员函数必须是 `const` 成员函数。
- 提前返回、减少嵌套：先处理失败与边界，再写主流程；嵌套超过 3 层的 `if` 说明该抽函数了。
- **单语句的 `if` / `for` / `while` 也必须写花括号**，不省略。改既有代码时把你动到的语句补上花括号即可，不做全文件机械替换。
- 字符串字面量：面向用户的走 `tr()`，其余一律 `QStringLiteral`。路径拼接用 `QStringLiteral("/log")` 而不是 `QLatin1String` 或裸字面量。
- 枚举：需要暴露给 QML 或需要元信息的用普通 `enum` + `Q_ENUM`（见 `AgentModel::Roles`）；纯内部的状态量用 `enum class`。

### 3.2 错误处理与跨模块契约

- **跨模块边界不抛异常**：同步可失败的操作用 `core::OpResult`（`{ ok, error }`，QML 可直接读 `.ok` / `.error`），异步操作用信号回报结果。异常无法穿越 C++/QML 边界，这条对 QML 暴露面是硬约束。
- **用异常，不用 `std::optional`**：模块内部的 C++ 代码里，"可能失败 / 值不存在"用异常表达，不要引入 `std::optional` 返回值——调用方容易漏查 `has_value()`，让失败以空值形式静默传播。与上一条的分工：跨模块边界（尤其面向 QML）用 `OpResult` / 信号，模块内部用异常。
- 失败必须带可读原因（英文），不要只返回 `false` 就让调用方猜。
- 不吞错误：捕获不到、修不了的情况记一条 `qWarning()` 再向上返回失败。
- 模块之间只通过公开接口交互；领域模块（`agentcatalog`、`skillcatalog`、`web`）之间零依赖，跨域行为写在 `awb_workbench`。

### 3.3 头文件与实现的边界

- 头文件是**契约**：类型定义、成员声明、内联 getter、`Q_PROPERTY`、信号。具体实现放 `.cpp`。
- 头文件里只允许写：单行的内联 getter/setter、模板、以及确有必要的小型内联函数。
- 不要在头文件里写实现细节的解释性注释——那是 `.cpp` 的内容（见第 6 节）。

## 4. Qt / QObject 规范

### 4.1 类的结构

成员声明按固定顺序排列，读者从上往下就能看出这个类的契约：

```cpp
class AgentsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)

public:
    // 1. 类型别名、枚举、构造函数
    // 2. 普通方法、Q_INVOKABLE 方法
Q_SIGNALS:
    // 3. 信号
public Q_SLOTS:
    // 4. 公有槽
private:
    // 5. 私有方法
    // 6. 成员变量（m_ 前缀）
};
```

- **继承 `QObject` 的类必须写 `Q_OBJECT` 宏**，无一例外：漏掉它 moc 不会为该类生成元对象代码，信号槽、`Q_PROPERTY`、`qobject_cast` 会静默失效或编译失败。
- **一律使用大写 Qt 宏**：`Q_OBJECT`、`Q_PROPERTY`、`Q_ENUM`、`Q_INVOKABLE`、`Q_SIGNALS`、`Q_SLOTS`、`Q_EMIT`；**禁止小写的 `signals`、`slots`、`emit` 关键字**——它们只在未定义 `QT_NO_KEYWORDS` 时可用，大写宏保证代码在任何 Qt 配置下都能编译，这也是 Qt 官方推荐的最佳实践。
- `Q_OBJECT` 紧跟类名后的第一行；`Q_PROPERTY` / `Q_ENUM` 紧随其后。
- 构造函数：`QObject *parent = nullptr` 作为最后一个参数并带默认值；需要注入的依赖（`Settings *`、`Theme *`）排在前面的参数里。
- 析构函数只在需要清理非 QObject 资源时才写；此时基类析构用 `= default`。

### 4.2 与 QML 的接口

- **QML 要调用的每个方法都必须 `Q_INVOKABLE`（或槽/信号），要赋值的每个属性都必须有 `WRITE`**。裸方法不在 meta-object 方法表里，QML 调用时抛「is not a function」，而且只有点击才会暴露；`check_architecture` 规则 5 在构建期拦截。
- 面向 QML 的属性用 `Q_PROPERTY`，按 `READ` / `WRITE` / `NOTIFY` 排列；不变的用 `CONSTANT`。属性变化时必须 `Q_EMIT xxxChanged()`。
- 面向 QML 的 API 按**异步**设计：`refresh()` 立即返回，结果经 `refreshFinished` 类信号送达。这样以后挪到工作线程不需要改 QML。
- 与 QML 交互的模型继承 `QAbstractListModel`，暴露 `roleNames()`；role 名与顺序是对 QML 的稳定契约，改动前先确认所有页面。
- 全局对象只在组装处经 `qmlRegisterSingletonInstance` 注册到纯 C++ URI `AgentWorkbench.App`，**类型名必须大写**。不要用 `setContextProperty`，也不要往 `AgentWorkbench` URI 手工注册单例。QML 侧的小写契约名（`theme`、`agents`、`web`…）是 `MainWindow.qml` 根部的别名。

### 4.3 信号与槽

- `connect` 一律用成员指针或 lambda：

  ```cpp
  connect(m_runtime, &AgentRuntime::launchFailed, this, &AgentsFacade::launchFailed);
  connect(&m_timer, &QTimer::timeout, this, [this]() { poll(); });
  ```

  禁止 `SIGNAL()` / `SLOT()` 字符串宏——它放弃编译期检查，重命名后会静默失效。

- **信号与槽的参数类型必须逐字匹配，包括传递形式**：不要信号按值传、槽却用 `const &` 接收（如信号 `void changed(Foo foo)` 配槽 `void onChanged(const Foo &foo)`）。参数不一致的连接 Qt 在某些情况下会识别不了——能编译、却连不上或运行时静默丢弃，几乎没有线索可查，调试代价极高。
- **信号槽传递自定义类指针时，`connect` 处需要完整类型**：头文件里只有前向声明会导致「不完整类型」编译错误。做法保持既有分工——头文件尽量前向声明，`.cpp` 里 `#include` 该类的完整头文件，不要为了 `connect` 把 include 搬进头文件。
- **信号只在状态真正变化时发射（边沿触发）**。重复发射同一个状态会让订阅方反复重载、重放动画；`AgentHealthMonitor::runningChanged` 就是这个规则的样板。
- 一个信号表达一个事实。需要同时传达"谁 + 结果 + 原因"时用多参数或 `OpResult`，不要发明"万能信号"。
- 耗时操作（网络、进程、磁盘扫描）不得阻塞 UI 线程，也不要写 `waitForFinished()` 式的同步等待；用信号/回调。

### 4.4 线程与 GUI

- **永远不要在线程里直接操作 GUI 控件**：Qt 的 GUI 对象（窗口、控件、QML 元素、`QQuickItem`）只能在主线程访问，跨线程访问是未定义行为——典型表现是偶发崩溃与画面错乱，而不是稳定复现的报错。
- 工作线程把结果交回主线程只有两条路：**信号槽**（跨线程连接自动排队到接收者线程执行）或 `QMetaObject::invokeMethod(接收者, ...)` 显式切换。线程侧只负责发信号 / 调 `invokeMethod`，所有触碰 UI 的代码写在主线程执行的槽（或 lambda）里。
- 本项目面向 QML 的 API 一律异步设计（`refresh()` 立即返回 + `xxxFinished` 信号，见 4.2），正是为把工作挪进线程留好余地：结果永远经信号回报，界面更新永远发生在主线程。

### 4.5 容器迭代与高 DPI

- **禁止对非 const 的 Qt 容器直接范围迭代**。Qt 容器是隐式共享（COW）的，对非 const 容器写范围 `for`——无论 `for (T &v : container)` 还是 `for (const T &v : container)`——都会调用非 const 的 `begin()`，触发 detach 深拷贝，`const T &` 救不了。正确写法二选一：容器本身声明成 `const`，或用 `std::as_const()` 包裹：

  ```cpp
  for (const AgentDefinition &def : std::as_const(m_definitions)) {
      ...
  }
  ```

  `std::as_const` 来自 `<utility>`（C++17），Qt 5 / Qt 6 两条路线通用；不要用 `qAsConst`（Qt 5 专属，Qt 6 已废弃）。

- **高 DPI / 缩放比例 ≠ 100% 的环境下，`QPixmap` 的物理尺寸与逻辑尺寸不一致**：`width()` / `height()` 返回的是物理像素，直接拿来计算绘制或摆放位置会偏移，**必须除以 `devicePixelRatio()` 换算成逻辑尺寸**。Qt 6 路线可直接用 `deviceIndependentSize()` 一步取得；Qt 5 兼容分支手工做除法。

### 4.6 用户可见字符串与日志

- 用户可见字符串用 `tr()` 包裹，源串必须是英文（`check_architecture` 规则 3）；翻译放 `translations/`，用 `%1` 占位符配 `.arg()`。
- 内部字面量用 `QStringLiteral`，不要用 `tr()` 包不该翻译的内容（命令行、JSON 键、URL 片段）。
- 日志用 `qInfo()` / `qWarning()`，英文，带 `[app]` / `[cmd]` 前缀与操作名、对象 id（见 `AgentsFacade.cpp` 的 `logPrefix`）。提交前不要留下 `qDebug()`。
- 应用级事件日志用 `core/Logging.h` 的 `AWB_DEBUG` / `AWB_INFO` / `AWB_WARNING` / `AWB_CRITICAL` 宏：级别取宏名，分类固定 `awb.event`（写进行前缀，将来的 UI 日志视图按它过滤）；模块内部的一般日志维持 `qInfo()` / `qWarning()` 加 `[module]` 前缀。写盘是异步的（后台线程消费队列），不要假设一行日志在 `qInfo()` 返回时已经落盘。
- 语言分工要记牢：**标识符、面向用户的字符串、日志、提交信息用英文；只有代码注释用中文**（见第 6 节）。不要把中文写进 `tr()`，也不要把中文写进日志。
- 日志中禁止出现带 token 的 URL——`redactedUrl` 同时抹掉 `?token=` 与 `#token=`。

## 5. QML 规范

界面骨架、侧栏规则与组件复用目录以 `designs.md` 为准，本节只讲代码层面的写法。

### 5.1 文件结构

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench          // 共享组件（A*、PageHeader 等）
import AgentWorkbench.App      // C++ 单例（theme、agents、workbench …）

// 组件说明：它解决什么问题、契约是什么（必须写，中文）
Item {
    id: page

    // 属性声明
    property string filterText: ""

    // 复杂逻辑抽成 function
    function recountShown() { ... }

    // 子项
}
```

- import 顺序：`QtQuick` → `QtQuick.Controls` → `QtQuick.Layouts` → `AgentWorkbench` → `AgentWorkbench.App`。
- 根元素 `id` 用 `page`（页面）、`control`（控件）、`root`（其它）——既有代码的取法，保持一致。
- 组件文件顶部必须有一段说明该组件用途与契约的注释（中文）；属性、函数、非显然的子项同样要有说明。

### 5.2 与 C++ 的边界

- QML 只与门面对象和模型交互：agent 操作走 `agents.*`，跨域动作走 `workbench.*`，通知走 `workbench.notify`，环境状态走 `environment.*`，主题令牌走 `theme.*`。
- QML 不读写文件、不启动进程、不调 `Qt.openUrlExternally`——这些都要有对应的 `Q_INVOKABLE` 入口。
- 调用 C++ 属性/方法前确认它可调用（见 4.2）；C++ 侧的 `Q_INVOKABLE` 漏写不会在加载时暴露，只会静默失效。

### 5.3 视觉与主题

- 只用 `theme.*` 语义令牌，**绝不写字面颜色**（`#rrggbb`、`Qt.rgba(0.2, 0.3, …)` 都会被门禁拒绝）。深浅两套主题下都必须可读。
- 间距、圆角、字号、动效时长一律用主题令牌（`theme.spacingM`、`theme.radiusCard`、`theme.durationFast`），不要发明数字。
- 复用优先：按钮、输入框、卡片、弹窗、状态点等一律用 `components/` 里的 `A*` 组件，不要手写裸 `Button` + 自定义 `background`。货架目录与"什么时候用哪个"见 `designs.md` 第 3 节。
- 状态永远不只靠颜色表达：配 tooltip 或文字。

### 5.4 布局与交互

- 优先用 `ColumnLayout` / `RowLayout` / `GridLayout` 排版；布局里的子项用 `Layout.fillWidth` / `Layout.preferredWidth` 控制尺寸，不要给布局子项写 `width` 绑定——`width` 的赋值不会回流给布局，字面上"生效"了但兄弟节点不动（侧栏折叠留白就是这么来的）。需要动画时绑定 `implicitWidth` / `implicitHeight` 并对它们加 `Behavior`。
- 插槽类型的属性（`default property`、具名 slot）用 `RowLayout` 之类的布局容器，别用裸 `Item`。
- `MouseArea` 声明在它应该覆盖的区域之前，避免后声明的整块 `MouseArea` 吃掉前面按钮的点击（关闭按钮点不动是典型症状）。
- 附加式 `ToolTip` 必须写 `delay: 300`、`timeout: 10000`，宿主是 delegate 时加 `Component.onDestruction: ToolTip.hide()`——否则宿主被销毁后 tooltip 会冻在屏幕上。
- `WebEngineView` 的 `LifecycleState` 是作用域枚举：写 `WebEngineView.LifecycleState.Active`，裸写 `WebEngineView.Active` 是 `undefined`，赋值静默失效。
- 复杂逻辑写成 `function`，函数内用 `let` / `const`，不要把多步赋值塞进一行的绑定表达式里；注意绑定循环（`onXChanged` 里又改回 `x`）。

### 5.5 新增文件要动的地方

在某个模块的 `qml/` 下新增 `.qml` 后，两处清单必须同步（缺一处就是运行时找不到组件，或字符串不进翻译）：

1. `app/CMakeLists.txt` 里对应区域的清单——`_shell_qml`、`_component_qml`、`_agentcatalog_qml`、`_skillcatalog_qml`、`_web_qml`、`_webengine_qml`。那里的 `QT_RESOURCE_ALIAS` 决定组件 URL，形如 `qrc:/qt/qml/AgentWorkbench/<area>/<Name>.qml`；
2. 若文件里有 `qsTr()`，加进 `cmake/AwbTranslations.cmake` 的 `AWB_TS_SOURCES`。

QML 资源不放进静态库（qrc 初始化器会被链接器丢掉），所以清单集中在可执行文件这一层——模块自己的 `CMakeLists.txt` 只列 C++，不列 `.qml`。

## 6. 注释规范（Doxygen）

这是最容易被写歪的一节：注释不是"给自己看的便条"，而是这个项目的**代码文档**。所有注释用中文写，尽量说清楚，但**不写没有信息量的句子**。

总分工一句话：**头文件的成员函数只留简短普通注释，`.cpp` 的函数实现承载完整 Doxygen 注释**；信号、枚举、成员变量等没有 `.cpp` 可去的实体是例外，完整注释写在头文件里。

### 6.1 总则

1. **风格**：遵循 **Doxygen** 注释风格，但写在哪、用什么格式按本节分工执行——完整 Doxygen 块（`/** ... */`）写在 `.cpp` 的函数实现前，以及头文件的信号声明处；头文件的成员函数只写一行简短**普通**注释（`//`），内容等同于 `@brief`。
2. **语言**：中文。术语、类型名、函数名保留英文原文（`Q_INVOKABLE`、`facade`、role），不要硬译。
3. **为什么这样分工**：头文件被大量翻译单元 include，完整文档写在头文件里，文档一改所有依赖者全部重编；完整文档放 `.cpp`，改文档只重编单个翻译单元（详见 6.6）。头文件因此只保留"扫一眼就知道这个函数干什么"的那一句。
4. **同步**：改了行为、参数、返回值、失败条件，必须在同一次提交里改注释（头文件与 `.cpp` 两处都要改）。与代码不一致的注释视同缺陷。
5. **不写废话**：不复述代码已经说清楚的事。`// 设置名称`、`// 遍历列表`、`// 构造函数` 这类注释一律不要。简略注释说的是契约——什么时候用、保证什么——不是函数名的翻译。
6. **不做机械迁移**：不要为了统一格式去重写你没有改动的文件（见第 0 节）。

`.cpp` 里的注释分两类，位置不同：

- **文档注释**（Doxygen，`/** ... */`）：描述函数的契约——`@brief`、参数含义、返回值、副作用、失败条件。写在函数定义上方，**注释前空一行**，与上一段代码分隔。
- **软注释**（普通 `//`）：解释这段实现为什么这么写、有什么约束、踩过什么坑。写在被解释的代码上方。

实现层面的解释只出现在 `.cpp` 里，头文件不承载。

下面各节的示例取自本仓库的真实代码，演示的是**本规范的目标写法**。既有文件里尚未补齐的地方按第 0 节的迁移约定处理：你改到哪个类，就把那个类补齐，不做全库重写。

### 6.2 头文件：成员函数只写简短普通注释

头文件里需要注释的对象，以及各自的写法：

**类 / 结构体**——一段 `///` 说明它是什么、负责什么、边界在哪：

```cpp
/// 按固定间隔探测每个 agent 的 webUrl，把「运行中 / 已停止」的变化广播出去。
///
/// 只上报状态变化（边沿触发），不做进程嗅探；探测细节见 AgentHealthMonitor.cpp。
class AgentHealthMonitor : public QObject
{
    ...
};
```

类的**详细**说明（设计取舍、与其他模块的协作方式）写在 `.cpp` 实现文件的开头，作为注释块。

**成员函数**——**不写完整 Doxygen 注释**（避免文档变更触发全量构建），只保留简短**普通**注释（`//`），内容等同于 `@brief`；**`get` / `set` 配对函数共用一条注释**：

```cpp
public:
    // 启动该 agent 并开始轮询它的会话 URL；已在运行或 command 为空时直接返回，不报错
    Q_INVOKABLE void launch(const QString &id);

    // agent 的显示名
    QString name() const;
    void setName(const QString &name);
```

参数含义、失败路径、返回值细节全部写在 `.cpp` 的定义处（见 6.3），不在头文件里堆。

**信号**——信号**没有 `.cpp` 实现**，是分工的例外：完整的 Doxygen 注释（含 `@brief`、`@param`）写在头文件的信号声明处：

```cpp
Q_SIGNALS:
    /**
     * @brief 启动或停止失败时发射，界面据此在卡片上闪红并弹出原因
     * @param id 出错的 agent id
     * @param message 可直接展示给用户的失败原因（英文 `tr()` 源串）
     */
    void launchFailed(const QString &id, const QString &message);
```

**枚举**——枚举类型上方写一条 `///`，每个值用行尾 `///<` 说明它代表什么：

```cpp
/// 列表模型的 role。名字与顺序是对 QML 的契约，改动前先确认所有页面。
enum Roles {
    IdRole = Qt::UserRole + 1,  ///< agent 的 id（agents.json 中的键）
    NameRole,                   ///< 显示名
    RunningRole,                ///< 健康检查判定为运行中
    ...
};
Q_ENUM(Roles)
```

**成员变量**——注释用行尾 `///<`；同一组同类成员也可以在组上方共用一条，不必逐个重复：

```cpp
private:
    QTimer m_timer;                 ///< 探测定时器
    QHash<QString, qint64> m_pids;  ///< 本次会话中由本启动器启动过的进程，键为 agent id
```

**header-only 实体**——结构体、模板、内联函数没有 `.cpp` 定义处，完整注释（简略 + 详细 + 标签）就写在头文件里：

```cpp
/// 可失败同步操作的结果：跨模块边界不抛异常，失败以 { ok = false, error = ... } 传递。
///
/// Q_GADGET 让 QML 能直接读 Q_INVOKABLE 返回值的 .ok 与 .error。
struct OpResult
{
    ...
};
```

### 6.3 .cpp：所有函数实现前写完整 Doxygen 注释

**所有函数实现前**都写完整的 Doxygen 注释：`/** ... */` 格式，**注释前空一行**与上一段代码分隔，使用 `@brief`、`@param`、`@return`、`@sa` 等标签，说明用中文：

```cpp

/**
 * @brief 从 agent 的启动输出里挑出会话 URL
 *
 * 带 token 门禁的 harness（dsh 一类）把每进程随机的带 token URL 打到 stdout，
 * 而不是写 token 文件，所以这里扫日志内容而不是读文件。只取第一条指向
 * 同一服务器（协议 + 主机 + 端口相同）的 URL，避免把文档链接误当会话入口。
 *
 * @param output 启动输出（已做 token 脱敏）
 * @param webUrl agent 配置的 web 地址，用于比对服务器
 * @return 找到的会话 URL；没有匹配时返回空字符串
 * @sa finalUrl
 */
QString AgentUrls::sessionUrlFromOutput(const QString &output, const QString &webUrl)
{
    ...
}
```

规则：

- `@brief` 一句话说清"做什么"；设计动机、约束、踩过的坑写在后面的正文段落，不要塞进 `@brief` 一行里。
- 函数再简单也要有完整的注释块——纯转发、单行 getter 的实现也写（`@brief` 一行即可）：既保证文档工具对每个实现都能提取到内容，也保证风格一致。
- `@param` 该写就写，不要因为参数名看起来自解释就一律跳过：取值范围、单位、为空或 `nullptr` 时的行为、所有权、失败语义都要交代。只有签名已经把话说尽（如 `const QString &id`，且没有任何附加约定）时才省略。
- `@return` 写清楚返回值的语义：空串代表什么、`-1` 代表什么、容器的顺序、失败时返回什么。"成功返回 true" 这种同义反复除外。
- `@note` 写使用上的注意事项，`@warning` 写会产生后果的约束，`@sa`（或 `@see`）指向相关函数——配对的 getter / setter 互相 `@sa`，`@deprecated` 注明替代品。没有这些东西就不写标签，别为了凑格式写空标签。
- 匿名 namespace 里的辅助函数、文件级常量只在 `.cpp` 里存在，没有声明处，同样在定义上方写完整注释。

### 6.4 .cpp：软注释

软注释是普通 `//`，解释"为什么"，不解释"是什么"。判断标准：如果注释可以从下一行代码直接读出来，就删掉。

```cpp
// QStandardPaths 的测试模式只重定向 App* 位置，不动 HomeLocation，
// 因此单元测试会写到开发者真实的配置目录。测试模式下必须换用被重定向的位置。
if (QStandardPaths::isTestModeEnabled()) {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}
```

这类注释的价值在于它记录的是**当时的推理与代价**：

- 为什么不能用更直觉的写法（上面的例子：为什么不能直接用 `HomeLocation`）；
- 某个看似多余的判断挡住了什么（并发、重入、越界、非 ASCII 路径）；
- "不这样写会发生什么"——尤其是曾经真的发生过的 bug（例如卡片可见性计数为什么不能读 `Item.visible`）。

一段实现流程较长的代码，用空行 + 简短 `//` 分节，帮助读者跳读；不要给每一行都加注释。

### 6.5 语言与语气

- 中文，用中文标点；句末不加句号也可以，但同一文件里保持一致。
- 说人话，不用"我们"、不用客套话、不用"注意！！！"。要强调就写清楚后果。
- 数字、单位、类型名保持原样：`60 s`、`QHash<QString, qint64>`、`QQuickWebEngineProfile`。
- 不写"待优化"、"暂时这样"这类没有结论的话；确实要留待办就写 `@todo` 并说清条件与替代方案。
- 不写死人的名字与外部链接（会失效）；要引用背景材料就指向仓库内的文档，例如 `docs/research/webengine-embedding.md`。

### 6.6 为什么这样分工

- **重建代价**：头文件被大量翻译单元 include，完整文档写在头文件里意味着任何一次文档修订都触发依赖者全量重编；完整文档放 `.cpp`，改文档只重编那一个翻译单元。这就是"头文件成员函数不写完整 Doxygen 注释"的原因。
- **例外必须完整**：信号、枚举、成员变量、header-only 类型没有 `.cpp` 定义处，它们的完整注释只能写在头文件里——否则这些信息不会出现在任何生成的文档中。
- **头文件的普通注释只服务读代码的人**：它不被 Doxygen 提取，因此只写"扫一眼知道这个函数干什么"的那一句（等同 `@brief`），其余内容全部留给 `.cpp`。两处内容有重叠时，以 `.cpp` 的完整注释为准，并按 6.1 第 4 条保持同步。

### 6.7 反例

```cpp
// 错：复述代码，零信息
// 设置 agent 的名称
def.setName(name);

// 错：没有结论的注释
// 这里可能有问题，先这样
return m_cache.value(id);

// 错：把实现细节写进头文件，头文件应该只留简略说明
// 先查 QHash，未命中再扫目录，扫的时候跳过 .tmp 后缀……
QVariantMap load(const QString &id);

// 错：头文件的成员函数写完整 Doxygen 注释——文档一改，所有依赖该头的翻译单元全部重编
/**
 * @brief 启动该 agent 并开始轮询它的会话 URL
 * @param id agent 的 id
 */
Q_INVOKABLE void launch(const QString &id);

// 错：.cpp 的函数实现只写普通注释，文档工具什么都提取不到
// 启动 agent 并轮询会话 URL
void AgentsFacade::launch(const QString &id) { ... }

// 错：信号没有 .cpp 实现，头文件里又不写注释——它在任何文档里都不会出现
Q_SIGNALS:
    void launchFailed(const QString &id, const QString &message);
```

## 7. 测试规范

测试的注册方式、"用例必须写在 `private Q_SLOTS:` 里"、不依赖网络与真实数据目录等硬性约定见 `AGENTS.md` 的「测试」一节。风格层面的要求：

- 一个 `QObject` 派生类对应一个被测单元，用例名 `testXxx` 描述**被验证的行为**，不是描述实现（`testTildeExpansion`，不是 `testExpand2`）。
- 断言用 `QCOMPARE` / `QVERIFY2`；`QVERIFY2` 的第二参数给出人能读的原因（`qPrintable(actual)`）。
- 用 `QSignalSpy` 验证信号，包括"不该发射时没有发射"。
- 每个用例自带数据准备与清理，不依赖执行顺序；需要固定目录时用 `Paths::setDataRootForTesting()` 注入 `QTemporaryDir`。
- 用例里有非显然的边界时写软注释说明**为什么这个边界重要**（同上：`tst_envexpander.cpp` 的注释风格）。

## 8. 提交前自检

- [ ] `bash scripts/build.sh --test` 全绿（8 个测试目标 + `check_architecture`）。
- [ ] 新增/移动的 `.qml` 已同步 `app/CMakeLists.txt` 的区域清单与（含 `qsTr()` 时）`cmake/AwbTranslations.cmake` 的 `AWB_TS_SOURCES`。
- [ ] QML 用到的 C++ 方法/属性可调用（`Q_INVOKABLE` / `WRITE`）。
- [ ] 新代码一律大写 Qt 宏（`Q_SIGNALS` / `Q_SLOTS` / `Q_EMIT`），`QObject` 派生类带 `Q_OBJECT`；信号与槽的参数类型和传递形式逐字匹配。
- [ ] 单语句 `if` / `for` / `while` 都带花括号；非 const Qt 容器的范围迭代经 `std::as_const()` 或容器已声明为 `const`。
- [ ] 新增的面板/页面在深浅两套主题下都读过一遍，没有字面颜色。
- [ ] 改动的类与函数补齐了符合第 6 节分工的注释（头文件简短普通注释、`.cpp` 完整 `/** ... */` Doxygen 注释）；没有引入新的"复述代码"式注释。
- [ ] `tr()` / `qsTr()` 里没有非 ASCII 字符；注释、标识符、日志、提交信息各自使用规定的语言。
- [ ] 没有留下 `qDebug()`、临时代码、被注释掉的旧实现。
