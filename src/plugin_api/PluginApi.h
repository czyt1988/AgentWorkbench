#ifndef AWB_PLUGIN_API_PLUGINAPI_H
#define AWB_PLUGIN_API_PLUGINAPI_H

#include <QString>
#include <QStringList>

/// 插件 ABI。规则：
///  - 只有 Qt 类型跨过这条边界——绝不出现宿主的 C++ 类；
///  - 每个插件导出下方两个 extern "C" 符号；
///  - `apiVersion` 必须与 ApiVersion 一致，否则宿主拒绝加载。
///
/// 插件跑在宿主进程里（信任级与应用本身相同——没有沙箱；
/// 用户在设置里显式启用）。

#if defined(WIN32) || defined(_WIN32)
#define AWB_PLUGIN_EXPORT __declspec(dllexport)
#else
#define AWB_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace awb::plugin {

/// 本头文件描述的 ABI 版本。任何破坏性变更都要递增。
constexpr int ApiVersion = 1;

/// 插件贡献的一个侧栏页面。只用 Qt 值类型——宿主类不跨边界。
struct PageDescriptor
{
    QString id;
    QString title;
    QString icon;    ///< qrc:/… URL（插件自己的资源）
    QString source;  ///< qrc:/… QML URL
    QString section; ///< "main" | "extensions" | "system"
    int order = 50;
};

/// 注册时递给插件的宿主服务。纯虚：宿主实现、插件调用。
/// 内置页面与插件走同一条注册路径——没有单独的"插件模式"。
class Services
{
public:
    virtual ~Services() = default;

    // 注册页面；重复 id 被宿主拒绝（记日志）。注销按 id。
    virtual void registerPage(const PageDescriptor &page) = 0;
    virtual void unregisterPage(const QString &id) = 0;

    // 注册一种额外的 Web 表面（组件 QML URL）
    virtual void addWebSurface(const QString &kind,
                               const QString &componentUrl) = 0;

    // 该插件私有的可写目录（<dataRoot>/plugins/<pluginId>/data）
    virtual QString dataDir(const QString &pluginId) = 0;

    // level：0 = info、1 = warning、2 = error
    virtual void log(int level, const QString &message) = 0;

    // 弹一条 toast（级别含义与 log 相同）
    virtual void notify(int level, const QString &title,
                        const QString &text) = 0;

    // 只读主题访问：令牌对应的 "#rrggbb"，未知令牌返回空串
    virtual QString themeColor(const QString &token) = 0;

    // 只读设置访问（键缺失返回空串）
    virtual QString settingsValue(const QString &key) = 0;
};

} // namespace awb::plugin

// --- C 入口 ----------------------------------------------------------
extern "C" {
/// 返回插件编译时使用的 API 版本。
AWB_PLUGIN_EXPORT int awb_plugin_api_version();
/// 注册入口。返回 0 表示成功；其它值让宿主记日志并忽略该插件。
AWB_PLUGIN_EXPORT int awb_plugin_register(awb::plugin::Services *services);
}

#endif // AWB_PLUGIN_API_PLUGINAPI_H
