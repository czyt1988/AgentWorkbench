#include "tools/MarkdownEdit.h"

#include "theme/Theme.h"

#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariant>

#include <algorithm>
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

} // namespace awb::tools
