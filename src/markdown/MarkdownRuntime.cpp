#include "markdown/MarkdownRuntime.h"

#include "core/Logging.h"
#include "theme/Theme.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

namespace awb::markdown {

namespace {

// 扩展名 → Content-Type。页面 JS 与字体都靠它，自定义 scheme 上没有服务器
// 帮忙嗅探，猜错 MIME 的表现是「脚本文件当文本返回、页面白屏且没有任何错误」。
const QHash<QString, QString> &mimeTable()
{
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> m;
        m.insert(QStringLiteral("html"), QStringLiteral("text/html"));
        m.insert(QStringLiteral("js"), QStringLiteral("application/javascript"));
        m.insert(QStringLiteral("mjs"), QStringLiteral("text/javascript"));
        m.insert(QStringLiteral("css"), QStringLiteral("text/css"));
        m.insert(QStringLiteral("json"), QStringLiteral("application/json"));
        m.insert(QStringLiteral("svg"), QStringLiteral("image/svg+xml"));
        m.insert(QStringLiteral("png"), QStringLiteral("image/png"));
        m.insert(QStringLiteral("jpg"), QStringLiteral("image/jpeg"));
        m.insert(QStringLiteral("jpeg"), QStringLiteral("image/jpeg"));
        m.insert(QStringLiteral("gif"), QStringLiteral("image/gif"));
        m.insert(QStringLiteral("webp"), QStringLiteral("image/webp"));
        m.insert(QStringLiteral("avif"), QStringLiteral("image/avif"));
        m.insert(QStringLiteral("ico"), QStringLiteral("image/x-icon"));
        m.insert(QStringLiteral("bmp"), QStringLiteral("image/bmp"));
        m.insert(QStringLiteral("woff2"), QStringLiteral("font/woff2"));
        m.insert(QStringLiteral("woff"), QStringLiteral("font/woff"));
        m.insert(QStringLiteral("ttf"), QStringLiteral("font/ttf"));
        m.insert(QStringLiteral("otf"), QStringLiteral("font/otf"));
        m.insert(QStringLiteral("map"), QStringLiteral("application/json"));
        return m;
    }();
    return table;
}

// 主题令牌 → 页面 CSS 变量。两边同名是巧合而不是约定：改页面样式表时
// 只看这张表，不要在 JS 里再拼一份。
struct TokenBinding
{
    const char *token;    ///< Theme 的令牌名（color() / metric() 的入参）
    const char *cssVar;   ///< 页面里的 --awb-* 变量名
};

const TokenBinding kColorBindings[] = {
    { "textPrimary", "--awb-text" },
    { "textSecondary", "--awb-text-secondary" },
    { "textMuted", "--awb-text-muted" },
    { "textDisabled", "--awb-text-disabled" },
    { "textLink", "--awb-link" },
    { "textOnAccent", "--awb-text-on-accent" },
    { "accent", "--awb-accent" },
    { "focusRing", "--awb-focus" },
    { "workspaceBg", "--awb-page-bg" },
    { "surfaceBg", "--awb-surface" },
    { "surfaceAltBg", "--awb-surface-alt" },
    { "surfaceHoverBg", "--awb-surface-hover" },
    { "consoleBg", "--awb-code-bg" },
    { "borderSubtle", "--awb-border" },
    { "borderStrong", "--awb-border-strong" },
    { "separator", "--awb-separator" },
    { "selectionBg", "--awb-selection-bg" },
    { "selectionText", "--awb-selection-text" },
    { "scrollbar", "--awb-scrollbar" },
    { "danger", "--awb-danger" },
    { "warning", "--awb-warning" },
    { "success", "--awb-success" },
    { "info", "--awb-info" },
};

const TokenBinding kMetricBindings[] = {
    { "fontSizeBody", "--awb-font-size" },
    { "fontSizeCaption", "--awb-font-size-small" },
    { "fontSizePageTitle", "--awb-font-size-title" },
    { "radiusCard", "--awb-radius" },
    { "radiusControl", "--awb-radius-control" },
    { "spacingS", "--awb-gap-s" },
    { "spacingM", "--awb-gap" },
    { "spacingL", "--awb-gap-l" },
};

// 页面 chrome 的源串都在这里：lupdate 看不见 .js，所以 JS 侧一个可翻译串
// 都不写，运行期从 boot 载荷里取这本字典。每条都用
// QCoreApplication::translate("MarkdownPreviewPage", "字面量") 的原形写——
// 包一层 page(text) 助手的话，lupdate 从形参里抽不到串，整本字典都不进 .ts。

// 标题行的 ATX 前缀：行首最多 3 个空格（CommonMark 的 content 缩进），
// #..###### 后跟空格或行尾。`#hashtag` 与 7 个以上连号的 # 都不算。
const QRegularExpression &atxRe()
{
    static const QRegularExpression re(
        QStringLiteral("^ {0,3}(#{1,6})(\\s|$)"));
    return re;
}

// ATX 结尾的可选闭合串：`### 标题 ###`，必须前面有空白、后面只剩空白
const QRegularExpression &atxClosingRe()
{
    static const QRegularExpression re(QStringLiteral("\\s+#+\\s*$"));
    return re;
}

// setext 下划线：整行只有 = 或只有 -，且至少一个字符。level 可为空指针
// （只需要判定时不必接级别）。
bool isSetextUnderline(const QString &line, int *levelOut)
{
    static const QRegularExpression re(QStringLiteral("^([=-]+)$"));
    const QRegularExpressionMatch match = re.match(line.trimmed());
    if (!match.hasMatch()) {
        return false;
    }
    if (levelOut) {
        *levelOut = match.captured(1).startsWith(QLatin1Char('=')) ? 1 : 2;
    }
    return true;
}

// 用「第 group 个捕获组」替换整段匹配。
//
// 不能用 text.replace(re, QStringLiteral("$1"))：QString::replace 的替换串是
// **字面量**，不解释捕获组引用，结果是每个链接的标题都变成字面上的 "$1"——
// 大纲里全是 $1 这种字符串，而冒烟测试什么都测不出来。真正带组引用的
// 全局替换要自己按 globalMatch 重建字符串。
QString keepGroup(const QRegularExpression &re, const QString &text, int group)
{
    QString out;
    int cursor = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += text.mid(cursor, match.capturedStart() - cursor);
        out += match.captured(group);
        cursor = match.capturedEnd();
    }
    out += text.mid(cursor);
    return out;
}

// 逐个把行内标记换成它的可读部分
QString stripInlineMarkup(const QString &input)
{
    static const QRegularExpression imageRe(
        QStringLiteral("!\\[([^]]*)]\\([^)]*\\)"));
    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^]]*)]\\([^)]*\\)"));
    static const QRegularExpression autolinkRe(
        QStringLiteral("<((https?|ftp|mailto)[^>]*)>"));
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    static const QRegularExpression codeRe(QStringLiteral("`([^`]*)`"));
    static const QRegularExpression emphasisRe(
        QStringLiteral("(\\*{1,3}|_{1,3}|~{2})([^*_~]+)\\1"));
    // 转义符 + 一个标点。字符类里的 ] 必须转义：没转义的那个 ] 会**提前闭合**
    // 字符类，后面的 ()#+ 全变成类外的字面量与空组，正则照样编译通过，
    // 只是永远匹配不到 `\*`——大纲里就留着反斜杠。
    static const QRegularExpression escapeRe(
        QStringLiteral("\\\\([\\\\`*_{}\\[\\]()#+\\-.!|])"));
    static const QRegularExpression spaceRe(QStringLiteral("[ \\t]+"));

    QString text = keepGroup(imageRe, input, 1);
    text = keepGroup(linkRe, text, 1);
    text = keepGroup(autolinkRe, text, 1);
    text.replace(tagRe, QString());            // 标签整段去掉，没有要留的组
    text = keepGroup(codeRe, text, 1);
    // 强调成对出现才剥；循环到不动为止，让 "**a *b***" 这类嵌套也能收敛。
    QString previous;
    do {
        previous = text;
        text = keepGroup(emphasisRe, text, 2);
    } while (text != previous);
    text = keepGroup(escapeRe, text, 1);
    // 只并平铺空白：大纲标签是单行文字，但换行在这里不该变成空格
    text.replace(spaceRe, QStringLiteral(" "));
    return text.trimmed();
}

} // namespace

/**
 * @brief 本构建是否具备 Markdown 预览能力
 * @return Qt 6 + WebEngine 为真
 *
 * 判据是编译期的 AWB_MD_PREVIEW_SUPPORTED，由 src/markdown/CMakeLists.txt
 * 在 `AWB_ENABLE_WEBENGINE AND QT_VERSION_MAJOR EQUAL 6` 时定义——与
 * app/CMakeLists.txt 嵌入运行时资源、以及 webengine/ 适配器被编译的条件
 * 三者同源。Qt 5 与关 WebEngine 的构建里预览**不存在**，而不是存在但坏掉。
 */
bool MarkdownRuntime::isSupportedBuild()
{
#ifdef AWB_MD_PREVIEW_SUPPORTED
    return true;
#else
    return false;
#endif
}

/**
 * @brief 不具备能力时给用户的解释
 * @return 英文源串；具备能力时为空串
 */
QString MarkdownRuntime::unavailableReason()
{
    if (isSupportedBuild()) {
        return QString();
    }
    return QCoreApplication::translate(
        "MarkdownRuntime",
        "This build has no Markdown preview: it needs Qt 6 with WebEngine "
        "enabled.");
}

/**
 * @brief 拼一个 awbmd://runtime/<path> URL
 * @param path 斜杠开头的资源路径
 * @return 构造好的 URL
 */
QUrl MarkdownRuntime::runtimeUrl(const QString &path)
{
    const QString clean = path.startsWith(QLatin1Char('/'))
        ? path.mid(1) : path;
    return QUrl(QStringLiteral("%1://%2/%3")
                    .arg(QLatin1String(kScheme), QLatin1String(kHost), clean));
}

/**
 * @brief 第三方运行时 URL
 * @param dir 库目录名（manifest 的 dir 字段，不是 npm 包名）
 * @param file 目录内文件名
 * @return awbmd://runtime/vendor/<dir>/<file>
 */
QUrl MarkdownRuntime::vendorUrl(const QString &dir, const QString &file)
{
    return runtimeUrl(QStringLiteral("/vendor/%1/%2").arg(dir, file));
}

/**
 * @brief 本页自有资源 URL
 * @param file runtime/ 目录里的文件名
 * @return awbmd://runtime/app/<file>
 */
QUrl MarkdownRuntime::appUrl(const QString &file)
{
    return runtimeUrl(QStringLiteral("/app/%1").arg(file));
}

/**
 * @brief 当前文档的 fetch 地址
 * @param rev 文档版本号
 * @return 带 rev 的 doc.json URL
 *
 * rev 进 query 而不是让页面每次渲染都重取同一份：URL 变了 Chromium 才认为是
 * 新资源，rev 没变时页面的 fetch 命中它自己的缓存即可。
 */
QUrl MarkdownRuntime::documentUrl(int rev)
{
    QUrl url = runtimeUrl(QStringLiteral("/doc.json"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("rev"), QString::number(rev));
    url.setQuery(query);
    return url;
}

/**
 * @brief 文档相对资源的 URL
 * @param docId 本次打开的文档 id（同一窗口内稳定）
 * @param relPath 相对文档目录的路径，调用方已归一（不含 ../）
 * @return awbmd://runtime/asset/<docId>/<relPath>
 *
 * relPath 按 '/' 分段逐个百分号编码：README 里 `![](images/a b.png)` 这种
 * 带空格的路径很常见，整串编码会把 '/' 也吃掉。
 */
QUrl MarkdownRuntime::assetUrl(const QString &docId, const QString &relPath)
{
    const QStringList parts = relPath.split(QLatin1Char('/'));
    QStringList encoded;
    encoded.reserve(parts.size());
    for (const QString &part : parts) {
        encoded.append(QString::fromUtf8(
            QUrl::toPercentEncoding(part, QByteArray())));
    }
    return runtimeUrl(QStringLiteral("/asset/%1/%2")
                          .arg(docId, encoded.join(QLatin1Char('/'))));
}

/**
 * @brief JS→C++ 信标 URL
 * @param kind 事件名
 * @param payload 事件负载（JSON 文本）
 * @return awbmd://runtime/event?k=..&p=..
 *
 * 超长的 payload 在这里就截掉：信标是 GET 的 URL，塞大负载既撞 URL 上限，
 * 也说明调用方走错了通道（大结果应由 C++ 用 runJavaScript 的返回值取回）。
 */
QUrl MarkdownRuntime::eventUrl(const QString &kind, const QString &payload)
{
    QByteArray bytes = payload.toUtf8();
    if (bytes.size() > kEventPayloadBytes) {
        AWB_WARNING << "markdown: event beacon truncated:" << kind
                    << bytes.size() << "bytes";
        bytes.truncate(kEventPayloadBytes);
    }
    QUrl url = runtimeUrl(QStringLiteral("/event"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("k"), kind);
    query.addQueryItem(QStringLiteral("p"), payloadFromBeacon(bytes));
    url.setQuery(query);
    return url;
}

/**
 * @brief 把信标 payload 的字节解回字符串
 * @param bytes 已经（或还没）截到上限的 UTF-8 字节
 * @return 解码结果
 *
 * 截断可能把一个多字节字符切一半，所以截到的位置要回退到字符边界，否则
 * fromUtf8 在串尾留下一个 U+FFFD——那是「payload 被切了」的信号，不是内容。
 */
QString MarkdownRuntime::payloadFromBeacon(const QByteArray &bytes)
{
    if (bytes.size() > kEventPayloadBytes) {
        AWB_WARNING << "markdown: event beacon truncated:"
                    << bytes.size() << "bytes";
        int cut = kEventPayloadBytes;
        // UTF-8 续接字节形如 10xxxxxx：从截点往前退到首字节为止。
        while (cut > 0 && (bytes.at(cut) & 0xC0) == 0x80) {
            --cut;
        }
        return QString::fromUtf8(bytes.constData(), cut);
    }
    return QString::fromUtf8(bytes.constData(), bytes.size());
}

/**
 * @brief 把 awbmd 路径映射到 Qt 资源路径
 * @param path URL 的路径部分（可带或不带前导 '/'）
 * @return :/markdown/<path>；含 ".." 时返回空串
 *
 * 前缀写 ':/' 而不是 'qrc:/'，这不是风格问题：QFile 对 'qrc:/…' 在 Windows 上
 * 先按原生文件系统判定路径，得到的是「文件名、目录名或卷标语法不正确」的打开
 * 失败，而 ':/…' 一直走资源引擎（本仓库读资源的既有代码 ThemeRegistry、
 * AgentRepository、WebEngineCompat 用的都是 ':/'）。'qrc:' 只在 QML 的
 * source / iconSource 那类 URL 消费者里才是惯用写法。
 */
QString MarkdownRuntime::resourceTextForPath(const QString &path)
{
    const QString resource = resourcePathForPath(path);
    if (resource.isEmpty()) {
        return QString();
    }
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    return text;
}

QString MarkdownRuntime::resourcePathForPath(const QString &path)
{
    QString clean = path;
    while (clean.startsWith(QLatin1Char('/'))) {
        clean.remove(0, 1);
    }
    if (clean.contains(QLatin1String(".."))) {
        return QString();
    }
    return QStringLiteral(":%1/%2")
        .arg(QLatin1String(kResourceRoot), clean);
}

/**
 * @brief 按扩展名给出 Content-Type
 * @param path 资源或文档路径（query 不参与判断）
 * @return MIME 串；未知扩展为空串
 */
QString MarkdownRuntime::mimeTypeForPath(const QString &path)
{
    const QString suffix = QFileInfo(path.split(QLatin1Char('?')).constFirst())
                               .suffix().toLower();
    return mimeTable().value(suffix);
}

/**
 * @brief 读取随包清单里的库版本表
 * @return 包名 → 版本号；清单缺失时为空 map
 */
QVariantMap MarkdownRuntime::vendorVersions()
{
    QVariantMap versions;
    QFile file(QStringLiteral(":/markdown/vendor/manifest.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        AWB_WARNING << "markdown: vendor manifest is missing"
                    << file.fileName();
        return versions;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return versions;
    }
    const QJsonArray libs = doc.object().value(QStringLiteral("libs")).toArray();
    for (const QJsonValue &value : libs) {
        const QJsonObject lib = value.toObject();
        versions.insert(lib.value(QStringLiteral("name")).toString(),
                        lib.value(QStringLiteral("version")).toString());
    }
    return versions;
}

/**
 * @brief 渲染栈版本的展示串
 * @return "markdown-it 15.0.2 · KaTeX 0.19.0 · …"；无清单时为空串
 */
QString MarkdownRuntime::rendererVersionText()
{
    const QVariantMap versions = vendorVersions();
    if (versions.isEmpty()) {
        return QString();
    }
    // 清单里的名字是 npm 包名（全小写、highlight.js 带点），展示要按各项目
    // 自己的官方写法——用户在「关于」里读的是 KaTeX 与 Mermaid，不是 katex。
    // 顺序即渲染管线的顺序，新库加在这里，别按字母排。
    const QList<QPair<QString, QString>> order = {
        { QStringLiteral("markdown-it"), QStringLiteral("markdown-it") },
        { QStringLiteral("katex"), QStringLiteral("KaTeX") },
        { QStringLiteral("mermaid"), QStringLiteral("Mermaid") },
        { QStringLiteral("highlight.js"), QStringLiteral("highlight.js") },
    };
    QStringList parts;
    for (const auto &entry : order) {
        if (versions.contains(entry.first)) {
            parts.append(QStringLiteral("%1 %2")
                             .arg(entry.second, versions.value(entry.first).toString()));
        }
    }
    return parts.join(QStringLiteral(" · "));
}

/**
 * @brief 主题令牌 → 页面 CSS 变量
 * @param theme 当前主题（可为空指针，此时只给出变体名）
 * @return 变量名 → CSS 值
 *
 * 颜色一律写成 rgba()，不写 #rrggbb：主题的 tooltip/selection 一类令牌带
 * alpha，name() 会把 alpha 抹掉，深浅两个主题因此看上去一样。变体名单独
 * 给一个 --awb-variant，页面据此选 mermaid 的 default / dark 主题。
 */
QVariantMap MarkdownRuntime::cssVariables(const theme::Theme *theme)
{
    QVariantMap vars;
    vars.insert(QStringLiteral("--awb-variant"), QStringLiteral("dark"));
    if (!theme) {
        return vars;
    }
    vars.insert(QStringLiteral("--awb-variant"), theme->variant());
    for (const TokenBinding &binding : kColorBindings) {
        const QColor color = theme->color(QLatin1String(binding.token));
        if (!color.isValid()) {
            continue;
        }
        vars.insert(QLatin1String(binding.cssVar),
                    QStringLiteral("rgba(%1,%2,%3,%4)")
                        .arg(color.red())
                        .arg(color.green())
                        .arg(color.blue())
                        .arg(color.alphaF(), 0, 'g', 3));
    }
    for (const TokenBinding &binding : kMetricBindings) {
        const double value = theme->metric(QLatin1String(binding.token));
        if (value <= 0.0) {
            continue;
        }
        vars.insert(QLatin1String(binding.cssVar),
                    QStringLiteral("%1px").arg(value, 0, 'f', 1));
    }
    vars.insert(QStringLiteral("--awb-font-family"), theme->family());
    vars.insert(QStringLiteral("--awb-font-mono"), theme->monoFamily());
    return vars;
}

/**
 * @brief 页面 chrome 的字符串字典
 * @return key → 已翻译字符串
 *
 * 键是页面 JS 用的固定标识，值才是可翻译源串：JS 侧不认识任何语言，
 * 缺键时它自己退回键名（可见的错，好过静默的空白）。
 */
QJsonObject MarkdownRuntime::pageStrings()
{
    QJsonObject strings;
    strings.insert(QStringLiteral("outline"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Outline"));
    strings.insert(QStringLiteral("search"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Search"));
    strings.insert(QStringLiteral("findPlaceholder"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Find in document"));
    strings.insert(QStringLiteral("noMatches"), QCoreApplication::translate(
        "MarkdownPreviewPage", "No matches"));
    strings.insert(QStringLiteral("matchCount"), QCoreApplication::translate(
        "MarkdownPreviewPage", "%1 of %2"));
    strings.insert(QStringLiteral("caseSensitive"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Match case"));
    strings.insert(QStringLiteral("previous"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Previous match"));
    strings.insert(QStringLiteral("next"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Next match"));
    strings.insert(QStringLiteral("closeSearch"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Close search"));
    strings.insert(QStringLiteral("copy"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Copy selection"));
    strings.insert(QStringLiteral("copySource"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Copy Markdown source"));
    strings.insert(QStringLiteral("openExternal"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Open in browser"));
    strings.insert(QStringLiteral("exportHtml"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Export standalone HTML"));
    strings.insert(QStringLiteral("exported"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Exported"));
    strings.insert(QStringLiteral("save"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Save"));
    strings.insert(QStringLiteral("saved"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Saved"));
    strings.insert(QStringLiteral("discard"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Discard changes"));
    strings.insert(QStringLiteral("reload"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Reload from disk"));
    strings.insert(QStringLiteral("edit"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Edit"));
    strings.insert(QStringLiteral("previewOnly"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Preview only"));
    strings.insert(QStringLiteral("fontSize"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Font size"));
    strings.insert(QStringLiteral("contentWidth"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Content width"));
    strings.insert(QStringLiteral("renderError"), QCoreApplication::translate(
        "MarkdownPreviewPage", "This block failed to render"));
    strings.insert(QStringLiteral("diagramError"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Diagram could not be drawn"));
    strings.insert(QStringLiteral("diagramDeferred"), QCoreApplication::translate(
        "MarkdownPreviewPage", "Click to render this diagram"));
    strings.insert(QStringLiteral("emptyDocument"), QCoreApplication::translate(
        "MarkdownPreviewPage", "The document is empty"));
    strings.insert(QStringLiteral("changedOnDisk"), QCoreApplication::translate(
        "MarkdownPreviewPage", "The file changed on disk while you were editing"));
    strings.insert(QStringLiteral("stats"), QCoreApplication::translate(
        "MarkdownPreviewPage", "%1 lines, %2 words, %3 blocks"));
    return strings;
}

/**
 * @brief 块的标题级别
 * @param block 待判定的块
 * @return 1~6；不是标题时 0
 */
int MarkdownRuntime::headingLevelOf(const MarkdownBlock &block)
{
    const QStringList lines = block.text.split(QLatin1Char('\n'));
    if (lines.isEmpty()) {
        return 0;
    }
    const QRegularExpressionMatch atx = atxRe().match(lines.constFirst());
    if (atx.hasMatch()) {
        return atx.captured(1).size();
    }
    if (lines.size() >= 2) {
        int level = 0;
        // setext 的正文行必须自己像一段文字：围栏、表格、引用都不算，
        // 「--- ===」这种两行分隔线也不算。
        const QString &first = lines.constFirst();
        if (isSetextUnderline(lines.at(1), &level)
            && !first.startsWith(QLatin1Char('#'))
            && !first.startsWith(QLatin1Char('>'))
            && !first.startsWith(QLatin1Char('|'))
            && !first.startsWith(QLatin1Char('`'))
            && !first.startsWith(QLatin1Char('~'))
            && !isSetextUnderline(first, nullptr)) {
            return level;
        }
    }
    return 0;
}

/**
 * @brief 块序列 → 大纲
 * @param blocks split() 的结果
 * @return 每项含 level / text / anchor / blockIndex
 *
 * anchor 与页面 DOM 的 `<section id>` 同名，所以点一条就直接滚到那一块，
 * 不需要 JS 再算一次映射。文本超过 120 字截断——大纲面板不显示整篇文档。
 */
QVariantList MarkdownRuntime::outlineFromBlocks(
    const QVector<MarkdownBlock> &blocks)
{
    QVariantList outline;
    for (const MarkdownBlock &block : blocks) {
        const int level = headingLevelOf(block);
        if (level == 0) {
            continue;
        }
        const QStringList lines = block.text.split(QLatin1Char('\n'));
        // ATX 取首行（去掉 # 前缀），setext 也取首行（正文在上、下划线在后）。
        QString text = plainHeadingText(lines.constFirst());
        if (text.length() > 120) {
            text = text.left(119) + QChar(0x2026);
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("level"), level);
        entry.insert(QStringLiteral("text"), text);
        entry.insert(QStringLiteral("anchor"),
                     QStringLiteral("b%1").arg(block.index));
        entry.insert(QStringLiteral("blockIndex"), block.index);
        outline.append(entry);
    }
    return outline;
}

/**
 * @brief 标题行的纯文本
 * @param line 标题原文（含 ATX 前缀）
 * @return 去掉标记后的文本
 *
 * 前后两层 # 都要剥：CommonMark 允许 `## 标题 ##` 的闭合串，只去前缀的话
 * 大纲里会留一串 `##`，而它是点完跳过去的那行字，用户看得见。
 */
QString MarkdownRuntime::plainHeadingText(const QString &line)
{
    QString text = line;
    text.replace(atxRe(), QString());
    text.replace(atxClosingRe(), QString());
    return stripInlineMarkup(text);
}

/**
 * @brief 组装一条 C++→JS 命令
 * @param cmd 命令名
 * @param seq 调用方维护的递增序号
 * @param args 命令参数
 * @return 单行 JSON 文本，可直接放进 AWB.dispatch(...)
 */
QString MarkdownRuntime::commandJson(const QString &cmd, int seq,
                                     const QJsonObject &args)
{
    QJsonObject message;
    message.insert(QStringLiteral("cmd"), cmd);
    message.insert(QStringLiteral("seq"), seq);
    message.insert(QStringLiteral("args"), args);
    return QString::fromUtf8(
        QJsonDocument(message).toJson(QJsonDocument::Compact));
}

/**
 * @brief C++ → 页面的命令名表
 * @return 唯一的命令名列表
 *
 * 名字同时出现在三处：这里的表、MarkdownPreviewController 的 enqueue() 调用、
 * 页面 renderer.js 的 dispatch 分支。改一处必须改三处，测试只挡得住前两处。
 */
const QStringList &MarkdownRuntime::commandNames()
{
    static const QStringList names = {
        QStringLiteral("setDocument"),
        QStringLiteral("updateBlocks"),
        QStringLiteral("setView"),
        QStringLiteral("setTheme"),
        QStringLiteral("scrollTo"),
        QStringLiteral("find"),
        QStringLiteral("renderAll"),
        QStringLiteral("serialize"),
    };
    return names;
}

/**
 * @brief 页面 → C++ 的信标事件名表
 * @return 唯一的事件名列表
 *
 * 每个事件都对应 MarkdownPreviewController 的一个 report* 入口，scheme handler
 * 照着这张表分发；表里没有的 kind 只记警告，不让页面能调用到任何未声明的方法。
 *
 * 这张表只列**小结果**：信标是 GET 的 URL，负载受 kEventPayloadCap 约束，超了
 * 会被截断。导出的整篇 HTML（reportMarkup）与渲染耗时里的大对象一律不在这里——
 * 它们由 QML 侧的 runJavaScript 返回值取回，那条通道没有长度限制。把 markup
 * 放进来会诱使后来人把大结果塞进信标，所以这里刻意不收它。
 */
const QStringList &MarkdownRuntime::eventKinds()
{
    static const QStringList kinds = {
        QStringLiteral("ready"),
        QStringLiteral("frame"),
        QStringLiteral("error"),
        QStringLiteral("find"),
    };
    return kinds;
}

} // namespace awb::markdown
