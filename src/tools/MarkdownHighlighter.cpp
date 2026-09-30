#include "tools/MarkdownHighlighter.h"

#include <QFont>
#include <QRegularExpression>
#include <QString>
#include <QTextDocument>

namespace awb::tools {

namespace {

/// 围栏代码块的跨行状态：普通块 = StateNormal，进入 ``` 之后 = StateInFence。
enum BlockState {
    StateNormal = 0,
    StateInFence = 1,
};

/// 行内已格式化区间 [start, end)。后续规则命中与之相交的匹配一律跳过：
/// 行内代码是原子区间（内部不该再出现粗体高亮），粗体也要让斜体规则
/// 闭嘴——`*text*` 的正则会匹配到 `**text**` 的内部，靠占位区间排除。
struct Span
{
    int start = 0;
    int end = 0;
};

/** 行内代码：`code`，内容不含反引号与换行。 */
const QRegularExpression kReInlineCode(QStringLiteral("`([^`\\n]+)`"));
/** 粗体：**text** 或 __text__。 */
const QRegularExpression kReBold(QStringLiteral("\\*\\*.+?\\*\\*|__.+?__"));
/** 删除线：~~text~~。 */
const QRegularExpression kReStrike(QStringLiteral("~~.+?~~"));
/** 链接：[text](url)。 */
const QRegularExpression kReLink(QStringLiteral("\\[([^\\]\\n]+)\\]\\(([^()\\n]*)\\)"));
/** 斜体：*text*。前后不能贴着字词字符（挡掉 foo*bar*baz），内部不含星号。 */
const QRegularExpression kReItalic(
    QStringLiteral("(?<!\\w)\\*(?![\\s*])[^*\\n]+?(?<![\\s*])\\*(?!\\w)"));
/** ATX 标题行首：1-6 个 # 后跟空白或行尾。 */
const QRegularExpression kReHeading(QStringLiteral("^(#{1,6})(?=\\s|$)"));
/** 水平分隔线：--- / *** / ___，允许至多 3 个前导空格。 */
const QRegularExpression kReDivider(QStringLiteral("^ {0,3}(?:-{3,}|\\*{3,}|_{3,}) *$"));
/** 引用行首：>。 */
const QRegularExpression kReQuote(QStringLiteral("^ {0,3}>"));
/** 列表标记行首：- * + 或 1. 1)，后跟空白或行尾。 */
const QRegularExpression kReListMarker(
    QStringLiteral("^ {0,3}(?:[-*+]|\\d{1,9}[.)])(?= |$)"));
/** 围栏行：``` 打头（开与闭不做区分，内容宽容处理）。 */
const QRegularExpression kReFence(QStringLiteral("^ {0,3}```"));

/**
 * @brief 判断 [start, end) 是否与任一占用区间相交
 *
 * @param spans 已占用的区间列表
 * @param start 待检查区间起点（含）
 * @param end 待检查区间终点（不含）
 * @return 相交返回 true
 */
bool intersects(const QVector<Span> &spans, int start, int end)
{
    for (const Span &span : spans) {
        if (start < span.end && end > span.start)
            return true;
    }
    return false;
}

/**
 * @brief 把正则匹配的捕获区间收窄成 Span
 *
 * Qt 6 的 capturedStart/capturedEnd 返回 qsizetype，Span 以 int 存储区间；
 * 单个文本块的长度受 int 上限约束，显式收窄是安全的。
 *
 * @param match 正则匹配
 * @return 对应的 [start, end) 区间
 */
Span spanOf(const QRegularExpressionMatch &match)
{
    return Span{static_cast<int>(match.capturedStart()),
                static_cast<int>(match.capturedEnd())};
}

/**
 * @brief 判断一行是否为 ``` 围栏行
 *
 * @param text 当前块文本
 * @return 是围栏行返回 true
 */
bool isFenceLine(const QString &text)
{
    return kReFence.match(text).hasMatch();
}

/**
 * @brief 构造只带前景色的字符格式
 *
 * @param color 前景色
 * @return 前景色字符格式
 */
QTextCharFormat colorFormat(const QColor &color)
{
    QTextCharFormat fmt;
    fmt.setForeground(color);
    return fmt;
}

} // namespace

/**
 * @brief 构造高亮器并立即对整篇文档做一次高亮
 *
 * @param document 被高亮的文档（同时作为父对象，随文档一起销毁）
 * @param colors 初始配色
 */
MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document,
                                         const MarkdownColors &colors)
    : QSyntaxHighlighter(document)
    , m_colors(colors)
{
}

/**
 * @brief 替换配色
 *
 * 只存值不重扫：主题切换后由调用方统一 rehighlight()，测试也借此在
 * 不重建文档的情况下换一组颜色验证。
 *
 * @param colors 新配色
 */
void MarkdownHighlighter::setColors(const MarkdownColors &colors)
{
    m_colors = colors;
}

/**
 * @brief 返回当前配色
 *
 * @return 当前配色
 */
MarkdownColors MarkdownHighlighter::colors() const
{
    return m_colors;
}

/**
 * @brief 高亮单个文本块（一行）
 *
 * 处理顺序：围栏状态机（跨行）→ 标题 / 分隔线 / 引用（整行，命中即收）→
 * 列表标记（只染标记，行内容继续）→ 行内规则（代码 → 粗体 → 删除线 →
 * 链接 → 斜体，后套的规则跳过已占用区间）。
 *
 * @param text 当前块文本
 */
void MarkdownHighlighter::highlightBlock(const QString &text)
{
    // 围栏代码块：块状态在开/闭围栏之间保持 StateInFence，闭围栏行本身
    // 按围栏色高亮，围栏之间的内容整行按代码块色高亮。
    if (previousBlockState() == StateInFence) {
        const bool closes = isFenceLine(text);
        setCurrentBlockState(closes ? StateNormal : StateInFence);
        setFormat(0, text.length(), colorFormat(closes ? m_colors.fence
                                                        : m_colors.codeBlock));
        return;
    }
    setCurrentBlockState(StateNormal);
    if (isFenceLine(text)) {
        setCurrentBlockState(StateInFence);
        setFormat(0, text.length(), colorFormat(m_colors.fence));
        return;
    }

    // 标题：整行上标题色并加粗。
    if (kReHeading.match(text).hasMatch()) {
        QTextCharFormat fmt;
        fmt.setForeground(m_colors.heading);
        fmt.setFontWeight(QFont::Bold);
        setFormat(0, text.length(), fmt);
        return;
    }

    // 水平分隔线：整行弱化。
    if (kReDivider.match(text).hasMatch()) {
        setFormat(0, text.length(), colorFormat(m_colors.divider));
        return;
    }

    // 引用：整行引用色加斜体。
    if (kReQuote.match(text).hasMatch()) {
        QTextCharFormat fmt;
        fmt.setForeground(m_colors.quote);
        fmt.setFontItalic(true);
        setFormat(0, text.length(), fmt);
        return;
    }

    // 列表标记：只染标记本身（含前导空格之后到标记末尾），行内容继续
    // 参加行内规则。
    const QRegularExpressionMatch listMatch = kReListMarker.match(text);
    if (listMatch.hasMatch()) {
        QTextCharFormat fmt;
        fmt.setForeground(m_colors.listMarker);
        fmt.setFontWeight(QFont::Bold);
        setFormat(listMatch.capturedStart(), listMatch.capturedLength(), fmt);
    }

    QVector<Span> consumed;

    // 行内代码：原子区间，先占位。
    QRegularExpressionMatchIterator it = kReInlineCode.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(),
                  colorFormat(m_colors.code));
        consumed.append(spanOf(match));
    }

    // 粗体：整段加粗，标记对（** 或 __）再上强调色。setFormat 是覆盖
    // 不是合并，标记格式必须自带属性，否则会丢掉整段的加粗。
    it = kReBold.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (intersects(consumed, match.capturedStart(), match.capturedEnd()))
            continue;
        QTextCharFormat whole;
        whole.setFontWeight(QFont::Bold);
        setFormat(match.capturedStart(), match.capturedLength(), whole);
        QTextCharFormat marker;
        marker.setForeground(m_colors.emphasis);
        marker.setFontWeight(QFont::Bold);
        setFormat(match.capturedStart(), 2, marker);
        setFormat(match.capturedEnd() - 2, 2, marker);
        consumed.append(spanOf(match));
    }

    // 删除线：整段划线，~~ 标记对上强调色（同样自带属性）。
    it = kReStrike.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (intersects(consumed, match.capturedStart(), match.capturedEnd()))
            continue;
        QTextCharFormat whole;
        whole.setFontStrikeOut(true);
        setFormat(match.capturedStart(), match.capturedLength(), whole);
        QTextCharFormat marker;
        marker.setForeground(m_colors.emphasis);
        marker.setFontStrikeOut(true);
        setFormat(match.capturedStart(), 2, marker);
        setFormat(match.capturedEnd() - 2, 2, marker);
        consumed.append(spanOf(match));
    }

    // 链接：整个 [text](url) 上链接色加下划线。
    it = kReLink.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (intersects(consumed, match.capturedStart(), match.capturedEnd()))
            continue;
        QTextCharFormat fmt;
        fmt.setForeground(m_colors.link);
        fmt.setFontUnderline(true);
        setFormat(match.capturedStart(), match.capturedLength(), fmt);
        consumed.append(spanOf(match));
    }

    // 斜体：最后套，跳过一切已占用区间；标记自带斜体属性。
    it = kReItalic.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (intersects(consumed, match.capturedStart(), match.capturedEnd()))
            continue;
        QTextCharFormat whole;
        whole.setFontItalic(true);
        setFormat(match.capturedStart(), match.capturedLength(), whole);
        QTextCharFormat marker;
        marker.setForeground(m_colors.emphasis);
        marker.setFontItalic(true);
        setFormat(match.capturedStart(), 1, marker);
        setFormat(match.capturedEnd() - 1, 1, marker);
        consumed.append(spanOf(match));
    }
}

} // namespace awb::tools
