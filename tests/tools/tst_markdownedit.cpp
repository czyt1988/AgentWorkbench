#include "awbtest.h"

#include "tools/MarkdownEdit.h"
#include "tools/MarkdownHighlighter.h"

#include <QColor>
#include <QFont>
#include <QString>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

using awb::tools::MarkdownColors;
using awb::tools::MarkdownEdit;

namespace {

// 每个角色一组独立的 RGB，让 QCOMPARE 能区分「染错角色」的情况。
MarkdownColors distinctColors()
{
    MarkdownColors colors;
    colors.heading = QColor(1, 2, 3);
    colors.emphasis = QColor(4, 5, 6);
    colors.code = QColor(7, 8, 9);
    colors.fence = QColor(10, 11, 12);
    colors.codeBlock = QColor(13, 14, 15);
    colors.quote = QColor(16, 17, 18);
    colors.listMarker = QColor(19, 20, 21);
    colors.link = QColor(22, 23, 24);
    colors.divider = QColor(25, 26, 27);
    return colors;
}

/**
 * @brief 取块内某位置命中的语法高亮区间
 *
 * @param block 文本块
 * @param position 块内偏移
 * @return 命中的区间；没有命中时 length 为 0
 */
QTextLayout::FormatRange formatAt(const QTextBlock &block, int position)
{
    const auto formats = block.layout()->formats();
    for (const QTextLayout::FormatRange &range : formats) {
        if (position >= range.start && position < range.start + range.length)
            return range;
    }
    return QTextLayout::FormatRange();
}

/**
 * @brief 把文档设为 text、选中 [start, end) 并执行一个光标操作
 *
 * 光标操作改文档也改光标，二者都活着才有断言意义，所以经调用方声明的
 * document / cursor 出参，而不是返回指向局部对象的任何东西。
 *
 * @param text 文档初始内容
 * @param start 选区起点
 * @param end 选区终点
 * @param operation 要执行的静态操作
 * @param document 承载文档（调用方作用域存活）
 * @param cursor 操作后的光标（位置/选区即动作结果）
 */
void apply(const QString &text, int start, int end,
           void (*operation)(QTextCursor &), QTextDocument *document,
           QTextCursor *cursor)
{
    document->setPlainText(text);
    *cursor = QTextCursor(document);
    cursor->setPosition(start);
    cursor->setPosition(end, QTextCursor::KeepAnchor);
    operation(*cursor);
}

/**
 * @brief 归一化 selectedText 的换行
 *
 * QTextCursor::selectedText() 把换行返回为 U+2020 段落分隔符（即 ·），
 * 断言前换回 \n。
 *
 * @param cursor 持有选区的光标
 * @return 换行归一化后的选区文本
 */
QString normalizedSelection(const QTextCursor &cursor)
{
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

} // namespace

// 右键菜单工具栏三个格式化动作的光标语义（MarkdownEdit 的静态操作）。
class TestMarkdownEdit : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testBoldWrapsSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello world"), 0, 5,
              &MarkdownEdit::applyBoldToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("**hello** world"));
        QCOMPARE(cursor.selectionStart(), 2);
        QCOMPARE(cursor.selectionEnd(), 7);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("hello"));
    }

    void testBoldTrimsWhitespace()
    {
        // 标记对要贴着文字：选区两端的空白留在 ** 外面。
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("xx word yy"), 2, 8,
              &MarkdownEdit::applyBoldToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("xx **word** yy"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("word"));
    }

    void testBoldWithoutSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello"), 2, 2,
              &MarkdownEdit::applyBoldToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("he****llo"));
        QCOMPARE(cursor.position(), 4);
        QVERIFY(!cursor.hasSelection());
    }

    void testCodeSingleLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("use foo here"), 4, 7,
              &MarkdownEdit::applyCodeToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("use `foo` here"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("foo"));
    }

    void testCodeMultilineBecomesFence()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("a\nbc"), 0, 4,
              &MarkdownEdit::applyCodeToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("\n```\na\nbc\n```\n"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("a\nbc"));
    }

    void testCodeMultilineKeepsExistingNewlines()
    {
        // 选区首尾已带换行时它们留在围栏外当行分隔，不补换行不出空行。
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("x\na\nb\ny"), 1, 6,
              &MarkdownEdit::applyCodeToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("x\n```\na\nb\n```\ny"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("a\nb"));
    }

    void testCodeWithoutSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("ab"), 1, 1,
              &MarkdownEdit::applyCodeToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("a``b"));
        QCOMPARE(cursor.position(), 2);
    }

    void testBulletWithoutSelectionUsesCurrentLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello"), 2, 2,
              &MarkdownEdit::applyBulletToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("- hello"));
        QCOMPARE(cursor.position(), 4);
    }

    void testBulletSelectionCoversEveryLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb\ncc"), 0, 5,
              &MarkdownEdit::applyBulletToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("- aa\n- bb\ncc"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("- aa\n- bb"));
    }

    void testBulletSelectionEndingAtLineStart()
    {
        // 选区末尾贴着换行（光标在下一行行首）时，那行没选到内容，不加点。
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 0, 3,
              &MarkdownEdit::applyBulletToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("- aa\nbb"));
    }

    void testBoldIsOneUndoStep()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("hello world"));
        QTextCursor cursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        MarkdownEdit::applyBoldToCursor(cursor);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("hello world"));
    }
};

// Markdown 高亮器：围栏状态机、整行规则、行内规则与占位区间。
class TestMarkdownHighlighter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testHeading()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("# Title"));
        awb::tools::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();

        const QTextLayout::FormatRange range =
                formatAt(document.firstBlock(), 1);
        QVERIFY(range.length > 0);
        QCOMPARE(range.format.foreground().color(), colors.heading);
        QCOMPARE(range.format.fontWeight(), static_cast<int>(QFont::Bold));
    }

    void testHeadingNeedsSpace()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("#nospace"));
        awb::tools::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();
        QCOMPARE(formatAt(document.firstBlock(), 0).length, 0);
    }

    void testFencedCodeBlock()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("```cpp\nint x;\n```\nplain"));
        awb::tools::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();

        const QTextBlock fenceOpen = document.firstBlock();
        QCOMPARE(formatAt(fenceOpen, 0).format.foreground().color(), colors.fence);
        const QTextBlock content = fenceOpen.next();
        QCOMPARE(formatAt(content, 0).format.foreground().color(),
                 colors.codeBlock);
        const QTextBlock fenceClose = content.next();
        QCOMPARE(formatAt(fenceClose, 0).format.foreground().color(),
                 colors.fence);
        const QTextBlock plain = fenceClose.next();
        QCOMPARE(formatAt(plain, 0).length, 0);
    }

    void testQuoteDividerAndList()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("> quoted\n---\n- item"));
        awb::tools::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();

        const QTextBlock quote = document.firstBlock();
        QCOMPARE(formatAt(quote, 1).format.foreground().color(), colors.quote);
        QVERIFY(formatAt(quote, 1).format.fontItalic());
        const QTextBlock divider = quote.next();
        QCOMPARE(formatAt(divider, 0).format.foreground().color(), colors.divider);
        const QTextBlock list = divider.next();
        QCOMPARE(formatAt(list, 0).format.foreground().color(), colors.listMarker);
        QCOMPARE(formatAt(list, 2).length, 0);
    }

    void testInlineRules()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("**bold** `code` *it* [x](y)"));
        awb::tools::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        // 粗体标记对：强调色（且自带粗体属性）；内部：加粗。
        QCOMPARE(formatAt(block, 0).format.foreground().color(), colors.emphasis);
        QCOMPARE(formatAt(block, 0).format.fontWeight(),
                 static_cast<int>(QFont::Bold));
        QCOMPARE(formatAt(block, 2).format.fontWeight(),
                 static_cast<int>(QFont::Bold));
        // 行内代码整体一色。
        QCOMPARE(formatAt(block, 10).format.foreground().color(), colors.code);
        // 斜体内部斜体。
        QVERIFY(formatAt(block, 17).format.fontItalic());
        // 链接整体链接色 + 下划线。
        QCOMPARE(formatAt(block, 22).format.foreground().color(), colors.link);
        QVERIFY(formatAt(block, 22).format.fontUnderline());
    }

    void testCodeSpansAreAtomic()
    {
        // 行内代码内部不再套其它行内规则：星号保持普通文本。
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("`**q**`"));
        awb::tools::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        QCOMPARE(formatAt(block, 3).format.foreground().color(), colors.code);
        QCOMPARE(formatAt(block, 3).format.fontWeight(),
                 static_cast<int>(QFont::Normal));
    }

    void testBoldInnerContent()
    {
        // **q** 的内部 * 不构成斜体：整个区间被粗体占用。
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("**q**"));
        awb::tools::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        QVERIFY(!formatAt(block, 2).format.fontItalic());
        QCOMPARE(formatAt(block, 2).format.fontWeight(),
                 static_cast<int>(QFont::Bold));
    }

    void testColorSwapRehighlight()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("`code`"));
        awb::tools::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();
        QCOMPARE(formatAt(document.firstBlock(), 1).format.foreground().color(),
                 colors.code);

        MarkdownColors swapped = colors;
        swapped.code = QColor(28, 29, 30);
        highlighter.setColors(swapped);
        highlighter.rehighlight();
        QCOMPARE(formatAt(document.firstBlock(), 1).format.foreground().color(),
                 swapped.code);
    }
};

AWB_TEST(TestMarkdownEdit)
AWB_TEST(TestMarkdownHighlighter)
#include "tst_markdownedit.moc"
