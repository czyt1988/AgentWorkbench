# 插件

AgentWorkbench 可以用插件扩展。本页说明什么是插件、如何启用，以及如何编写插件。

!!! warning "信任级别"
    插件运行在**应用进程内部**——没有沙箱。插件能做应用本身能做的一切。
    只启用你信任的插件。这与内嵌 Web 视图的信任模型一致（参见
    [WebEngine 内嵌调研](research/webengine-embedding.md)）。

## 启用插件

插件**默认全部禁用**。使用步骤：

1. 打开 **设置 → 插件**。
2. 打开 **启用插件（实验性）** 总开关。
3. 单独启用你要的插件。
4. 重启 AgentWorkbench——插件动态库只在启动时加载一次。

设置页会列出插件目录（`<数据目录>/plugins/`）下发现的每个插件，即使处于
禁用状态——读取清单永远不会加载代码。

## 插件的结构

```
<dataRoot>/plugins/
  notes/
    plugin.json     清单（id、name、version、apiVersion、entry、pages）
    notes.dll       编译后的插件
```

`plugin.json`：

```json
{
  "id": "notes",
  "name": "Notes",
  "version": "0.1.0",
  "apiVersion": 1,
  "description": "一段简短说明，会显示在设置页。",
  "author": "you",
  "entry": "notes.dll",
  "pages": [
    { "id": "notes", "title": "Notes",
      "icon": "qrc:/icons/bot.svg",
      "source": "qrc:/notes/NotesPage.qml",
      "section": "extensions", "order": 50 }
  ]
}
```

- `apiVersion` 必须与宿主的插件 API 版本一致（0.4.0 为 `1`）；不一致会记
  警告并拒绝加载。
- `pages` 通过**与内置页面完全相同的注册路径**登记——插件页面就是侧边栏
  `extensions` 分组里的普通条目。

## 编写插件

插件是一个包含 `src/plugin_api/PluginApi.h` 并导出两个 C 符号的动态库：

```cpp
#include "plugin_api/PluginApi.h"

extern "C" {

AWB_PLUGIN_EXPORT int awb_plugin_api_version()
{
    return awb::plugin::ApiVersion;
}

AWB_PLUGIN_EXPORT int awb_plugin_register(awb::plugin::Services *services)
{
    awb::plugin::PageDescriptor page;
    page.id = "notes";
    page.title = "Notes";
    page.source = "qrc:/notes/NotesPage.qml";
    page.section = "extensions";
    services->registerPage(page);
    return 0; // 非 0 = 宿主记日志并忽略该插件
}

} // extern "C"
```

`Services` 接口是插件触达宿主的唯一通道：

| 服务 | 用途 |
| --- | --- |
| `registerPage` / `unregisterPage` | 侧边栏页面（与内置页面同一路径） |
| `addWebSurface` | 贡献额外的 Web 表面类型 |
| `dataDir` | 插件私有的可写目录 |
| `log` / `notify` | 应用日志与 toast 通知 |
| `themeColor` | 只读的主题令牌访问（`#rrggbb`） |
| `settingsValue` | 只读访问一小部分设置键 |

ABI 规则：

- 只有 Qt 类型跨边界——绝不传宿主 C++ 类；
- 清单损坏、版本不符或加载失败**只记日志并跳过**——插件永远不能阻止应用
  启动；
- 插件不能写 `agents.json` 或设置文件；主题与设置访问均为只读。

## 打包插件

把插件编译为动态库，将 `plugin.json` 与编译出的 `dll` 放进
`<数据目录>/plugins/` 下的同名文件夹，然后在 设置 → 插件 中启用并重启
——页面的侧边栏 extensions 分组就会出现。

## 开发提示

- 页面的 QML 作为**插件侧资源**（`qrc:/…`）打包；共享库中的资源在库加载
  时注册。
- 页面 QML 可以 `import AgentWorkbench.App` 使用共享令牌与全局对象
  （`theme`、`workbench`、`ui` 等），与内置页面完全一样。
- API 带版本号：对 `Services` 或 `PageDescriptor` 的任何破坏性修改都要递增
  `awb::plugin::ApiVersion`。
