#ifndef AWB_CORE_PLUGINHOST_H
#define AWB_CORE_PLUGINHOST_H

#include "plugin_api/PluginApi.h"

#include <QList>
#include <QObject>
#include <QString>

class QLibrary;

namespace awb::core {

/// 插件的发现与加载。
///
/// 扫描 <dataRoot>/plugins/*/plugin.json，校验 API 版本，经两个导出的
/// C 符号加载入口动态库。安全规则：坏 manifest、版本不匹配或加载失败
/// 一律记日志后跳过——插件永远不能阻止应用启动。一切跑在宿主进程里、
/// 没有沙箱，因此插件默认禁用、由用户逐个开启。
class PluginHost : public QObject
{
    Q_OBJECT

public:
    /// 已发现（仅 manifest）的插件，设置页列表用。
    struct Manifest
    {
        QString id;
        QString name;
        QString version;
        int apiVersion = 0;
        QString description;
        QString author;
        QString entry;  ///< 插件目录内动态库的文件名
        QString dir;    ///< <dataRoot>/plugins/<id>
        QList<plugin::PageDescriptor> pages;
        bool enabled = false;  ///< 加载时对照设置解析
    };

    explicit PluginHost(QObject *parent = nullptr);

    // 扫描插件目录收集 manifest（不加载任何库）
    QList<Manifest> discover() const;

    // 加载全部已启用插件。services 是宿主侧桥（在 awb_workbench 实现）；
    // 失败只记日志、从不抛异常。
    int loadEnabled(const QList<Manifest> &manifests,
                    plugin::Services *services);

    // 库要保持进程级存活，这里统一卸载
    void shutdown();

private:
    QList<QLibrary *> m_loaded;  ///< 已加载的库，shutdown() 统一卸载
};

} // namespace awb::core

#endif // AWB_CORE_PLUGINHOST_H
