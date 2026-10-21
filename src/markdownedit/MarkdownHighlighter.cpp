#include "markdownedit/MarkdownHighlighter.h"

#include <QFont>
#include <QRegularExpression>
#include <QString>
#include <QTextDocument>

namespace awb::markdownedit {

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
/** 下划线：<u>text</u>（窄白名单 HTML，内容不含 < 与换行、不嵌套）。 */
const QRegularExpression kReUnderline(QStringLiteral("<u>[^<\\n]+?</u>"));
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

/**
 * @brief 把 (start, length) 区间写入块内格式网格（覆盖语义）
 *
 * 与 QSyntaxHighlighter::setFormat 同一条纪律：后写的区间整体替换先写
 * 的。语法规则先在这里合成网格，highlightBlock 末尾一次性刷出——查找
 * 高亮因此可以**合并**进语法色，而不是像 setFormat 那样把语法前景色
 * 覆盖掉（setFormat 是逐字符赋值，实测见 tst_markdownedit 的
 * testSearchHighlightWorksOnHeadingLines）。
 *
 * @param grid 块内逐字符格式网格
 * @param start 区间起点（块内偏移）
 * @param length 区间长
 * @param format 目标格式（整段同一格式）
 */
void paintRange(QVector<QTextCharFormat> &grid, int start, int length,
                const QTextCharFormat &format)
{
    const int end = qMin(start + length, grid.size());
    for (int i = qMax(0, start); i < end; ++i) {
        grid[i] = format;
    }
}

} // namespace

/**
 * @brief 判定 needle 是否命中 text 的 at 处
 *
 * 大小写不敏感按逐字符 caseFolded 比较（不整串 toLower，避免给每块文本
 * 造两份副本）；全词匹配看命中两侧是否为单词字符。文本边缘（at == 0、
 * 命中贴着串尾）视作非单词边界——块边界与文档首尾同理，跨块的命中判定
 * 因此与 findFrom 的全文口径一致。
 *
 * @param text 被搜文本（单块或整篇）
 * @param needle 关键词（空串永不命中）
 * @param at 候选起点
 * @param caseSensitive 区分大小写
 * @param wholeWords 全词匹配
 * @return 命中返回 true
 */
bool needleMatchesAt(const QString &text, const QString &needle, int at,
                     bool caseSensitive, bool wholeWords)
{
    if (needle.isEmpty() || at < 0 || at + needle.length() > text.length()) {
        return false;
    }
    if (wholeWords) {
        const auto isWordChar = [](QChar ch) {
            return ch.isLetterOrNumber() || ch == QLatin1Char('_');
        };
        const bool leftOk = at == 0 || !isWordChar(text.at(at - 1));
        const int end = at + needle.length();
        const bool rightOk = end >= text.length()
                || !isWordChar(text.at(end));
        if (!leftOk || !rightOk) {
            return false;
        }
    }
    for (int i = 0; i < needle.length(); ++i) {
        const QChar a = text.at(at + i);
        const QChar b = needle.at(i);
        if (caseSensitive ? (a != b)
                          : (a.toCaseFolded() != b.toCaseFolded())) {
            return false;
        }
    }
    return true;
}

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
 * 网格三段：语法规则主体写进**格式网格**（与 setFormat 同一条覆盖语义：后写替换先写），查找命中在其上合并背景色，最后把网格按连续相同格式合并成区间刷出。网格是必须的：`setFormat` 是逐字符**覆盖**，查找高亮直接套会丢掉代码块、标题行里命中的语法前景色。
 */
void MarkdownHighlighter::highlightBlock(const QString &text)
{
    QVector<QTextCharFormat> grid;
    grid.resize(text.length());
    highlightSyntax(text, grid);
    applySearchHighlight(text, grid);

    // 网格刷出：连续相同格式合并成一个区间；默认格式（无语法、无查找）
    // 不写，保持块内无区间。
    int start = 0;
    while (start < text.length()) {
        int end = start + 1;
        while (end < text.length() && grid.at(end) == grid.at(start)) {
            ++end;
        }
        if (grid.at(start) != QTextCharFormat()) {
            setFormat(start, end - start, grid.at(start));
        }
        start = end;
    }
}

/**
 * @brief 高亮单个文本块的语法部分
 *
 * 处理顺序：围栏状态机（跨行）→ 标题 / 分隔线 / 引用（整行，命中即收）→
 * 列表标记（只染标记，行内容继续）→ 行内规则（代码 → 粗体 → 删除线 →
 * 链接 → 斜体，后套的规则跳过已占用区间）。结果写进格式网格，**不是**
 * 直接 setFormat——网格让随后的查找高亮能合并进来。
 *
 * @param text 当前块文本
 * @param grid 块内逐字符格式网格（输出）
 */
void MarkdownHighlighter::highlightSyntax(const QString &text,
                                          QVector<QTextCharFormat> &grid)
{
    // 围栏代码块：块状态在开/闭围栏之间保持 StateInFence，闭围栏行本身
    // 按围栏色高亮，围栏之间的内容整行按代码块色高亮。
    if (previousBlockState() == StateInFence) {
        const bool closes = isFenceLine(text);
        setCurrentBlockState(closes ? StateNormal : StateInFence);
        paintRange(grid, 0, text.length(), colorFormat(closes ? m_colors.fence
                                                        : m_colors.codeBlock));
        return;
    }
    setCurrentBlockState(StateNormal);
    if (isFenceLine(text)) {
        setCurrentBlockState(StateInFence);
        paintRange(grid, 0, text.length(), colorFormat(m_colors.fence));
        return;
    }

    // 标题：整行上标题色并加粗。
    if (kReHeading.match(text).hasMatch()) {
        QTextCharFormat fmt;
        fmt.setForeground(m_colors.heading);
        fmt.setFontWeight(QFont::Bold);
        paintRange(grid, 0, text.length(), fmt);
        return;
    }

    // 水平分隔线：整行弱化。
    if (kReDivider.match(text).hasMatch()) {
        paintRange(grid, 0, text.length(), colorFormat(m_colors.divider));
        return;
    }

    // 引用：整行引用色加斜体。
    if (kReQuote.match(text).hasMatch()) {
        QTextCharFormat fmt;
        fmt.setForeground(m_colors.quote);
        fmt.setFontItalic(true);
        paintRange(grid, 0, text.length(), fmt);
        return;
    }

    // 列表标记：只染标记本身（含前导空格之后到标记末尾），行内容继续
    // 参加行内规则。
    const QRegularExpressionMatch listMatch = kReListMarker.match(text);
    if (listMatch.hasMatch()) {
        QTextCharFormat fmt;
        fmt.setForeground(m_colors.listMarker);
        fmt.setFontWeight(QFont::Bold);
        paintRange(grid, listMatch.capturedStart(), listMatch.capturedLength(), fmt);
    }

    QVector<Span> consumed;

    // 行内代码：原子区间，先占位。
    QRegularExpressionMatchIterator it = kReInlineCode.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        paintRange(grid, match.capturedStart(), match.capturedLength(),
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
        paintRange(grid, match.capturedStart(), match.capturedLength(), whole);
        QTextCharFormat marker;
        marker.setForeground(m_colors.emphasis);
        marker.setFontWeight(QFont::Bold);
        paintRange(grid, match.capturedStart(), 2, marker);
        paintRange(grid, match.capturedEnd() - 2, 2, marker);
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
        paintRange(grid, match.capturedStart(), match.capturedLength(), whole);
        QTextCharFormat marker;
        marker.setForeground(m_colors.emphasis);
        marker.setFontStrikeOut(true);
        paintRange(grid, match.capturedStart(), 2, marker);
        paintRange(grid, match.capturedEnd() - 2, 2, marker);
        consumed.append(spanOf(match));
    }

    // 下划线：整段加下划线字形，<u></u> 标记对上强调色（标记不对称，
    // 长度各是 3 与 4）。同样自带属性防 setFormat 覆盖丢字形。
    it = kReUnderline.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (intersects(consumed, match.capturedStart(), match.capturedEnd()))
            continue;
        QTextCharFormat whole;
        whole.setFontUnderline(true);
        paintRange(grid, match.capturedStart(), match.capturedLength(), whole);
        QTextCharFormat marker;
        marker.setForeground(m_colors.emphasis);
        marker.setFontUnderline(true);
        paintRange(grid, match.capturedStart(), 3, marker);
        paintRange(grid, match.capturedEnd() - 4, 4, marker);
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
        paintRange(grid, match.capturedStart(), match.capturedLength(), fmt);
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
        paintRange(grid, match.capturedStart(), match.capturedLength(), whole);
        QTextCharFormat marker;
        marker.setForeground(m_colors.emphasis);
        marker.setFontItalic(true);
        paintRange(grid, match.capturedStart(), 1, marker);
        paintRange(grid, match.capturedEnd() - 1, 1, marker);
        consumed.append(spanOf(match));
    }
}

/**
 * @brief 设置查找高亮的关键词并立即重扫
 *
 * 空串清除查找高亮。立即 rehighlight 而不是等下一次文档改动：关键词是
 * 用户在命令条里输入的，不落在文档改动事件上。
 *
 * @param needle 关键词（空串 = 清除）
 * @param caseSensitive 区分大小写
 * @param wholeWords 全词匹配
 */
void MarkdownHighlighter::setSearch(const QString &needle,
                                    bool caseSensitive, bool wholeWords)
{
    m_searchNeedle = needle;
    m_searchCaseSensitive = caseSensitive;
    m_searchWholeWords = wholeWords;
    rehighlight();
}

/**
 * @brief 给当前块里的查找命中叠背景色
 *
 * 只带背景色的格式在 setFormat 的合并语义下不会盖掉语法前景色（命中落
 * 在代码块、标题行里时依旧可读）。命中口径与 MarkdownEdit::findFrom 同
 * 一处（needleMatchesAt）：不重叠推进（命中后跳过整个关键词），与
 * replaceAll 的替换计数一致。
 *
 * @param text 当前块文本
 */
void MarkdownHighlighter::applySearchHighlight(const QString &text,
                                              QVector<QTextCharFormat> &grid)
{
    if (m_searchNeedle.isEmpty() || !m_colors.search.isValid()) {
        return;
    }
    QTextCharFormat format;
    format.setBackground(m_colors.search);
    const int needleLength = m_searchNeedle.length();
    int at = 0;
    while (at + needleLength <= text.length()) {
        if (needleMatchesAt(text, m_searchNeedle, at,
                            m_searchCaseSensitive, m_searchWholeWords)) {
            // 合并背景色进网格：网格里已有的语法格式保留前景/字形，
            // 只补一层底色；无语法格式的命中就是纯底色。
            for (int i = at; i < at + needleLength && i < grid.size(); ++i) {
                grid[i].merge(format);
            }
            at += needleLength;
        } else {
            ++at;
        }
    }
}

} // namespace awb::markdownedit
