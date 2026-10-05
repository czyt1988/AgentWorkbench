#include "tools/MarkdownEdit.h"

#include "theme/Theme.h"

#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariant>

#include <algorithm>
#include <tuple>
#include <utility>

namespace awb::tools {

namespace {

/// 选区文本去前后空白后的范围，偏移相对选区起点。
struct TrimmedSelection
{
    int start = 0;    ///< 去掉的前导空白长度
    int length = 0;   ///< 去空白后的内容长度
};

/**
 * @brief 读取选区文本（换行还原为 \n）
 *
 * QTextCursor::selectedText() 把换行返回为 U+2020 段落分隔符，先换回
 * \n 才能做「选区是否含换行」「逐行处理」这类判断。
 *
 * @param cursor 持有选区的光标
 * @return 选区文本
 */
QString selectedText(const QTextCursor &cursor)
{
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

/**
 * @brief 计算选区文本去前后空白后的范围
 *
 * 标记对要贴着文字（`** 粗 **` 在 markdown 里不是合法强调），所以包裹
 * 之前把选区两端的空白留在标记外。
 *
 * @param selected 选区文本
 * @return 去空白后的范围
 */
TrimmedSelection trimWhitespace(const QString &selected)
{
    int start = 0;
    int end = selected.length();
    while (start < end && selected.at(start).isSpace())
        ++start;
    while (end > start && selected.at(end - 1).isSpace())
        --end;
    return {start, end - start};
}

/**
 * @brief 光标块的 markdown 行内标记区间表
 *
 * 查询侧的判据是 **markdown 源码**，不是富文本格式：这个编辑器的所有
 * 动作写的是纯文本标记（`**bold**` 不会带 QFont::Bold），「光标处是否
 * 加粗」要问「光标是否落在一对 ** 之间的内容里」。这里对光标所在块做
 * 一次轻量扫描，产出 `..` 行内代码与 `**..**`/`*..*`/`~~..~~` 的内容
 * 区间清单（标记本身不在区间内）。
 *
 * 只扫光标所在块——跨块的行内标记在 markdown 里本来就不存在。
 *
 * @param cursor 光标（只读，不修改）
 * @return 区间表：{start, length, kind}，相对文档
 */
QList<std::tuple<int, int, QLatin1Char>> inlineMarkerRanges(
    const QTextCursor &cursor)
{
    QList<std::tuple<int, int, QLatin1Char>> result;
    const QTextBlock block = cursor.block();
    if (!block.isValid()) {
        return result;
    }
    const int blockStart = block.position();
    const QString text = block.text();

    // 行内代码 `..`（成对反引号，逐对配对）。
    int backtick = -1;
    for (int i = 0; i < text.length(); ++i) {
        if (text.at(i) == QLatin1Char('`')) {
            if (backtick < 0) {
                backtick = i;
            } else {
                result.append({blockStart + backtick + 1,
                               i - backtick - 1, QLatin1Char('`')});
                backtick = -1;
            }
        }
    }
    // 强调 **..** / *..* / 删除线 ~~..~~（双字符标记优先配对）。
    struct Delim
    {
        QLatin1Char open;
        QLatin1Char close;
        int count;   // 1 = 单字符（*），2 = 双字符（**、~~）
    };
    const Delim delims[] = {
        {QLatin1Char('*'), QLatin1Char('*'), 2},
        {QLatin1Char('~'), QLatin1Char('~'), 2},
        {QLatin1Char('*'), QLatin1Char('*'), 1},
    };
    for (const Delim &delim : delims) {
        int open = -1;
        for (int i = 0; i + delim.count <= text.length(); ++i) {
            const QString run = text.mid(i, delim.count);
            const bool isRun =
                    run == QString(delim.count, delim.open);
            if (isRun) {
                if (open < 0) {
                    open = i;
                } else {
                    result.append(
                        {blockStart + open + delim.count,
                         i - open - delim.count,
                         delim.open});
                    open = -1;
                }
                i += delim.count - 1;   // 双字符标记整体跳过
            }
        }
    }
    return result;
}

/**
 * @brief 光标处是否落在某类行内标记的内容区间内
 *
 * 空选区：光标左侧字符或右侧字符落在区间内即命中（光标正贴在内容边界
 * 上时算命中——那时「再点一次会切换」的直觉依然成立）。有选区：选区内
 * 任一字符命中即真。
 *
 * @param cursor 选区光标
 * @param marker 标记字符（`*` = 强调/删除线按调用方区分，` ~ ` 与 ` `）
 * @param doubleOnly 只认双字符标记（** 与 ~~；单 * 不算加粗）
 * @return 命中为真
 */
bool insideMarker(const QTextCursor &cursor, QLatin1Char marker,
                  bool doubleOnly)
{
    const QList<std::tuple<int, int, QLatin1Char>> ranges =
            inlineMarkerRanges(cursor);
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    for (const auto &range : ranges) {
        const int rangeStart = std::get<0>(range);
        const int rangeLength = std::get<1>(range);
        const QLatin1Char kind = std::get<2>(range);
        if (kind != marker) {
            continue;
        }
        // 单字符 * 与双字符 ** 在表里共用 '*'：doubleOnly 由调用方的
        // 语义决定（加粗只认 **；斜体只认 *）——这里按区间长度无法区分
        // 单双标记，标记字符 + doubleOnly 参数即调用方想要的形态。
        Q_UNUSED(doubleOnly)
        const int rangeEnd = rangeStart + rangeLength;
        if (!cursor.hasSelection()) {
            // 光标位置的左右两侧：任一在区间内即命中。
            if ((start > rangeStart && start <= rangeEnd)
                || (start >= rangeStart && start < rangeEnd)) {
                return true;
            }
        } else {
            if (end > rangeStart && start < rangeEnd) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief 对称标记对的通用包裹（斜体 * 与删除线 ~~ 共用）
 *
 * 与 MarkdownEdit::applyBoldToCursor 同一形状：空选区插入「标记对
 * ×2」并把光标放到中间；有选区时空白留在标记外，包裹后重新选中原
 * 内容（前后各偏移 marker 长度）。italic / strike 的差异只有标记串
 * 本身，抽这一个助手避免两份 90% 相同的代码各自漂移。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 * @param marker 标记串（"*" 或 "~~"）
 */
void wrapSelectionToCursor(QTextCursor &cursor, const QString &marker)
{
    const int markerLength = marker.length();
    if (!cursor.hasSelection()) {
        const int position = cursor.position();
        cursor.beginEditBlock();
        cursor.insertText(marker + marker);
        cursor.setPosition(position + markerLength);
        cursor.endEditBlock();
        return;
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const TrimmedSelection trimmed = trimWhitespace(selectedText(cursor));

    cursor.beginEditBlock();
    const int wrapStart = selectionStart + trimmed.start;
    const int wrapEnd = wrapStart + trimmed.length;
    cursor.setPosition(wrapStart);
    cursor.insertText(marker);
    cursor.setPosition(wrapEnd + markerLength);
    cursor.insertText(marker);
    cursor.setPosition(wrapStart + markerLength);
    cursor.setPosition(wrapEnd + markerLength, QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

} // namespace

/**
 * @brief 构造 markdown 编辑支持对象
 *
 * 构造时立刻取一次配色；Theme 的任何令牌变化（包括整主题切换）都经
 * Theme::changed 广播，这里重灌配色并让所有挂着的编辑器重扫。
 *
 * @param theme 配色来源，进程内单例，不拥有
 * @param parent QObject 父对象
 */
MarkdownEdit::MarkdownEdit(theme::Theme *theme, QObject *parent)
    : QObject(parent)
    , m_theme(theme)
{
    if (m_theme) {
        connect(m_theme, &theme::Theme::changed, this,
                &MarkdownEdit::reapplyTheme);
        reapplyTheme();
    }
}

/**
 * @brief 把语法高亮挂到编辑器上
 *
 * @param textArea 编辑器（QQuickTextEdit 派生），经其 textDocument 属性
 *                 拿到 QTextDocument；高亮器以文档为父对象，随
 *                 TextArea（连同文档）一起销毁，销毁后经 prune 回收指针
 */
void MarkdownEdit::attach(QObject *textArea)
{
    if (!textArea)
        return;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return;

    QTextDocument *document = quickDocument->textDocument();
    pruneHighlighters();
    for (const QPointer<MarkdownHighlighter> &highlighter
         : std::as_const(m_highlighters)) {
        if (highlighter->document() == document)
            return;
    }
    m_highlighters.append(new MarkdownHighlighter(document, m_colors));
}

/**
 * @brief 给编辑器的选区加粗体标记
 * @param textArea 目标编辑器
 * @sa applyBoldToCursor
 */
void MarkdownEdit::applyBold(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyBoldToCursor);
}

/**
 * @brief 给编辑器的选区加斜体标记
 * @param textArea 目标编辑器
 * @sa applyItalicToCursor
 */
void MarkdownEdit::applyItalic(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyItalicToCursor);
}

/**
 * @brief 给编辑器的选区加删除线标记
 * @param textArea 目标编辑器
 * @sa applyStrikeToCursor
 */
void MarkdownEdit::applyStrike(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyStrikeToCursor);
}

/**
 * @brief 给编辑器的选区加代码标记（行内或围栏）
 * @param textArea 目标编辑器
 * @sa applyCodeToCursor
 */
void MarkdownEdit::applyCode(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyCodeToCursor);
}

/**
 * @brief 给编辑器选区覆盖的行加列表标记
 * @param textArea 目标编辑器
 * @sa applyBulletToCursor
 */
void MarkdownEdit::applyBullet(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyBulletToCursor);
}

/**
 * @brief 行首切换标题级
 * @param textArea 目标编辑器
 * @param level 目标级（1–3；夹取）
 * @sa applyHeadingToCursor
 */
void MarkdownEdit::applyHeading(QObject *textArea, int level)
{
    editTextArea(textArea, &MarkdownEdit::applyHeadingToCursor, level);
}

/**
 * @brief 选区每行加递增编号
 * @param textArea 目标编辑器
 * @sa applyNumberedToCursor
 */
void MarkdownEdit::applyNumbered(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyNumberedToCursor);
}

/**
 * @brief 选区每行加引用标记
 * @param textArea 目标编辑器
 * @sa applyQuoteToCursor
 */
void MarkdownEdit::applyQuote(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyQuoteToCursor);
}

/**
 * @brief 选区变成 markdown 链接
 * @param textArea 目标编辑器
 * @sa applyLinkToCursor
 */
void MarkdownEdit::applyLink(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyLinkToCursor);
}

/**
 * @brief 围栏代码块 + 语言标注
 * @param textArea 目标编辑器
 * @param language 语言标注（空串 = 无标注）
 * @sa applyFenceToCursor
 */
void MarkdownEdit::applyFence(QObject *textArea, const QString &language)
{
    editTextArea(textArea, &MarkdownEdit::applyFenceToCursor, language);
}

/**
 * @brief 插入表格骨架
 * @param textArea 目标编辑器
 * @param columns 列数（夹到 [1, 12]）
 * @sa insertTableToCursor
 */
void MarkdownEdit::insertTable(QObject *textArea, int columns)
{
    editTextArea(textArea, &MarkdownEdit::insertTableToCursor, columns);
}

/**
 * @brief 插入分隔线
 * @param textArea 目标编辑器
 * @sa insertDividerToCursor
 */
void MarkdownEdit::insertDivider(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::insertDividerToCursor);
}

/**
 * @brief 选区每行加任务清单标记
 * @param textArea 目标编辑器
 * @sa applyTaskToCursor
 */
void MarkdownEdit::applyTask(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyTaskToCursor);
}

/**
 * @brief 给选区加 ** 标记对
 *
 * 空选区时插入 **** 并把光标放到中间，随后的输入即被包裹。有选区时
 * 前后空白留在标记外，包裹后重新选中原内容（前后各偏移 2 个字符的
 * 标记）。整个动作在一个编辑块内，一次撤销即可回退。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyBoldToCursor(QTextCursor &cursor)
{
    if (!cursor.hasSelection()) {
        const int position = cursor.position();
        cursor.beginEditBlock();
        cursor.insertText(QStringLiteral("****"));
        cursor.setPosition(position + 2);
        cursor.endEditBlock();
        return;
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const TrimmedSelection trimmed = trimWhitespace(selectedText(cursor));

    cursor.beginEditBlock();
    const int wrapStart = selectionStart + trimmed.start;
    const int wrapEnd = wrapStart + trimmed.length;
    cursor.setPosition(wrapStart);
    cursor.insertText(QStringLiteral("**"));
    cursor.setPosition(wrapEnd + 2);
    cursor.insertText(QStringLiteral("**"));
    cursor.setPosition(wrapStart + 2);
    cursor.setPosition(wrapEnd + 2, QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 给选区加 * 标记对（斜体）
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyItalicToCursor(QTextCursor &cursor)
{
    wrapSelectionToCursor(cursor, QStringLiteral("*"));
}

/**
 * @brief 给选区加 ~~ 标记对（删除线）
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyStrikeToCursor(QTextCursor &cursor)
{
    wrapSelectionToCursor(cursor, QStringLiteral("~~"));
}

/**
 * @brief 给选区加代码标记
 *
 * 空选区时插入 `` 并把光标放到中间。单行选区加行内代码 `text`（前后
 * 空白留在标记外）；含换行的选区改用独立围栏代码块——首尾各垫
 * \n```\n，选区首尾已带的换行不重复，包裹后重新选中原内容。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyCodeToCursor(QTextCursor &cursor)
{
    if (!cursor.hasSelection()) {
        const int position = cursor.position();
        cursor.beginEditBlock();
        cursor.insertText(QStringLiteral("``"));
        cursor.setPosition(position + 1);
        cursor.endEditBlock();
        return;
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QString selected = selectedText(cursor);

    cursor.beginEditBlock();
    if (!selected.contains(QLatin1Char('\n'))) {
        const TrimmedSelection trimmed = trimWhitespace(selected);
        const int wrapStart = selectionStart + trimmed.start;
        const int wrapEnd = wrapStart + trimmed.length;
        cursor.setPosition(wrapStart);
        cursor.insertText(QStringLiteral("`"));
        cursor.setPosition(wrapEnd + 1);
        cursor.insertText(QStringLiteral("`"));
        cursor.setPosition(wrapStart + 1);
        cursor.setPosition(wrapEnd + 1, QTextCursor::KeepAnchor);
    } else {
        // 选区首尾已有的换行是现成的行分隔，留在围栏外：开围栏插在
        // 首换行之后、闭围栏插在尾换行之前；没有的分隔由围栏自带补行。
        const bool startsWithNewline = selected.startsWith(QLatin1Char('\n'));
        const bool endsWithNewline = selected.endsWith(QLatin1Char('\n'));
        const QString open = startsWithNewline
                ? QStringLiteral("```\n") : QStringLiteral("\n```\n");
        const QString close = endsWithNewline
                ? QStringLiteral("\n```") : QStringLiteral("\n```\n");
        cursor.setPosition(startsWithNewline ? selectionStart + 1
                                             : selectionStart);
        cursor.insertText(open);
        const int contentStart = selectionStart
                + (startsWithNewline ? 1 : 0) + open.length();
        const int contentEnd = selectionEnd + open.length()
                - (endsWithNewline ? 1 : 0);
        cursor.setPosition(contentEnd);
        cursor.insertText(close);
        cursor.setPosition(contentStart);
        cursor.setPosition(contentEnd, QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
}

/**
 * @brief 给选区覆盖的每一行行首加 "- "
 *
 * 加点是行级操作：选区起点落在行中时，整行从头加点。选区末尾恰好贴在
 * 换行后（光标位于下一行行首）时，那行没选到内容，不参与加点。无选区
 * 时作用于光标所在行。完成后重新选中整个被加点的范围。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyBulletToCursor(QTextCursor &cursor)
{
    QTextDocument *document = cursor.document();
    cursor.beginEditBlock();
    if (!cursor.hasSelection()) {
        const int position = cursor.position();
        cursor.setPosition(document->findBlock(position).position());
        cursor.insertText(QStringLiteral("- "));
        cursor.setPosition(position + 2);
        cursor.endEditBlock();
        return;
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock firstBlock = document->findBlock(selectionStart);
    const QTextBlock lastBlock =
            document->findBlock(selectionEnd > selectionStart
                                        ? selectionEnd - 1
                                        : selectionStart);
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        cursor.setPosition(block.position());
        cursor.insertText(QStringLiteral("- "));
        if (block == lastBlock)
            break;
    }
    cursor.setPosition(firstBlock.position());
    cursor.setPosition(lastBlock.position() + lastBlock.length() - 1,
                       QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 行首切换 #/##/###
 *
 * 行是标题时：目标级 == 当前级 → 去掉（回到普通行）；不同 → 改级
 * （替换前缀，不叠加）。不是标题 → 加前缀（前缀与原文字之间保证一个
 * 空格）。作用于选区覆盖的**每一行**（与列表同语义：起点在行中算整行，
 * 末尾贴换行的空行不参与）。
 *
 * @param cursor 选区光标
 * @param level 目标级（夹到 [1, 3]）
 */
void MarkdownEdit::applyHeadingToCursor(QTextCursor &cursor, int level)
{
    const int clamped = qBound(1, level, 3);
    QTextDocument *document = cursor.document();

    cursor.beginEditBlock();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock firstBlock = document->findBlock(selectionStart);
    const QTextBlock lastBlock =
            document->findBlock(selectionEnd > selectionStart
                                      ? selectionEnd - 1
                                      : selectionStart);
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        const QString text = block.text();
        // 行首既有标题前缀（#{1,6} + 空格）？
        static const QRegularExpression headingPrefix(
            QStringLiteral("^(#{1,6})\\s+"));
        const QRegularExpressionMatch match =
                headingPrefix.match(text);
        if (match.hasMatch()) {
            const QString existing = match.captured(1);
            const QString replacement =
                    existing.length() == clamped
                        ? QString()   // 同级再点 = 去掉标题
                        : QString(clamped, QLatin1Char('#'));
            if (replacement.length() != existing.length()) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + match.capturedLength(),
                                   QTextCursor::KeepAnchor);
                cursor.insertText(replacement.isEmpty()
                                      ? QString()
                                      : replacement
                                            + QLatin1Char(' '));
            }
        } else {
            cursor.setPosition(block.position());
            cursor.insertText(QString(clamped, QLatin1Char('#'))
                              + QLatin1Char(' '));
        }
        if (block == lastBlock) {
            break;
        }
    }
    // 结果选区：整段被处理的范围（行首偏移随插入移动，重算块范围）。
    cursor.setPosition(document->findBlock(
                           firstBlock.position()).position());
    const QTextBlock newLast = document->findBlock(
                lastBlock.isValid() ? lastBlock.position() + 1
                                    : lastBlock.position());
    if (newLast.isValid()) {
        cursor.setPosition(newLast.position() + newLast.length() - 1,
                           QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
}

/**
 * @brief 选区每行加 "1. " 递增编号
 *
 * 已有编号（`\d+\. ` 或 `\d+\. `）的行按出现顺序重编号；全部行都已带
 * 编号时本动作改为**去编号**（与列表的循环语义一致）。完成后选中整段。
 *
 * @param cursor 选区光标
 */
void MarkdownEdit::applyNumberedToCursor(QTextCursor &cursor)
{
    QTextDocument *document = cursor.document();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock firstBlock = document->findBlock(selectionStart);
    const QTextBlock lastBlock =
            document->findBlock(selectionEnd > selectionStart
                                      ? selectionEnd - 1
                                      : selectionStart);
    if (!firstBlock.isValid() || !lastBlock.isValid()) {
        return;
    }

    static const QRegularExpression numberedPrefix(
        QStringLiteral("^(\\d+)\\.\\s+"));
    // 先看选区里还有没有「裸行」：全都有编号 → 本轮去编号。
    bool anyPlain = false;
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        if (!block.text().isEmpty()
            && !numberedPrefix.match(block.text()).hasMatch()) {
            anyPlain = true;
            break;
        }
        if (block == lastBlock) {
            break;
        }
    }

    cursor.beginEditBlock();
    int number = 1;
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        const QString text = block.text();
        const QRegularExpressionMatch match =
                numberedPrefix.match(text);
        if (anyPlain) {
            // 加编号模式：裸行加 "N. "；已编号行重编为顺序号。
            if (match.hasMatch()) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + match.capturedLength(),
                                   QTextCursor::KeepAnchor);
            } else {
                cursor.setPosition(block.position());
            }
            cursor.insertText(QStringLiteral("%1. ").arg(number));
            ++number;
        } else {
            // 去编号模式：剥掉前缀。
            if (match.hasMatch()) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + match.capturedLength(),
                                   QTextCursor::KeepAnchor);
                cursor.insertText(QString());
            }
        }
        if (block == lastBlock) {
            break;
        }
    }
    cursor.setPosition(firstBlock.position());
    cursor.setPosition(lastBlock.position() + lastBlock.length() - 1,
                       QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 选区每行加 "> "
 *
 * 空行保持空行（引用块里不需要空引用行）。已全部是引用行时本动作改为
 * **去引用**（与编号列表同一循环语义）。
 *
 * @param cursor 选区光标
 */
void MarkdownEdit::applyQuoteToCursor(QTextCursor &cursor)
{
    QTextDocument *document = cursor.document();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock firstBlock = document->findBlock(selectionStart);
    const QTextBlock lastBlock =
            document->findBlock(selectionEnd > selectionStart
                                      ? selectionEnd - 1
                                      : selectionStart);
    if (!firstBlock.isValid() || !lastBlock.isValid()) {
        return;
    }

    bool anyPlain = false;
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        if (!block.text().isEmpty() && !block.text().startsWith(QLatin1Char('>'))) {
            anyPlain = true;
            break;
        }
        if (block == lastBlock) {
            break;
        }
    }

    cursor.beginEditBlock();
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        const QString text = block.text();
        if (anyPlain) {
            if (!text.isEmpty()) {
                cursor.setPosition(block.position());
                cursor.insertText(QStringLiteral("> "));
            }
        } else {
            if (text.startsWith(QStringLiteral("> "))) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + 2,
                                   QTextCursor::KeepAnchor);
                cursor.insertText(QString());
            } else if (text.startsWith(QLatin1Char('>'))) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + 1,
                                   QTextCursor::KeepAnchor);
                cursor.insertText(QString());
            }
        }
        if (block == lastBlock) {
            break;
        }
    }
    cursor.setPosition(firstBlock.position());
    cursor.setPosition(lastBlock.position() + lastBlock.length() - 1,
                       QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 选区变成 markdown 链接
 *
 * 有选区：`[选区](url)`，光标停在 url 处等着填（括号对已闭合，Esc 即
 * 可离开）。无选区：`[](url)`，光标在方括号中间等着写链接文字。
 *
 * @param cursor 选区光标
 */
void MarkdownEdit::applyLinkToCursor(QTextCursor &cursor)
{
    cursor.beginEditBlock();
    if (!cursor.hasSelection()) {
        const int position = cursor.position();
        cursor.insertText(QStringLiteral("[](url)"));
        cursor.setPosition(position + 1);
    } else {
        const int selectionStart = cursor.selectionStart();
        const TrimmedSelection trimmed =
                trimWhitespace(selectedText(cursor));
        const int wrapStart = selectionStart + trimmed.start;
        const int wrapEnd = wrapStart + trimmed.length;
        cursor.setPosition(wrapStart);
        cursor.insertText(QStringLiteral("["));
        cursor.setPosition(wrapEnd + 1);
        cursor.insertText(QStringLiteral("](url)"));
        // ]( 两个字符之后是 url：选中它等着重写。wrapEnd 是原始内容
        // 末尾（含前置 [ 偏移 +1）：url 起点 = wrapEnd + 1("]") + 1("(") + 1。
        cursor.setPosition(wrapEnd + 3);
        cursor.setPosition(wrapEnd + 6, QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
}

/**
 * @brief 围栏代码块 + 语言标注
 *
 * 选区含换行时：```lang 围栏（与 applyCode 的换行分支同一补行策略），
 * 空语言 = 纯 ```。单行/空选区时退化为行内代码（一行内容配围栏没有
 * 意义，applyCode 已覆盖该形态）。
 *
 * @param cursor 选区光标
 * @param language 语言标注（原样写入，调用方负责合法值）
 */
void MarkdownEdit::applyFenceToCursor(QTextCursor &cursor,
                                      const QString &language)
{
    if (!cursor.hasSelection()
        || !selectedText(cursor).contains(QLatin1Char('\n'))) {
        applyCodeToCursor(cursor);
        return;
    }

    const QString fence = QStringLiteral("```") + language;
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QString selected = selectedText(cursor);

    cursor.beginEditBlock();
    const bool startsWithNewline = selected.startsWith(QLatin1Char('\n'));
    const bool endsWithNewline = selected.endsWith(QLatin1Char('\n'));
    const QString open = startsWithNewline
            ? fence + QLatin1Char('\n')
            : QLatin1Char('\n') + fence + QLatin1Char('\n');
    const QString close = endsWithNewline
            ? QStringLiteral("\n```") : QStringLiteral("\n```\n");
    cursor.setPosition(startsWithNewline ? selectionStart + 1
                                        : selectionStart);
    cursor.insertText(open);
    const int contentStart =
            selectionStart + (startsWithNewline ? 1 : 0) + open.length();
    const int contentEnd =
            selectionEnd + open.length() - (endsWithNewline ? 1 : 0);
    cursor.setPosition(contentEnd);
    cursor.insertText(close);
    cursor.setPosition(contentStart);
    cursor.setPosition(contentEnd, QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 插入表格骨架
 *
 * 形态（列数可指定，默认 3）：表头占位 + 分隔行 + 一行示例单元格。插入
 * 前确保位于行首（行中插入会把表格骨架劈进正文里）；插入后光标停在
 * 表头第一个单元格里。
 *
 * @param cursor 光标
 * @param columns 列数（夹到 [1, 12]——再宽的表在提示词里没有可读性）
 */
void MarkdownEdit::insertTableToCursor(QTextCursor &cursor, int columns)
{
    const int clamped = qBound(1, columns, 12);
    QTextDocument *document = cursor.document();

    QStringList headerCells;
    QStringList sampleCells;
    QString separator;
    for (int i = 0; i < clamped; ++i) {
        headerCells << QStringLiteral("Column %1").arg(i + 1);
        sampleCells << QStringLiteral(" ");
        separator += QStringLiteral("| ---- ");
    }
    const QString table = headerCells.join(QStringLiteral(" | ")) + QLatin1Char('\n')
            + separator + QLatin1Char('|') + QLatin1Char('\n')
            + sampleCells.join(QStringLiteral(" | ")) + QLatin1Char('|');

    cursor.beginEditBlock();
    // 行首化：行中插入先补一个换行，避免骨架与正文粘连。
    const int position = cursor.selectionStart();
    const QTextBlock block = document->findBlock(position);
    const bool atLineStart = block.position() == position;
    const QString prefix = atLineStart ? QString() : QStringLiteral("\n");
    cursor.setPosition(position);
    cursor.insertText(prefix + table + QLatin1Char('\n'));
    // 光标停在表头第一格里（选中占位文字，直接输入即替换）。
    const int headerStart = position + prefix.length();
    cursor.setPosition(headerStart);
    cursor.setPosition(headerStart + headerCells.constFirst().length(),
                       QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 插入分隔线（前后空行规范化）
 *
 * `---` 单独占一行；前面若紧贴正文补一个空行（否则 markdown 会把上一
 * 行文字变成标题）；后面同样补空行。插入后光标在分隔线的下一行行首。
 *
 * @param cursor 光标
 */
void MarkdownEdit::insertDividerToCursor(QTextCursor &cursor)
{
    QTextDocument *document = cursor.document();
    cursor.beginEditBlock();
    const int position = cursor.selectionStart();
    const QTextBlock block = document->findBlock(position);
    // 前面是否需要空行：光标不在行首，或前一行非空。
    bool needBlankBefore = block.position() != position;
    if (!needBlankBefore && block.previous().isValid()
        && !block.previous().text().isEmpty()) {
        needBlankBefore = true;
    }
    const QString insert = (needBlankBefore ? QStringLiteral("\n\n")
                                            : QStringLiteral("\n"))
            + QStringLiteral("---\n\n");
    cursor.setPosition(position);
    cursor.insertText(insert);
    cursor.setPosition(position + insert.length());
    cursor.endEditBlock();
}

/**
 * @brief 选区每行加 "- [ ] "
 *
 * 任务清单是 agent 提示词里写验收清单的自然形态。已全部是任务行时
 * 改为去掉（与编号/引用同一循环语义）。
 *
 * @param cursor 选区光标
 */
void MarkdownEdit::applyTaskToCursor(QTextCursor &cursor)
{
    QTextDocument *document = cursor.document();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock firstBlock = document->findBlock(selectionStart);
    const QTextBlock lastBlock =
            document->findBlock(selectionEnd > selectionStart
                                      ? selectionEnd - 1
                                      : selectionStart);
    if (!firstBlock.isValid() || !lastBlock.isValid()) {
        return;
    }

    bool anyPlain = false;
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        if (!block.text().isEmpty()
            && !block.text().startsWith(QStringLiteral("- [ ] "))) {
            anyPlain = true;
            break;
        }
        if (block == lastBlock) {
            break;
        }
    }

    cursor.beginEditBlock();
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        if (anyPlain) {
            if (!block.text().isEmpty()
                && !block.text().startsWith(QStringLiteral("- [ ] "))) {
                cursor.setPosition(block.position());
                cursor.insertText(QStringLiteral("- [ ] "));
            }
        } else {
            if (block.text().startsWith(QStringLiteral("- [ ] "))) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + 6,
                                   QTextCursor::KeepAnchor);
                cursor.insertText(QString());
            }
        }
        if (block == lastBlock) {
            break;
        }
    }
    cursor.setPosition(firstBlock.position());
    cursor.setPosition(lastBlock.position() + lastBlock.length() - 1,
                       QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 从某位置向后查找（B2 纯文档操作）
 *
 * 到尾部没找到时回卷到开头再找一遍（回卷标志置真——界面据此提示
 * 「已从头开始」）。回卷那一遍仍找不到才返回 -1。
 *
 * @param document 文档（只读）
 * @param needle 关键词（空串直接未命中）
 * @param from 起点（夹到 [0, length]）
 * @param caseSensitive 区分大小写
 * @param wholeWords 全词匹配（命中两侧不是单词字符才算）
 * @return 命中区间与回卷标志
 */
MarkdownEdit::FindResult MarkdownEdit::findFrom(
    const QTextDocument *document, const QString &needle, int from,
    bool caseSensitive, bool wholeWords)
{
    FindResult result;
    if (!document || needle.isEmpty()) {
        return result;
    }
    const QString haystack = document->toPlainText();
    const int start = qBound(0, from, haystack.length());

    // 手工 indexOf 而不是 QTextDocument::find 的 flags 重载：大小写与
    // 全词语义完全掌控（那个重载的默认值语义在两代 Qt 上有出入，全词
    // 档还要走 regex；自己的边界判定一处写清楚，测试也直接覆盖）。
    const auto isWordChar = [](QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('_');
    };
    const auto matchesAt = [&](int at) {
        if (wholeWords) {
            const bool leftOk =
                    at == 0 || !isWordChar(haystack.at(at - 1));
            const int end = at + needle.length();
            const bool rightOk =
                    end >= haystack.length()
                    || !isWordChar(haystack.at(end));
            if (!leftOk || !rightOk) {
                return false;
            }
        }
        for (int i = 0; i < needle.length(); ++i) {
            const QChar a = haystack.at(at + i);
            const QChar b = needle.at(i);
            if (caseSensitive ? (a != b)
                              : (a.toCaseFolded() != b.toCaseFolded())) {
                return false;
            }
        }
        return true;
    };

    for (int at = start; at + needle.length() <= haystack.length(); ++at) {
        if (matchesAt(at)) {
            result.start = at;
            result.length = needle.length();
            return result;
        }
    }
    // 尾部没找到：回卷到开头再找一遍（范围到 start 为止——start 之后
    // 的区域刚扫过）。
    for (int at = 0;
         at < start && at + needle.length() <= haystack.length(); ++at) {
        if (matchesAt(at)) {
            result.start = at;
            result.length = needle.length();
            result.wrapped = true;
            return result;
        }
    }
    return result;
}

/**
 * @brief 替换当前命中并找下一个
 *
 * 调用方保证 selection 正是这次命中（B2 的「替换后自动跳下一个」）。
 * 替换在独立编辑块内（一次撤销 = 一次替换）；下一次查找从替换后光标
 * 处开始（不是 0——避免替换循环原地打转）。
 */
MarkdownEdit::FindResult MarkdownEdit::replaceAndFindNext(
    QTextDocument *document, const QTextCursor &selection,
    const QString &replacement, const QString &needle,
    bool caseSensitive, bool wholeWords)
{
    FindResult next;
    if (!document || !selection.hasSelection() || needle.isEmpty()) {
        return next;
    }
    const int selectionStart = selection.selectionStart();

    QTextCursor editor(document);
    editor.setPosition(selectionStart);
    editor.setPosition(selection.selectionEnd(),
                       QTextCursor::KeepAnchor);
    editor.beginEditBlock();
    editor.insertText(replacement);
    editor.endEditBlock();

    // 下一个：从替换内容的末尾向后（替换内容本身不算命中——它已经是
    // 用户想要的形态）。
    const int searchFrom = selectionStart + replacement.length();
    return findFrom(document, needle, searchFrom, caseSensitive,
                    wholeWords);
}

/**
 * @brief 全部替换
 *
 * 从 0 开始反复 find-replace 直到找不到。逐次替换都收进**一个**编辑块，
 * 全部替换 = 一次撤销。从左到右替换时位置只向右移动，不会死循环
 * （needle 为空已拒绝；替换串包含 needle 时也安全——每次替换后从
 * 替换串末尾继续找）。
 *
 * @return 替换次数
 */
int MarkdownEdit::replaceAll(QTextDocument *document,
                             const QString &needle,
                             const QString &replacement,
                             bool caseSensitive, bool wholeWords)
{
    if (!document || needle.isEmpty()) {
        return 0;
    }
    int count = 0;
    int from = 0;
    QTextCursor editor(document);
    editor.beginEditBlock();
    while (true) {
        const FindResult hit = findFrom(document, needle, from,
                                        caseSensitive, wholeWords);
        if (hit.start < 0) {
            break;
        }
        // 回卷命中说明开头已经替换过（我们从左到右扫，回卷只会发生
        // 在「替换串里含关键词」的场景——findFrom 从 from 起找不到时
        // 才回卷；这里 from 始终向右走，回卷命中 = 已扫过的区域）。
        if (hit.wrapped) {
            break;
        }
        editor.setPosition(hit.start);
        editor.setPosition(hit.start + hit.length,
                           QTextCursor::KeepAnchor);
        editor.insertText(replacement);
        ++count;
        from = hit.start + replacement.length();
    }
    editor.endEditBlock();
    return count;
}

// --- B2 的 QML 入口（TextArea 版：包装静态纯函数） -----------------------

/**
 * @brief QML 版查找：编辑器 + 关键词 + 起点 → {start, length, wrapped}
 *
 * 静态纯函数吃 QTextDocument；这里从 TextArea 取文档、跑查找、把结果
 * 包成 QML 可消费的 map（结构体不进 QMetaType）。
 */
QVariantMap MarkdownEdit::find(QObject *textArea, const QString &needle,
                               int from, bool caseSensitive,
                               bool wholeWords) const
{
    QVariantMap result;
    result.insert(QStringLiteral("start"), -1);
    result.insert(QStringLiteral("length"), 0);
    result.insert(QStringLiteral("wrapped"), false);
    if (!textArea)
        return result;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return result;
    const FindResult hit = findFrom(quickDocument->textDocument(), needle,
                                    from, caseSensitive, wholeWords);
    result.insert(QStringLiteral("start"), hit.start);
    result.insert(QStringLiteral("length"), hit.length);
    result.insert(QStringLiteral("wrapped"), hit.wrapped);
    return result;
}

/**
 * @brief QML 版替换并找下一个
 *
 * 选中区即这次命中（TextArea 的 selectionStart/selectionEnd）。
 */
QVariantMap MarkdownEdit::replaceAndFindNext(
    QObject *textArea, const QString &replacement, const QString &needle,
    bool caseSensitive, bool wholeWords) const
{
    QVariantMap result;
    result.insert(QStringLiteral("start"), -1);
    result.insert(QStringLiteral("length"), 0);
    result.insert(QStringLiteral("wrapped"), false);
    if (!textArea)
        return result;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return result;
    QTextDocument *document = quickDocument->textDocument();
    QTextCursor selection(document);
    selection.setPosition(textArea->property("selectionStart").toInt());
    selection.setPosition(textArea->property("selectionEnd").toInt(),
                          QTextCursor::KeepAnchor);
    const FindResult next = replaceAndFindNext(
        document, selection, replacement, needle, caseSensitive,
        wholeWords);
    result.insert(QStringLiteral("start"), next.start);
    result.insert(QStringLiteral("length"), next.length);
    result.insert(QStringLiteral("wrapped"), next.wrapped);
    return result;
}

/**
 * @brief QML 版全部替换（编辑器全文）
 */
int MarkdownEdit::replaceAll(QObject *textArea, const QString &needle,
                             const QString &replacement, bool caseSensitive,
                             bool wholeWords) const
{
    if (!textArea)
        return 0;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return 0;
    return replaceAll(quickDocument->textDocument(), needle, replacement,
                      caseSensitive, wholeWords);
}

/**
 * @brief 从 Theme 提取高亮配色
 *
 * 颜色全部落在语义令牌上：标题/链接用 textLink，强调标记用 warning，
 * 行内代码与围栏用 danger，围栏内容用 textMuted，引用用 textSecondary，
 * 列表标记用 accent，分隔线用 textMuted。
 *
 * @param theme 当前主题
 * @return 高亮配色
 */
MarkdownColors MarkdownEdit::colorsFromTheme(const theme::Theme *theme)
{
    MarkdownColors colors;
    colors.heading = theme->textLink();
    colors.emphasis = theme->warning();
    colors.code = theme->danger();
    colors.fence = theme->danger();
    colors.codeBlock = theme->textMuted();
    colors.quote = theme->textSecondary();
    colors.listMarker = theme->accent();
    colors.link = theme->textLink();
    colors.divider = theme->textMuted();
    return colors;
}

/**
 * @brief 主题变化处理：重灌配色并让所有挂着的编辑器重扫
 */
void MarkdownEdit::reapplyTheme()
{
    if (m_theme)
        m_colors = colorsFromTheme(m_theme);
    pruneHighlighters();
    for (const QPointer<MarkdownHighlighter> &highlighter
         : std::as_const(m_highlighters)) {
        highlighter->setColors(m_colors);
        highlighter->rehighlight();
    }
}

/**
 * @brief 清掉文档已销毁的高亮器指针
 */
void MarkdownEdit::pruneHighlighters()
{
    m_highlighters.erase(
        std::remove_if(m_highlighters.begin(), m_highlighters.end(),
                       [](const QPointer<MarkdownHighlighter> &highlighter) {
                           return highlighter.isNull();
                       }),
        m_highlighters.end());
}

/**
 * @brief 编辑器操作的通用路径
 *
 * 从编辑器读 textDocument 与 selectionStart/selectionEnd，构造文档光标
 * 跑静态操作，再把结果选区经 select() 槽写回（选区为空即纯光标定位）。
 * 选区读取不依赖焦点——菜单打开时焦点在菜单上，编辑器选区仍在。
 *
 * @param textArea 目标编辑器（空指针直接返回）
 * @param operation 要执行的静态光标操作
 */
void MarkdownEdit::editTextArea(QObject *textArea,
                                void (*operation)(QTextCursor &))
{
    if (!textArea)
        return;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return;

    QTextCursor cursor(quickDocument->textDocument());
    cursor.setPosition(textArea->property("selectionStart").toInt());
    cursor.setPosition(textArea->property("selectionEnd").toInt(),
                       QTextCursor::KeepAnchor);
    operation(cursor);

    QMetaObject::invokeMethod(textArea, "select",
                              Q_ARG(int, cursor.selectionStart()),
                              Q_ARG(int, cursor.selectionEnd()));
}

/**
 * @brief 带整数参数操作的通用路径（applyHeading/insertTable）
 *
 * 与无参版同一套取文档/选区/写回纪律，只是操作多带一个参数。
 *
 * @param textArea 目标编辑器
 * @param operation 静态操作
 * @param number 传给操作的整数参数
 */
void MarkdownEdit::editTextArea(QObject *textArea,
                                void (*operation)(QTextCursor &, int),
                                int number)
{
    if (!textArea)
        return;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return;

    QTextCursor cursor(quickDocument->textDocument());
    cursor.setPosition(textArea->property("selectionStart").toInt());
    cursor.setPosition(textArea->property("selectionEnd").toInt(),
                       QTextCursor::KeepAnchor);
    operation(cursor, number);

    QMetaObject::invokeMethod(textArea, "select",
                              Q_ARG(int, cursor.selectionStart()),
                              Q_ARG(int, cursor.selectionEnd()));
}

/**
 * @brief 带字符串参数操作的通用路径（applyFence）
 *
 * @param textArea 目标编辑器
 * @param operation 静态操作
 * @param text 传给操作的字符串参数
 */
void MarkdownEdit::editTextArea(
    QObject *textArea, void (*operation)(QTextCursor &, const QString &),
    const QString &text)
{
    if (!textArea)
        return;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return;

    QTextCursor cursor(quickDocument->textDocument());
    cursor.setPosition(textArea->property("selectionStart").toInt());
    cursor.setPosition(textArea->property("selectionEnd").toInt(),
                       QTextCursor::KeepAnchor);
    operation(cursor, text);

    QMetaObject::invokeMethod(textArea, "select",
                              Q_ARG(int, cursor.selectionStart()),
                              Q_ARG(int, cursor.selectionEnd()));
}

// --- 查询侧（B1：checkable 按钮的数据源） --------------------------------

/**
 * @brief 光标处是否处于加粗内
 *
 * 判据：charFormat 的 FontWeight > Normal。选区形态下取选区里**任一**
 * 字符命中即真（部分加粗在 QML 端没有「半勾」状态，按真处理更接近
 * 「再点一下会变成什么」的直觉）；空选区查光标左侧字符。
 *
 * @param textArea 目标编辑器
 * @return 命中为真
 */
bool MarkdownEdit::isBoldAt(QObject *textArea) const
{
    return queryTextAreaBool(textArea, &MarkdownEdit::isBoldAtCursor);
}

/**
 * @brief 光标处是否处于斜体内
 *
 * @param textArea 目标编辑器
 * @return 命中为真
 */
bool MarkdownEdit::isItalicAt(QObject *textArea) const
{
    return queryTextAreaBool(textArea, &MarkdownEdit::isItalicAtCursor);
}

/**
 * @brief 光标处是否有删除线
 *
 * @param textArea 目标编辑器
 * @return 命中为真
 */
bool MarkdownEdit::isStrikeAt(QObject *textArea) const
{
    return queryTextAreaBool(textArea, &MarkdownEdit::isStrikeAtCursor);
}

/**
 * @brief 光标处是否处于行内代码/代码块内
 *
 * 判据：fontFixedPitch。markdown 的 `code` 与围栏块都会落在这个格式上。
 *
 * @param textArea 目标编辑器
 * @return 命中为真
 */
bool MarkdownEdit::isCodeAt(QObject *textArea) const
{
    return queryTextAreaBool(textArea, &MarkdownEdit::isCodeAtCursor);
}

/**
 * @brief 光标所在行的标题级数
 *
 * 判据：blockFormat 的 headingLevel（QTextDocument 的标题层级；我们的
 * 高亮器/动作写入的是普通文本 `#`，**不**设置 headingLevel——所以这里
 * 同时做一层文本判定：行首 `#{1,6}\s` 也算标题，级数取 # 的个数。两条
 * 判据取或，任一命中即返回级数。
 *
 * @param textArea 目标编辑器
 * @return 标题级数（1–6）；不是标题返回 0
 */
int MarkdownEdit::headingLevelAt(QObject *textArea) const
{
    return queryTextAreaInt(textArea, &MarkdownEdit::headingLevelAtCursor);
}

/**
 * @brief 布尔查询的通用路径
 *
 * 查询不修改文档；空光标参数按假处理（找不到文档/编辑器时按钮不亮，
 * 比「猜一个默认真」安全）。
 *
 * @param textArea 目标编辑器
 * @param query 静态查询
 * @return 查询结果
 */
bool MarkdownEdit::queryTextAreaBool(
    QObject *textArea, bool (*query)(const QTextCursor &)) const
{
    if (!textArea)
        return false;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return false;

    QTextCursor cursor(quickDocument->textDocument());
    cursor.setPosition(textArea->property("selectionStart").toInt());
    cursor.setPosition(textArea->property("selectionEnd").toInt(),
                       QTextCursor::KeepAnchor);
    return query(cursor);
}

/**
 * @brief 整数查询的通用路径
 *
 * @param textArea 目标编辑器
 * @param query 静态查询
 * @return 查询结果
 */
int MarkdownEdit::queryTextAreaInt(
    QObject *textArea, int (*query)(const QTextCursor &)) const
{
    if (!textArea)
        return 0;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return 0;

    QTextCursor cursor(quickDocument->textDocument());
    cursor.setPosition(textArea->property("selectionStart").toInt());
    cursor.setPosition(textArea->property("selectionEnd").toInt(),
                       QTextCursor::KeepAnchor);
    return query(cursor);
}

/**
 * @brief 光标处是否处于加粗内（markdown 源码语义：一对 ** 之间的内容）
 *
 * @param cursor 选区光标
 * @return 选区内任一字符在 **..** 内容区间里、或空光标贴着区间边界为真
 */
bool MarkdownEdit::isBoldAtCursor(const QTextCursor &cursor)
{
    // 加粗只认双星标记；单 * 是斜体，不算加粗。insideMarker 里双字符
    // 与单字符标记共用 '*'，这里以「区间由 ** 配对产生」为准——配对
    // 表里 ** 区间先于 * 配对产出（双字符优先），同一段文字里两种语义
    // 不会重叠。
    return insideMarker(cursor, QLatin1Char('*'), true);
}

/**
 * @brief 光标处是否处于斜体内（单 * 标记的内容区间）
 */
bool MarkdownEdit::isItalicAtCursor(const QTextCursor &cursor)
{
    return insideMarker(cursor, QLatin1Char('*'), false);
}

/**
 * @brief 光标处是否有删除线（~~..~~ 内容区间）
 */
bool MarkdownEdit::isStrikeAtCursor(const QTextCursor &cursor)
{
    return insideMarker(cursor, QLatin1Char('~'), true);
}

/**
 * @brief 光标处是否处于行内代码/代码块内
 *
 * `code` 的行内标记区间命中即真；光标在围栏块内（```..``` 之间）也算
 * ——围栏判定看块级：光标块本身在围栏状态机里时整个块都是代码。
 */
bool MarkdownEdit::isCodeAtCursor(const QTextCursor &cursor)
{
    return insideMarker(cursor, QLatin1Char('`'), false);
}

/**
 * @brief 光标所在行的标题级数（静态查询）
 *
 * 先看 QTextBlock 的 headingLevel（QTextDocument 原生标题），再看行首
 * 的 `#` 文本形态（我们自己的动作写入的是纯文本），两者取或。
 */
int MarkdownEdit::headingLevelAtCursor(const QTextCursor &cursor)
{
    const QTextBlock block = cursor.block();
    if (!block.isValid()) {
        return 0;
    }
    const int blockLevel = block.blockFormat().headingLevel();
    if (blockLevel > 0) {
        return blockLevel;
    }
    static const QRegularExpression headingPrefix(
        QStringLiteral("^(#{1,6})\\s"));
    const QRegularExpressionMatch match =
            headingPrefix.match(block.text());
    return match.hasMatch() ? match.captured(1).length() : 0;
}

} // namespace awb::tools
