#include "web/webengine/WebEngineCompat.h"

#include <QDebug>
#include <QFile>

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
// Qt5WebEngine_PRIVATE_INCLUDE_DIRS 引入。QQuickWebEngineScript 是公共
// API（Qt 5 的 polyfill 注入走 view 级 userScripts，见 installCompatScript）。
#include <QQuickWebEngineScript>
#include <QQmlListProperty>
#include <QtWebEngine/private/qquickwebenginedownloaditem_p.h>
#include <QtWebEngine/private/qquickwebenginenewviewrequest_p.h>
#include <QtWebEngine/private/qquickwebengineview_p.h>
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
 * @brief 给 view 挂弹窗转发
 *
 * Qt 6 连 newWindowRequested 并在转发前置 accepted（不置则该次弹窗请求
 * 被引擎拒绝）；Qt 5 连 newViewRequested，不调 openIn 即为丢弃
 * （qquickwebengineview.cpp 里未被认领的请求返回 nullptr，页面侧拿到
 * 空的新建窗口）。两版的 URL 去向都由本应用决定，绝不让引擎自己开窗。
 *
 * 请求对象两版的属性形状不同（Qt 6 才谈得上 accepted），故 Qt 6 分支经
 * 元对象读写（setProperty/property），只依赖 QML 文档稳定暴露的名字
 * （accepted / requestedUrl）；Qt 5 的 requestedUrl() 直接调（本机源码
 * 可查证）。
 *
 * 幂等：重复传入同一个 view 只连接一次——一次弹窗开两个标签的错误很
 * 难查。用动态属性做标记，随视图销毁自动消失，不留悬垂指针；
 * Component.onCompleted 每个实例只触发一次，防的是将来的其它调用点。
 *
 * @param view 要监视的视图；为空时不动作
 */
void WebEngineCompat::watchPopups(QQuickWebEngineView *view)
{
    if (!view || view->property("_awbPopupsWatched").toBool()) {
        return;
    }
    view->setProperty("_awbPopupsWatched", true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(view, &QQuickWebEngineView::newWindowRequested,
            this, [this, view](QQuickWebEngineNewWindowRequest *request) {
        if (!request) {
            return;
        }
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
        if (!request) {
            return;
        }
        Q_EMIT popupRequested(view, request->requestedUrl());
    });
#endif
}

/**
 * @brief 拒绝一次页面权限请求（v1 策略：全部拒绝）
 *
 * grantFeaturePermission(..., false) 在两个大版本上同名同义（Qt 6 文档
 * 亦以此示例），无需分支；曾用过的 rejectFeature 名字并不存在于任何
 * 一版的 API 表里。
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
    view->grantFeaturePermission(
        securityOrigin,
        static_cast<QQuickWebEngineView::Feature>(feature), false);
}

/**
 * @brief 取 view 的 DevTools 前端地址
 *
 * 恒为空 URL：DevTools 不经地址加载。两版 QML 属性表里 devToolsView 与
 * inspectedView 成对出现（没有可读的 devToolsUrl），挂接方式见
 * attachDevTools。
 *
 * @param view 被检查的视图（未使用）
 * @return 恒为空 URL
 * @sa attachDevTools
 */
QUrl WebEngineCompat::devToolsUrl(QQuickWebEngineView *view)
{
    Q_UNUSED(view);
    return QUrl();
}

/**
 * @brief 把 devToolsView 挂成 view 的检查器视图
 *
 * 设 inspectedView 后检查器视图自行加载前端，两版同构。
 *
 * @param view 被检查的主视图；为空时不挂接
 * @param devToolsView 检查器视图；为空时不挂接
 */
void WebEngineCompat::attachDevTools(QQuickWebEngineView *view,
                                     QQuickWebEngineView *devToolsView)
{
    if (view && devToolsView) {
        devToolsView->setInspectedView(view);
    }
}

/**
 * @brief 给 view 挂旧引擎兼容 polyfill 脚本（Qt 5 的注入路径）
 *
 * Qt 5.15 内嵌的 Chromium 是 87（本机 Src 树 chrome/VERSION 可查证），
 * 现代 agent WebUI 启动即调用 .at / toSorted / Promise.withResolvers 等
 * Chrome 92–120 的 API，缺一个就是整页白屏。polyfill 源码在
 * :/web/compat-polyfills.js（全部带特性检测，新引擎上空转），必须以
 * MainWorld + DocumentCreation 注入——早于页面任何脚本、且页面脚本可见。
 *
 * 两版注入位置不同：Qt 6 的 QQuickWebEngineProfile 继承 core 的
 * QWebEngineProfile、有 scripts() 集合，profile 级注入一次全视图生效
 * （在 WebEngineProfileStore::createProfile 完成，本函数为空操作）；
 * Qt 5 的 Quick profile 只是 QObject 包装、没有 scripts()，只能走
 * view 级 userScripts。调用时机须在 view 的 Component.onCompleted——
 * Qt 5 的适配器初始化（lazyInitialize）经 singleShot(0) 排在完成阶段
 * 之后，此时追加的脚本仍能赶上首次加载（initializationFinished 统一
 * bind，qquickwebengineview.cpp 可查证）。
 *
 * 幂等：动态属性标记，与 watchPopups 同一套路。
 *
 * @param view 要注入的视图；为空时不动作
 */
void WebEngineCompat::installCompatScript(QQuickWebEngineView *view)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    Q_UNUSED(view);
#else
    if (!view || view->property("_awbCompatScriptInstalled").toBool()) {
        return;
    }
    view->setProperty("_awbCompatScriptInstalled", true);

    const QString source = compatScriptSource();
    if (source.isEmpty()) {
        return;
    }
    auto *script = new QQuickWebEngineScript(view);
    script->setName(QStringLiteral("awb-compat-polyfills"));
    script->setSourceCode(source);
    script->setInjectionPoint(QQuickWebEngineScript::DocumentCreation);
    script->setWorldId(QQuickWebEngineScript::MainWorld);
    // Qt 5 的 Quick 脚本类方法是 setRunOnSubframes（小写 f）；Qt 6 core 的
    // QWebEngineScript 才是 setRunsOnSubFrames——见 WebEngineProfileStore。
    script->setRunOnSubframes(true);

    // userScripts 是 QQmlListProperty（私有头里的 REVISION 1 属性）：
    // C++ 侧经函数指针追加，等价于 QML 里 userScripts: WebEngineScript{}
    // 的声明式写法——而后者在 Qt 6 会因 WebEngineScript 不可创建
    // （值类型，qmltypes 里 isCreatable:false）打挂整个表面组件。
    // append 的首参要非 const 指针，故 scripts 不能声明为 const。
    QQmlListProperty<QQuickWebEngineScript> scripts = view->userScripts();
    if (scripts.append) {
        scripts.append(&scripts, script);
    }
#endif
}

/**
 * @brief 取 polyfill 脚本源码（进程内缓存）
 *
 * 资源文件 :/web/compat-polyfills.js 由 app 与 tst_webengine 各自嵌入
 * （资源不进静态库，见 app/CMakeLists.txt 头注释）。读失败时返回空串并
 * 记警告——注入路径会静默跳过，页面回到「旧引擎裸奔」的行为，不至于
 * 让内嵌视图整体失效。
 *
 * @return 脚本源码；资源缺失时为空串
 */
QString WebEngineCompat::compatScriptSource()
{
    static const QString source = [] {
        QFile file(QStringLiteral(":/web/compat-polyfills.js"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning().noquote() << QStringLiteral(
                "WebEngineCompat: cannot read :/web/compat-polyfills.js — "
                "old engines will run pages without the JS polyfills");
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    }();
    return source;
}

} // namespace awb::web
