#include "web/webengine/WebEngineCompat.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QQuickWebEngineDownloadRequest>
// QQuickWebEngineView 在 Qt 6 里同样是私有 API（公共包含目录只提供
// Profile/Script/DownloadRequest），与 Qt 5 一样经 webengine 目标的
// *_PRIVATE_INCLUDE_DIRS 引入；newWindowRequested 信号与请求类型的声明
// 就在这些私有头里。
#include <QtWebEngineQuick/private/qquickwebenginenewwindowrequest_p.h>
#include <QtWebEngineQuick/private/qquickwebengineview_p.h>
#else
// Qt 5：QQuickWebEngineView 与 QQuickWebEngineDownloadItem 都是私有 API
// （公共包含目录里只有 Profile/Script），经 webengine 目标的
// Qt5WebEngine_PRIVATE_INCLUDE_DIRS 引入。
#include <QtWebEngine/private/qquickwebenginedownloaditem_p.h>
#include <QtWebEngine/private/qquickwebenginenewviewrequest_p.h>
#include <QtWebEngine/private/qquickwebengineview_p.h>
#endif

namespace awb::web {
namespace {

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using DownloadItem = QQuickWebEngineDownloadRequest;
#else
using DownloadItem = QQuickWebEngineDownloadItem;
#endif

// DownloadState 的取值两版一致（Requested=0, InProgress=1, Completed=2,
// Cancelled=3, Interrupted=4）；两个类型的枚举同值，静态断言兜底防漂移。
static_assert(int(DownloadItem::DownloadCompleted) == 2, "DownloadCompleted moved");
static_assert(int(DownloadItem::DownloadCancelled) == 3, "DownloadCancelled moved");
static_assert(int(DownloadItem::DownloadInterrupted) == 4, "DownloadInterrupted moved");

} // namespace

WebEngineCompat::WebEngineCompat(QObject *parent)
    : QObject(parent)
{
}

int WebEngineCompat::downloadCompleted() const
{
    return int(DownloadItem::DownloadCompleted);
}

int WebEngineCompat::downloadCancelled() const
{
    return int(DownloadItem::DownloadCancelled);
}

int WebEngineCompat::downloadInterrupted() const
{
    return int(DownloadItem::DownloadInterrupted);
}

/**
 * 给 view 挂弹窗转发：Qt 6 连 newWindowRequested 并在转发前置 accepted
 * （不置则该次弹窗请求被引擎拒绝）；Qt 5 连 newViewRequested，不调
 * openIn 即为丢弃（qquickwebengineview.cpp 里未被认领的请求返回
 * nullptr，页面侧拿到空的新建窗口）。两版的 URL 去向都由本应用决定，
 * 绝不让引擎自己开窗。
 *
 * 请求对象两版的属性形状不同（Qt 6 才谈得上 accepted），故 Qt 6 分支经
 * 元对象读写（setProperty/property），只依赖 QML 文档稳定暴露的名字
 * （accepted / requestedUrl）；Qt 5 的 requestedUrl() 直接调（本机源码
 * 可查证）。
 *
 * 幂等：重复传入同一个 view 只连接一次——一次弹窗开两个标签的错误很
 * 难查。用动态属性做标记，随视图销毁自动消失，不留悬垂指针；
 * Component.onCompleted 每个实例只触发一次，防的是将来的其它调用点。
 */
void WebEngineCompat::watchPopups(QQuickWebEngineView *view)
{
    if (!view || view->property("_awbPopupsWatched").toBool())
        return;
    view->setProperty("_awbPopupsWatched", true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(view, &QQuickWebEngineView::newWindowRequested,
            this, [this, view](QQuickWebEngineNewWindowRequest *request) {
        if (!request)
            return;
        // Qt 6 的应答入口随小版本演进（6.7 文档为 accepted 属性，6.11 的
        // 属性表里只剩 openIn/acceptAsNewWindow）：置位不存在的属性只会
        // 退化为忽略（弹窗被引擎拒绝，URL 仍由本应用接管路由，用户可见
        // 行为一致），因此这里不必按小版本分支。
        request->setProperty("accepted", true);
        Q_EMIT popupRequested(view, request->property("requestedUrl").toUrl());
    });
#else
    connect(view, &QQuickWebEngineView::newViewRequested,
            this, [this, view](QQuickWebEngineNewViewRequest *request) {
        if (!request)
            return;
        Q_EMIT popupRequested(view, request->requestedUrl());
    });
#endif
}

/**
 * 拒绝一次页面权限请求（v1 全拒）。grantFeaturePermission(..., false) 在
 * 两个大版本上同名同义（Qt 6 文档亦以此示例），无需分支；曾用过的
 * rejectFeature 名字并不存在于任何一版的 API 表里。
 */
void WebEngineCompat::denyFeature(QQuickWebEngineView *view,
                                  const QUrl &securityOrigin, int feature)
{
    if (!view || !securityOrigin.isValid())
        return;
    view->grantFeaturePermission(
        securityOrigin,
        static_cast<QQuickWebEngineView::Feature>(feature), false);
}

/**
 * 恒为空 URL：DevTools 不经地址加载。两版 QML 属性表里 devToolsView 与
 * inspectedView 成对出现（没有可读的 devToolsUrl），挂接方式见
 * attachDevTools。
 */
QUrl WebEngineCompat::devToolsUrl(QQuickWebEngineView *view)
{
    Q_UNUSED(view);
    return QUrl();
}

/**
 * 把 devToolsView 设为 view 的检查器：设 inspectedView 后检查器视图自行
 * 加载前端，两版同构。
 */
void WebEngineCompat::attachDevTools(QQuickWebEngineView *view,
                                     QQuickWebEngineView *devToolsView)
{
    if (view && devToolsView)
        devToolsView->setInspectedView(view);
}

} // namespace awb::web
