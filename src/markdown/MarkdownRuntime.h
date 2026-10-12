#ifndef AWB_MARKDOWN_MARKDOWNRUNTIME_H
#define AWB_MARKDOWN_MARKDOWNRUNTIME_H

#include "markdown/MarkdownChunker.h"

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVector>

namespace awb::theme {
class Theme;
} // namespace awb::theme

namespace awb::markdown {

/// 预览页面侧的纯字符串层：URL 布局、MIME 表、资源映射、主题→CSS 变量、
/// 页面文案、大纲提取、命令 JSON 组装。
///
/// 这些都不碰 WebEngine，也就不碰 QML，所以能在 tst_markdown 里直接调用。
/// 适配器（src/markdown/webengine/）拿到的一律是这里算好的字符串，
/// 两边之间唯一的契约就是这里的这几个函数——把「URL 长什么样」「什么 MIME」
/// 「主题怎么变成 CSS 变量」这类只有页面 JS 在意、却必须由 C++ 决定的事情
/// 收在一处，否则它们会散落到 handler、窗口 QML 和 renderer.js 三个地方。
///
/// 本层同时是「这个构建到底有没有预览能力」的判定者：isSupportedBuild()
/// 是编译期事实（Qt 6 + WebEngine），app/CMakeLists.txt 的资源嵌入用的是
/// 同一个条件，两者不一致的表现是「菜单项可点、页面 404」。
class MarkdownRuntime
{
public:
    /// 自定义 scheme 名。注册必须在 QtWebEngineQuick::initialize() 之前
    /// （QWebEngineUrlScheme::registerScheme() 晚调即被忽略），见 app/main.cpp。
    static constexpr const char *kScheme = "awbmd";
    /// 唯一的 host：一个 origin 供全部资源与文档，同源才有 fetch()。
    static constexpr const char *kHost = "runtime";

    /// 资源根前缀：awbmd://runtime/<path> 对应 :/markdown/<path>。
    static constexpr const char *kResourceRoot = "/markdown";

    /// 事件信标 query 里 payload 的字节上限。信标走 GET 的 URL，长度是
    /// Chromium 的 URL 上限，但真正该防的是「有人把大结果塞进信标」——
    /// 大结果一律经 runJavaScript 的返回值取回，这里超了直接截断并告警。
    static constexpr int kEventPayloadBytes = 8 * 1024;

    // 本构建是否具备预览能力（Qt 6 + AWB_ENABLE_WEBENGINE）
    static bool isSupportedBuild();
    // 不具备时给用户的解释（英文源串；具备时为空）
    static QString unavailableReason();

    // --- URL 构造 ----------------------------------------------------------
    // awbmd://runtime/<path>
    static QUrl runtimeUrl(const QString &path);
    // 第三方运行时：<dir>/<file> → awbmd://runtime/vendor/<dir>/<file>
    static QUrl vendorUrl(const QString &dir, const QString &file);
    // 本页自有资源 → awbmd://runtime/app/<file>
    static QUrl appUrl(const QString &file);
    // 当前文档（rev 变了页面才重新 fetch）
    static QUrl documentUrl(int rev);
    // 文档相对路径的图片等资源；relPath 里的 ../ 由调用方先归一
    static QUrl assetUrl(const QString &docId, const QString &relPath);
    // JS→C++ 的信标：kind 与 payload 都在 query 里
    static QUrl eventUrl(const QString &kind, const QString &payload);
    // 信标 payload 字节的解码（超长时回退到 UTF-8 字符边界再截）；公开是为了
    // 让测试能直接喂一段被截断的字节。
    static QString payloadFromBeacon(const QByteArray &bytes);

    // awbmd://runtime/<path> 的路径部分 → Qt 资源路径（:/markdown/<path>）
    static QString resourcePathForPath(const QString &path);
    // 按扩展名给 Content-Type；未知扩展返回空串（调用方据此 404）
    static QString mimeTypeForPath(const QString &path);
    // 随包资源文本（:/markdown/<path>，UTF-8）：预览面在推入车道把
    // theme.css 的文本喂给页面时用（fetch 车道由页面自己取，不经这里）。
    // 读不到返回空串。
    static QString resourceTextForPath(const QString &path);

    // --- 随包清单 ----------------------------------------------------------
    // 嵌入的 manifest.json（:/markdown/vendor/manifest.json）里的库版本表；
    // 读不到时返回空 map（预览仍可用，只是「关于」里没版本信息）。
    static QVariantMap vendorVersions();
    // 渲染栈版本的展示串，例如
    // "markdown-it 15.0.2 · KaTeX 0.19.0 · Mermaid 11.17.2 · highlight.js 11.12.0"
    static QString rendererVersionText();

    // --- 主题与文案 --------------------------------------------------------
    // Theme 令牌 → 页面 CSS 自定义属性（键含 "--awb-" 前缀）
    static QVariantMap cssVariables(const theme::Theme *theme);
    // 页面 chrome 的字符串字典：JS 里没有可翻译源串，全部由这里给
    static QJsonObject pageStrings();

    // --- 文档结构 --------------------------------------------------------
    // 块序列 → 大纲（level/text/anchor/blockIndex），供大纲面板与搜索定位
    static QVariantList outlineFromBlocks(const QVector<MarkdownBlock> &blocks);
    // 标题行的纯文本：去掉强调、链接、行内代码、图片、HTML 标签
    static QString plainHeadingText(const QString &line);
    // 块的标题级别；非标题块返回 0
    static int headingLevelOf(const MarkdownBlock &block);

    // --- 命令 JSON -------------------------------------------------------
    // {"cmd":cmd,"seq":seq,"args":{...}} 的唯一拼装处；seq 由调用方递增，
    // 页面用它把结果对回命令。
    static QString commandJson(const QString &cmd, int seq,
                               const QJsonObject &args);

    // --- 协议名表 --------------------------------------------------------
    // C++ → 页面的命令名。这张表是唯一权威：MarkdownPreviewController::enqueue()
    // 按它挡下写错的名字，页面的 dispatch 分支与 tst_markdownruntime 也都对着
    // 它断言。协议是字符串的，没有编译期检查，不加这张表就会漂移到「页面收了
    // 一个没人发的命令」。
    static const QStringList &commandNames();
    // 页面 → C++ 的信标事件名（awbmd://runtime/event?k=…）。scheme handler 按
    // 它分发到 MarkdownPreviewController 的 report* 入口，同样由测试对齐。
    static const QStringList &eventKinds();
};

} // namespace awb::markdown

#endif // AWB_MARKDOWN_MARKDOWNRUNTIME_H
