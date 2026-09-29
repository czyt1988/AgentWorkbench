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
/// 引用（写进文件即加载失败——onNewWindowRequested 这个 Qt 6 信号名就是
/// 这么在 Qt 5 上把整个内嵌表面打成空白页的），而下面这几个成员恰好名字
/// 或形状随大版本变化：
/// - 弹窗信号：Qt 6 newWindowRequested / Qt 5 newViewRequested，且请求对象
///   的应答方式不同（Qt 6 置 accepted 属性，Qt 5 不调 openIn 即丢弃），
///   经 watchPopups/popupRequested 桥接；
/// - 下载条目类型名 WebEngineDownloadRequest（Qt 6）/ WebEngineDownloadItem
///   （Qt 5），其 DownloadState 枚举两版同值（见 .cpp）；
/// - 权限拒绝与 DevTools 挂接：grantFeaturePermission(..., false) 与
///   inspectedView 绑定两版同名同构，直接使用。
///
/// 其余 QML 可见的请求对象两版形状一致，不需要桥：全屏请求（toggleOn +
/// accept() 的 gadget）、LifecycleState 作用域枚举（Qt 5.10+ 即支持）。
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

    // 转发 view 的弹窗请求为 popupRequested（幂等，可安全重复调用）
    Q_INVOKABLE void watchPopups(QQuickWebEngineView *view);

    // 拒绝一次页面权限请求（v1 全拒）；view 或来源无效时静默返回
    Q_INVOKABLE void denyFeature(QQuickWebEngineView *view,
                                 const QUrl &securityOrigin, int feature);

    // 恒为空 URL：DevTools 由 attachDevTools 以「检查器视图绑 inspectedView」
    // 的方式挂接，检查器前端自行加载，地址不参与
    Q_INVOKABLE QUrl devToolsUrl(QQuickWebEngineView *view);

    // 把 devToolsView 设为 view 的检查器（inspectedView 绑定，两版同构）
    Q_INVOKABLE void attachDevTools(QQuickWebEngineView *view,
                                    QQuickWebEngineView *devToolsView);

Q_SIGNALS:
    /**
     * @brief view 内页面请求开新窗口（target=_blank / window.open）时发射
     *
     * sourceView 是发起的视图——每个标签页一个表面实例，QML 据此认领；
     * 请求已在 C++ 侧应答（Qt 6 置 accepted，Qt 5 不调 openIn 即丢弃），
     * QML 只需决定 URL 的去向。
     *
     * @param sourceView 发起请求的 WebEngineView
     * @param target      请求要打开的 URL
     */
    void popupRequested(QObject *sourceView, const QUrl &target);
};

} // namespace awb::web

#endif // AWB_WEBENGINE_WEBENGINECOMPAT_H
