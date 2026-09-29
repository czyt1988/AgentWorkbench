#include "web/webengine/WebEngineCompat.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QQuickWebEngineDownloadRequest>
#include <QQuickWebEngineView>
#else
// Qt 5：QQuickWebEngineView 与 QQuickWebEngineDownloadItem 都是私有 API
// （公共包含目录里只有 Profile/Script），经 webengine 目标的
// Qt5WebEngine_PRIVATE_INCLUDE_DIRS 引入。
#include <QtWebEngine/private/qquickwebengineview_p.h>
#include <QtWebEngine/private/qquickwebenginedownloaditem_p.h>
#endif

namespace awb::web {
namespace {

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
// 下载条目的版本中立别名：类名两版不同，QML 无法条件引用类型，
// 下载状态因此经下面的取值函数暴露。
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

/**
 * @brief 构造兼容层
 *
 * @param parent QObject 父项
 */
WebEngineCompat::WebEngineCompat(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 取「下载完成」状态的版本中立取值
 *
 * QML 拿到的是 int：两版下载条目类型名不同（WebEngineDownloadRequest /
 * WebEngineDownloadItem），QML 里无法写枚举类型名，只能比值。
 *
 * @return DownloadState::DownloadCompleted 的 int 值（两版均为 2）
 */
int WebEngineCompat::downloadCompleted() const
{
    return int(DownloadItem::DownloadCompleted);
}

/**
 * @brief 取「下载取消」状态的版本中立取值
 *
 * @return DownloadState::DownloadCancelled 的 int 值（两版均为 3）
 */
int WebEngineCompat::downloadCancelled() const
{
    return int(DownloadItem::DownloadCancelled);
}

/**
 * @brief 取「下载中断」状态的版本中立取值
 *
 * @return DownloadState::DownloadInterrupted 的 int 值（两版均为 4）
 */
int WebEngineCompat::downloadInterrupted() const
{
    return int(DownloadItem::DownloadInterrupted);
}

/**
 * @brief 拒绝一次页面权限请求（v1 策略：全部拒绝）
 *
 * 两版拒绝的写法不同：Qt 6 是 view.rejectFeature(feature)，Qt 5 是
 * view.grantFeaturePermission(origin, feature, false)——QML 无法对
 * 不存在的成员做条件引用，差异因此收在这条 #if 里。
 *
 * @param view 权限请求所属的视图；为空时静默返回
 * @param securityOrigin 请求来源；无效时静默返回
 * @param feature QQuickWebEngineView::Feature 的 int 形式（QML 枚举传入）
 */
void WebEngineCompat::denyFeature(QQuickWebEngineView *view,
                                  const QUrl &securityOrigin, int feature)
{
    if (!view || !securityOrigin.isValid()) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    view->rejectFeature(
        static_cast<QQuickWebEngineView::Feature>(feature));
#else
    // Qt 5：Feature 是普通枚举，拒绝 = grantFeaturePermission(..., false)。
    view->grantFeaturePermission(
        securityOrigin,
        static_cast<QQuickWebEngineView::Feature>(feature), false);
#endif
}

/**
 * @brief 取 view 的 DevTools 前端地址
 *
 * Qt 6 直接读 view->devToolsUrl() 喂给独立窗口；Qt 5 没有这条路径——
 * 那边由 attachDevTools() 以「检查器视图绑 inspectedView」的方式挂接，
 * 地址不参与，恒返回空 URL。
 *
 * @param view 被检查的视图；可为空（Qt 6 下返回空 URL）
 * @return Qt 6 的 DevTools 地址；Qt 5 恒为空 URL
 * @sa attachDevTools
 */
QUrl WebEngineCompat::devToolsUrl(QQuickWebEngineView *view)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return view ? view->devToolsUrl() : QUrl();
#else
    Q_UNUSED(view);
    return QUrl();
#endif
}

/**
 * @brief 把 devToolsView 挂成 view 的检查器视图
 *
 * Qt 5 的检查方向与 Qt 6 相反：不是给主视图一个地址，而是给检查器视图
 * 设 inspectedView，它随后自己加载 chrome-devtools:// 前端。Qt 6 下
 * 无操作（devToolsUrl + url 绑定已覆盖），两个参数都不使用。
 *
 * @param view 被检查的主视图；为空时不挂接
 * @param devToolsView 检查器视图；为空时不挂接
 * @sa devToolsUrl
 */
void WebEngineCompat::attachDevTools(QQuickWebEngineView *view,
                                     QQuickWebEngineView *devToolsView)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 5 的检查方向与 Qt 6 相反：给 devTools 视图设 inspectedView，
    // 它自己随后加载 chrome-devtools:// 前端。
    if (view && devToolsView) {
        devToolsView->setInspectedView(view);
    }
#else
    Q_UNUSED(view);
    Q_UNUSED(devToolsView);
#endif
}

} // namespace awb::web
