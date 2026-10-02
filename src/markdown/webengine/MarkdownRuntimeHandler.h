#ifndef AWB_MARKDOWN_MARKDOWNRUNTIMEHANDLER_H
#define AWB_MARKDOWN_MARKDOWNRUNTIMEHANDLER_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QUrl>

#include <QWebEngineUrlSchemeHandler>

class QWebEngineUrlRequestJob;

namespace awb::markdown {

class MarkdownPreviewController;

/// awbmd://runtime/… 的唯一供体：随包渲染资源、当前文档、文档相对的图片，
/// 以及页面上报的小事件，全部从这一个 origin 里供出去。
///
/// 为什么走自定义 scheme 而不是 data: URL、解包到磁盘、或本机 HTTP：只有
/// 真同源才能让页面 fetch() 自己的文档、@font-face 拿到 KaTeX 的 woff2，
/// 而这两件事预览页每次都要做。细节与代价写在
/// docs/development/markdown-preview.md。
///
/// URL 的形状只由 MarkdownRuntime 决定（awbmd://runtime/<path> 与
/// :/markdown/<path> 一一对应）。页面 JS 不拼 URL，本类也不认第二种形状——
/// 两边各认一份时，改一处就会表现成「页面白屏且只有 404」。
class MarkdownRuntimeHandler : public QWebEngineUrlSchemeHandler
{
    Q_OBJECT

public:
    /// URL 路径的去处。分类是纯函数（routeOf），单测直接喂 URL 断言，
    /// 不需要真的驱动一次网络栈。
    enum class Route {
        Resource,   ///< /vendor/…、/app/…：随包资源，字节来自 Qt 资源
        Document,   ///< /doc.json：当前文档的载荷
        Asset,      ///< /asset/<docId>/…：文档目录里的图片等相对资源
        Event,      ///< /event?k=…&p=…：页面上报的小事件（GET 信标）
        Unknown     ///< 其余一律 UrlNotFound，不猜
    };

    /// 1×1 透明 PNG，信标的响应体。页面只在乎请求有没有回来，
    /// 但 Chromium 会按 Content-Type 尝试解码 new Image() 的结果，
    /// 回一段文本会让控制台留下永远解不开的噪声。
    static QByteArray beaconPixel();

    explicit MarkdownRuntimeHandler(MarkdownPreviewController *controller,
                                    QObject *parent = nullptr);

    /// QWebEngineUrlSchemeHandler 的唯一入口：每个 awbmd:// 请求进来一次，
    /// 本类要么 reply 要么 fail。
    void requestStarted(QWebEngineUrlRequestJob *job) override;

    // --- 纯函数，也是单测的入口 -------------------------------------------
    static Route routeOf(const QUrl &url, QString *rest = nullptr);
    /// 信标 URL 的 k/p；没有 k 时 kind 为空串
    static void parseBeacon(const QUrl &url, QString *kind, QString *payload);
    /// 事件名 → 交给控制器的哪个 report* 入口；表里没有的名字返回 false
    /// 并且什么都不调用（页面不能借信标触达任何未声明的方法）
    static bool isKnownEventKind(const QString &kind);
    /// 文档相对路径的形状过滤：空串、绝对路径、任何 ".." 段都不给。
    /// 真正的边界是 requestStarted 里「解析后的绝对路径仍在文档目录内」
    /// 那次前缀比对，这里只是把明显不该碰的形状先挡掉——它是纯函数，
    /// 单测直接喂路径断言，不需要造一个 QWebEngineUrlRequestJob。
    static bool isSafeRelativePath(const QString &relPath);

private:
    // 随包资源首次请求时读进内存并留着：mermaid 那一份 2.6 MB 是压缩资源，
    // 每个请求现解压一遍是白扔几十毫秒，而字体一次首屏就要来七八个请求。
    QByteArray resourceBytes(const QString &path, bool *found);
    void replyBytes(QWebEngineUrlRequestJob *job, const QByteArray &bytes,
                    const QString &mimeType);

    MarkdownPreviewController *m_controller;   ///< 文档与上报的去向（不拥有）
    QHash<QString, QByteArray> m_resources;    ///< 资源路径 → 已读出的字节
};

} // namespace awb::markdown

#endif // AWB_MARKDOWN_MARKDOWNRUNTIMEHANDLER_H
