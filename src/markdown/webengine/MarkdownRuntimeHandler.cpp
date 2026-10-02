#include "markdown/webengine/MarkdownRuntimeHandler.h"

#include "core/Logging.h"
#include "markdown/MarkdownPreviewController.h"
#include "markdown/MarkdownRuntime.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QUrlQuery>
#include <QWebEngineUrlRequestJob>

namespace awb::markdown {

namespace {

// 信标以外的请求都只允许读。QWebEngineUrlRequestJob 也给出
// requestMethod()/requestBody()，但预览页什么都不 POST——把非 GET 挡在
// 这里，页面脚本就借不了这条通道去碰本机任何东西。
bool isReadMethod(const QByteArray &method)
{
    return method == QByteArray("GET") || method == QByteArray("HEAD");
}

// awbmd://runtime/asset/<docId>/<rel> 里 rel 的第一段是 docId，剩下的才是
// 相对文档目录的路径。
QString assetRelativePath(const QString &rest, QString *docIdOut)
{
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash < 0) {
        if (docIdOut) {
            *docIdOut = rest;
        }
        return QString();
    }
    if (docIdOut) {
        *docIdOut = rest.left(slash);
    }
    return rest.mid(slash + 1);
}

} // namespace

/**
 * @brief 构造供体
 *
 * @param controller 文档载荷与页面上报的去向（不拥有；它比本类活得久，
 *        因为 main.cpp 里控制器在提供者之前声明）
 * @param parent 父对象
 */
MarkdownRuntimeHandler::MarkdownRuntimeHandler(
    MarkdownPreviewController *controller, QObject *parent)
    : QWebEngineUrlSchemeHandler(parent),
      m_controller(controller)
{
}

/**
 * @brief 分发一个 awbmd:// 请求
 * @param job 引擎给的请求作业；本函数要么 reply 要么 fail，不能都不做
 *
 * 引擎不接管响应体的生命周期：`reply()` 的 QIODevice 所有权不归 WebEngine，
 * 而文档要求它至少活到 job 结束。所以这里把 buffer 的 parent 设成 job——
 * job 由引擎持有并在请求结束时销毁，设备就跟着它一起走，既不悬垂也不泄漏。
 * 直接回一个栈上 QBuffer 是这类 handler 的经典崩溃点。
 */
void MarkdownRuntimeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    const QUrl url = job->requestUrl();
    if (!isReadMethod(job->requestMethod())) {
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }

    QString rest;
    switch (routeOf(url, &rest)) {
    case Route::Event: {
        QString kind;
        QString payload;
        parseBeacon(url, &kind, &payload);
        if (isKnownEventKind(kind) && m_controller) {
            if (kind == QLatin1String("ready")) {
                m_controller->reportReady(payload);
            } else if (kind == QLatin1String("frame")) {
                m_controller->reportFrame(payload);
            } else if (kind == QLatin1String("error")) {
                m_controller->reportError(payload);
            } else {
                // 剩下唯一的一种是 find：命中数与小序号都是几百字节的东西，
                // 走信标正合适。导出 HTML 不走这里（见 MarkdownRuntime 的
                // eventKinds() 注释）。
                m_controller->reportFindResult(payload);
            }
        } else if (!kind.isEmpty()) {
            AWB_WARNING << "markdown: page sent an unknown event:" << kind;
        }
        replyBytes(job, beaconPixel(), QStringLiteral("image/png"));
        return;
    }
    case Route::Document: {
        if (!m_controller) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        const QByteArray body =
            m_controller->documentBodyJson().toUtf8();
        replyBytes(job, body,
                   MarkdownRuntime::mimeTypeForPath(QStringLiteral("/doc.json")));
        return;
    }
    case Route::Resource: {
        bool found = false;
        const QByteArray bytes = resourceBytes(rest, &found);
        if (!found) {
            AWB_WARNING << "markdown: runtime asset missing:" << rest;
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        replyBytes(job, bytes, MarkdownRuntime::mimeTypeForPath(rest));
        return;
    }
    case Route::Asset: {
        if (!m_controller) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        QString docId;
        const QString rel = assetRelativePath(rest, &docId);
        // docId 是这次打开生成的，对不上就是陈旧的页面在要东西；形状不对的
        // 一律不给，否则预览一张图片能读出用户主目录。
        if (docId != m_controller->docId() || !isSafeRelativePath(rel)) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        const QString root = m_controller->assetRoot();
        const QString file = QDir(root).absoluteFilePath(rel);
        if (!file.startsWith(QDir(root).absolutePath())) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        QFile image(file);
        if (!image.open(QIODevice::ReadOnly)) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        const QByteArray bytes = image.readAll();
        image.close();
        replyBytes(job, bytes, MarkdownRuntime::mimeTypeForPath(file));
        return;
    }
    case Route::Unknown:
        break;
    }
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
}

/**
 * @brief 分类一个 awbmd:// URL
 * @param url 待分类的 URL
 * @param rest 输出：去掉 "/asset/" 之类前缀之后的路径（Resource 给完整
 *        资源相对路径，Asset 给 "<docId>/<rel>"）
 * @return 路由类别
 *
 * host 必须正好是 MarkdownRuntime::kHost：scheme 是全局注册的，本 handler 装在
 * 预览 profile 上，任何别的 host 都不该被答话——答了就是把「未知路径」变成
 * 「一次资源读取」。
 */
MarkdownRuntimeHandler::Route MarkdownRuntimeHandler::routeOf(const QUrl &url,
                                                              QString *rest)
{
    const auto setRest = [rest](const QString &value) {
        if (rest) {
            *rest = value;
        }
    };
    if (url.scheme() != QLatin1String(MarkdownRuntime::kScheme)
        || url.host() != QLatin1String(MarkdownRuntime::kHost)) {
        setRest(QString());
        return Route::Unknown;
    }
    // QUrl::path() 已经做过百分号解码，这里不再 decode：一次解码才和磁盘上
    // 的真实文件名对齐。
    const QString path = url.path();
    if (path == QLatin1String("/doc.json")) {
        setRest(QStringLiteral("doc.json"));
        return Route::Document;
    }
    if (path == QLatin1String("/event")) {
        setRest(QString());
        return Route::Event;
    }
    if (path.startsWith(QLatin1String("/vendor/"))
        || path.startsWith(QLatin1String("/app/"))) {
        setRest(path.mid(1));
        return Route::Resource;
    }
    if (path.startsWith(QLatin1String("/asset/"))) {
        setRest(path.mid(QString::fromLatin1("/asset/").size()));
        return Route::Asset;
    }
    setRest(QString());
    return Route::Unknown;
}

/**
 * @brief 拆出信标的事件名与负载
 * @param url 信标 URL
 * @param kind 输出：事件名（query 的 k）
 * @param payload 输出：负载文本（query 的 p）
 *
 * 负载在发送侧就按字节截过上限（MarkdownRuntime::eventUrl），这里只管取。
 */
void MarkdownRuntimeHandler::parseBeacon(const QUrl &url, QString *kind,
                                         QString *payload)
{
    const QUrlQuery query(url);
    if (kind) {
        *kind = query.queryItemValue(QStringLiteral("k"),
                                     QUrl::FullyDecoded);
    }
    if (payload) {
        *payload = query.queryItemValue(QStringLiteral("p"),
                                        QUrl::FullyDecoded);
    }
}

/**
 * @brief 事件名是否在协议表里
 * @param kind 事件名
 * @return 表里有则为真
 */
bool MarkdownRuntimeHandler::isKnownEventKind(const QString &kind)
{
    return MarkdownRuntime::eventKinds().contains(kind);
}

/**
 * @brief 文档相对路径的形状过滤
 * @param relPath 页面给的相对路径（可以是 Windows 的反斜杠写法）
 * @return 可以给则为真
 *
 * 逐段判 ".."（而不是整串 contains("..")）：`images/v1.2.png` 这种名字里
 * 带连点的合法路径不该被挡。反斜杠先归一，否则 `..\..\windows\win.ini`
 * 从形状上看不出来。绝对路径（`C:\…`、`\…`）一律拒绝——它们不属于
 * 「文档目录里的相对资源」这个语义。
 */
bool MarkdownRuntimeHandler::isSafeRelativePath(const QString &relPath)
{
    const QString normalized = QDir::fromNativeSeparators(relPath);
    if (normalized.isEmpty() || QFileInfo(normalized).isAbsolute()) {
        return false;
    }
    const QStringList parts = normalized.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part == QLatin1String("..")) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 一像素透明 PNG
 * @return PNG 字节
 *
 * 每次现算不如现成一段十六进制字面量省事，但那样等于往代码里塞一坨没人能
 * 核对的魔数；这里用 QImage 现生成，格式由 Qt 保证。
 */
QByteArray MarkdownRuntimeHandler::beaconPixel()
{
    static const QByteArray pixel = [] {
        QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        buffer.close();
        return bytes;
    }();
    return pixel;
}

/**
 * @brief 取随包资源字节，首次读时缓存
 * @param path 资源相对路径（不带前导 '/'）
 * @param found 输出：资源是否存在
 * @return 资源字节；不存在时为空
 */
QByteArray MarkdownRuntimeHandler::resourceBytes(const QString &path,
                                                 bool *found)
{
    const QString resource = MarkdownRuntime::resourcePathForPath(path);
    if (resource.isEmpty()) {
        if (found) {
            *found = false;
        }
        return QByteArray();
    }
    const auto cached = m_resources.constFind(resource);
    if (cached != m_resources.constEnd()) {
        if (found) {
            *found = true;
        }
        return cached.value();
    }
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) {
        if (found) {
            *found = false;
        }
        return QByteArray();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    m_resources.insert(resource, bytes);
    if (found) {
        *found = true;
    }
    return bytes;
}

/**
 * @brief 用给定字节与 MIME 回应请求
 * @param job 请求作业
 * @param bytes 响应体
 * @param mimeType Content-Type；空串按未知处理，直接 fail
 *
 * MIME 不能猜错：脚本被当文本返回时页面只是白屏、不报错，排查成本极高，
 * 所以未知类型宁可 fail 也不回一个 text/plain。
 */
void MarkdownRuntimeHandler::replyBytes(QWebEngineUrlRequestJob *job,
                                        const QByteArray &bytes,
                                        const QString &mimeType)
{
    if (mimeType.isEmpty()) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }
    auto *device = new QBuffer(job);   // 所有权见 requestStarted 的注释
    device->setData(bytes);
    device->open(QIODevice::ReadOnly);
    job->reply(mimeType.toLatin1(), device);
}

} // namespace awb::markdown
