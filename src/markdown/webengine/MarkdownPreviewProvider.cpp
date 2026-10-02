#include "markdown/webengine/MarkdownPreviewProvider.h"

#include "core/Logging.h"
#include "markdown/MarkdownPreviewController.h"
#include "markdown/MarkdownRuntime.h"
#include "markdown/webengine/MarkdownRuntimeHandler.h"

#include <QQuickWebEngineProfile>
#include <QWebEngineUrlScheme>

namespace awb::markdown {

namespace {

// scheme 表是否在 initialize() 之前真的被 registerScheme() 锁进去了。
// 引擎对「没注册的 scheme 装 handler」只给一条警告就照常接受请求，页面拿到
// 的是 200 的资源但 fetch 全废（Qt 6.7.3 实测：注册失败时 awbmd:// 的文档、
// 脚本、字体照样供得出，只有 fetch() 报 "URL scheme awbmd is not supported"），
// 而构建与纯函数用例仍然全绿。schemeByName() 读的就是引擎那份进程级注册表
// （url::CustomScheme::FindScheme），所以这里主动核对一次，把那种半失效变成
// 一个有日志、且会让 available 落成 false 的状态。
bool schemeIsRegistered()
{
    return QWebEngineUrlScheme::schemeByName(
               QByteArray(MarkdownRuntime::kScheme))
               .name()
        == QByteArray(MarkdownRuntime::kScheme);
}

} // namespace

/**
 * @brief 装好预览表面：建 handler，并按 scheme 是否已注册宣布能力
 *
 * @param controller 能力位的接收方（不拥有）
 * @param parent 父对象
 */
MarkdownPreviewProvider::MarkdownPreviewProvider(
    MarkdownPreviewController *controller, QObject *parent)
    : QObject(parent),
      m_controller(controller)
{
    m_handler = new MarkdownRuntimeHandler(controller, this);
    if (!schemeIsRegistered()) {
        AWB_WARNING << "markdown: the awbmd scheme is not registered, so the"
                       " preview stays unavailable. registerScheme() must run"
                       " before QtWebEngineQuick::initialize()";
        return;
    }
    if (m_controller) {
        m_controller->setSurfaceReady(true);
    }
}

/**
 * @brief 注册 awbmd:// 的语法与权限
 *
 * 必须在 QtWebEngineQuick::initialize() 之前调用，理由见头文件注释。
 *
 * 权限组合的含义：LocalScheme + SecureScheme + LocalAccessAllowed 让页面
 * 是个「本地内容的安全上下文」；CorsEnabled 与 FetchApiAllowed 让页面能
 * fetch 自己的 doc.json（没有后者时 fetch 在自定义 scheme 上直接被拒）；
 * ServiceWorkersAllowed 给将来的 worker 留位。
 *
 * 有意**不设** NoAccessAllowed——它强制 opaque origin，fetch 与 @font-face
 * 一起废掉，表现是「公式没有字形、图表画不出来」；也不设
 * ContentSecurityPolicyIgnored。预览页不需要联网，因此
 * QWebEngineSettings::LocalContentCanAccessRemoteUrls 保持默认关闭，
 * 文档里写的 http(s) 图片天然加载不了，而不是靠代码去拦。
 */
void MarkdownPreviewProvider::registerScheme()
{
    // 先取一个具名 QByteArray：`QWebEngineUrlScheme scheme(QByteArray(name))`
    // 会被 MSVC 按「声明一个接受 QByteArray 参数的函数 scheme」解析（most
    // vexing parse），后面 registerScheme(scheme) 就变成传函数指针。
    const QByteArray schemeName(MarkdownRuntime::kScheme);
    QWebEngineUrlScheme scheme(schemeName);
    // Host 而不是 HostAndPort：URL 里有真实的 host（awbmd://runtime/…），一个
    // origin 供全部资源与文档，同源才有 fetch()。但 HostAndPort 会让
    // registerScheme() 因为「语法含端口段而 defaultPort 还是 PortUnspecified」
    // 直接 return（Qt 6.7.3 只给一条 "needs a default port" 警告），scheme 根本
    // 没进引擎注册表，表现是页面能开、资源能给、fetch 全废——我们的 URL 里
    // 没有端口，语法本身就不该声明端口段。Host 反过来要求端口保持未指定。
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    QWebEngineUrlScheme::Flags flags = QWebEngineUrlScheme::Flag::SecureScheme
        | QWebEngineUrlScheme::Flag::LocalScheme
        | QWebEngineUrlScheme::Flag::LocalAccessAllowed
        | QWebEngineUrlScheme::Flag::CorsEnabled
        | QWebEngineUrlScheme::Flag::ServiceWorkersAllowed;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // 6.5 上这个 flag 还不存在（该构建走「C++ 用 runJavaScript 推文本」的
    // 兜底路径，功能少一点但不会白屏）。差异只落在这一个 #if 里。
    flags |= QWebEngineUrlScheme::Flag::FetchApiAllowed;
#endif
    scheme.setFlags(flags);
    QWebEngineUrlScheme::registerScheme(scheme);
}

/**
 * @brief 取（必要时建）预览专用的 profile
 * @return 恒为同一个实例；本类持有它
 *
 * 与 agent 的 profile 相反，这里刻意要 off-the-record：预览渲染的是本地
 * 文件，没有任何会话要留，而 AGENTS.md「每个 agent 一个持久 profile」那条
 * 硬要求针对的是同 host 不同端口串 cookie——本 profile 上只有 awbmd 一个
 * origin，不存在那类内容。HTTP 缓存同理走内存：随包资源已经在 Qt 资源里，
 * 落盘只是往数据目录丢一份可重建的副本。
 *
 * profile 的公开构造函数用空名字建 adapter，adapter 在构造时就据此判定隐身
 * （见 AGENTS.md 与 WebEngineProfileStore.cpp 的实测记录）。这里要的正是隐身，
 * 但 setOffTheRecord(true) 仍然显式写出来——依赖那条 Qt 缺陷「恰好」得到想要
 * 的行为，下一个人会把它当 bug「修」掉。
 */
QQuickWebEngineProfile *MarkdownPreviewProvider::previewProfile()
{
    if (m_profile) {
        return m_profile;
    }
    auto *profile = new QQuickWebEngineProfile(this);
    profile->setStorageName(QStringLiteral("awb-markdown"));
    profile->setOffTheRecord(true);
    profile->setHttpCacheType(QQuickWebEngineProfile::MemoryHttpCache);
    profile->setPersistentCookiesPolicy(
        QQuickWebEngineProfile::NoPersistentCookies);
    profile->installUrlSchemeHandler(
        QByteArray(MarkdownRuntime::kScheme), m_handler);
    m_profile = profile;
    return m_profile;
}

} // namespace awb::markdown
