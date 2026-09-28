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

void WebEngineCompat::denyFeature(QQuickWebEngineView *view,
                                  const QUrl &securityOrigin, int feature)
{
    if (!view || !securityOrigin.isValid())
        return;
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

QUrl WebEngineCompat::devToolsUrl(QQuickWebEngineView *view)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return view ? view->devToolsUrl() : QUrl();
#else
    Q_UNUSED(view);
    return QUrl();
#endif
}

void WebEngineCompat::attachDevTools(QQuickWebEngineView *view,
                                     QQuickWebEngineView *devToolsView)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 5 的检查方向与 Qt 6 相反：给 devTools 视图设 inspectedView，
    // 它自己随后加载 chrome-devtools:// 前端。
    if (view && devToolsView)
        devToolsView->setInspectedView(view);
#else
    Q_UNUSED(view);
    Q_UNUSED(devToolsView);
#endif
}

} // namespace awb::web
