#ifndef AWB_WEBENGINE_WEBENGINECOMPAT_H
#define AWB_WEBENGINE_WEBENGINECOMPAT_H

#include <QObject>
#include <QUrl>

class QQuickWebEngineView;

namespace awb::web {

/// WebEngine 的 Qt 5/Qt 6 成员差异桥，在 main.cpp 经 AgentWorkbench.App
/// 注册为 QML 单例 WebEngineCompat；WebEngineSurface.qml 只引用它提供的
/// 版本中立名字，其余 WebEngine API 两版同名、直接使用。
///
/// 之所以要把这几处挪进 C++：QML 无法对「不存在的类型/属性」做静态条件
/// 引用（写进文件即加载失败），而下面这几个成员恰好名字或枚举形态随大版本
/// 变化：
/// - 下载条目类型名 WebEngineDownloadRequest（Qt 6）/ WebEngineDownloadItem
///   （Qt 5），其 DownloadState 枚举两版同值（见 .cpp）；
/// - 权限拒绝：Qt 6 的 view.rejectFeature(feature) 与 Qt 5 的
///   view.grantFeaturePermission(origin, feature, false)；
/// - DevTools：Qt 6 读 view.devToolsUrl 喂给独立窗口，Qt 5 给检查器视图
///   绑 inspectedView。
///
/// LifecycleState 的作用域枚举（WebEngineView.LifecycleState.Active）在
/// Qt 5.10+ 即受支持（enum class + Q_ENUM），不需要桥。
class WebEngineCompat : public QObject
{
    Q_OBJECT

    /// 下载完成态（DownloadState::DownloadCompleted 的版本中立取值）。
    Q_PROPERTY(int downloadCompleted READ downloadCompleted CONSTANT)
    /// 下载取消态。
    Q_PROPERTY(int downloadCancelled READ downloadCancelled CONSTANT)
    /// 下载中断态。
    Q_PROPERTY(int downloadInterrupted READ downloadInterrupted CONSTANT)

public:
    explicit WebEngineCompat(QObject *parent = nullptr);

    int downloadCompleted() const;
    int downloadCancelled() const;
    int downloadInterrupted() const;

    /// 拒绝一次页面权限请求（v1 全拒）。view 或来源无效时静默返回。
    Q_INVOKABLE void denyFeature(QQuickWebEngineView *view,
                                 const QUrl &securityOrigin, int feature);

    /// 取 view 的 DevTools 地址。Qt 5 恒为空 URL：那边由 attachDevTools
    /// 以「检查器视图绑 inspectedView」的方式挂接，地址不参与。
    Q_INVOKABLE QUrl devToolsUrl(QQuickWebEngineView *view);

    /// 把 devToolsView 设为 view 的检查器（Qt 5 的 inspectedView 绑定）。
    /// Qt 6 无操作——devToolsUrl + url 绑定已覆盖。
    Q_INVOKABLE void attachDevTools(QQuickWebEngineView *view,
                                    QQuickWebEngineView *devToolsView);
};

} // namespace awb::web

#endif // AWB_WEBENGINE_WEBENGINECOMPAT_H
