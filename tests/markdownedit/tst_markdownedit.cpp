#include "awbtest.h"

#include "markdownedit/MarkdownEdit.h"
#include "markdownedit/MarkdownHighlighter.h"

#include <QColor>
#include <QFont>
#include <QString>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

using awb::markdownedit::IndentOptions;
using awb::markdownedit::MarkdownColors;
using awb::markdownedit::MarkdownEdit;

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
    colors.search = QColor(28, 29, 30);
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
 * @brief apply 的缩进偏好重载（handleTab/handleBacktabToCursor 专用）
 *
 * @param text 文档初始内容
 * @param start 选区起点
 * @param end 选区终点
 * @param operation 缩进静态操作
 * @param options 缩进偏好
 * @param document 承载文档（调用方作用域存活）
 * @param cursor 操作后的光标（位置/选区即动作结果）
 */
void apply(const QString &text, int start, int end,
           void (*operation)(QTextCursor &, const IndentOptions &),
           const IndentOptions &options, QTextDocument *document,
           QTextCursor *cursor)
{
    document->setPlainText(text);
    *cursor = QTextCursor(document);
    cursor->setPosition(start);
    cursor->setPosition(end, QTextCursor::KeepAnchor);
    operation(*cursor, options);
}

/// \t 模式（旧行为基准）：每级一个 \t，反缩进按 4 空格宽退。
IndentOptions tabOptions()
{
    IndentOptions options;
    options.useSpaces = false;
    options.spacesPerIndent = 4;
    return options;
}

/// 空格模式：每级 2 空格（markdown 嵌套列表的默认形态）。
IndentOptions spaceOptions()
{
    IndentOptions options;
    options.useSpaces = true;
    options.spacesPerIndent = 2;
    return options;
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

    // --- 斜体与删除线（B1：与加粗同一形状、不同标记对） ---------------------

    void testItalicWrapsSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello world"), 0, 5,
              &MarkdownEdit::applyItalicToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("*hello* world"));
        QCOMPARE(cursor.selectionStart(), 1);
        QCOMPARE(cursor.selectionEnd(), 6);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("hello"));
    }

    void testItalicTrimsWhitespace()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("xx word yy"), 2, 8,
              &MarkdownEdit::applyItalicToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("xx *word* yy"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("word"));
    }

    void testItalicWithoutSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello"), 2, 2,
              &MarkdownEdit::applyItalicToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("he**llo"));
        QCOMPARE(cursor.position(), 3);
        QVERIFY(!cursor.hasSelection());
    }

    void testStrikeWrapsSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello world"), 6, 11,
              &MarkdownEdit::applyStrikeToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("hello ~~world~~"));
        QCOMPARE(cursor.selectionStart(), 8);
        QCOMPARE(cursor.selectionEnd(), 13);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("world"));
    }

    void testStrikeTrimsWhitespace()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("xx word yy"), 2, 8,
              &MarkdownEdit::applyStrikeToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("xx ~~word~~ yy"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("word"));
    }

    void testStrikeWithoutSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello"), 2, 2,
              &MarkdownEdit::applyStrikeToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("he~~~~llo"));
        QCOMPARE(cursor.position(), 4);
        QVERIFY(!cursor.hasSelection());
    }

    void testUnderlineWrapsSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello world"), 6, 11,
              &MarkdownEdit::applyUnderlineToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("hello <u>world</u>"));
        QCOMPARE(cursor.selectionStart(), 9);
        QCOMPARE(cursor.selectionEnd(), 14);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("world"));
    }

    void testUnderlineTrimsWhitespace()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("xx word yy"), 2, 8,
              &MarkdownEdit::applyUnderlineToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("xx <u>word</u> yy"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("word"));
    }

    void testUnderlineWithoutSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello"), 2, 2,
              &MarkdownEdit::applyUnderlineToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("he<u></u>llo"));
        QCOMPARE(cursor.position(), 5);
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

    // --- M3 新动作（B1）--------------------------------------------------

    void testItalicIsOneUndoStep()
    {
        // 标记对在一个编辑块内插入：一次 undo 完整回退（不留下半对）。
        QTextDocument document;
        document.setPlainText(QStringLiteral("hello world"));
        QTextCursor cursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        MarkdownEdit::applyItalicToCursor(cursor);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("hello world"));
    }

    void testStrikeIsOneUndoStep()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("hello world"));
        QTextCursor cursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        MarkdownEdit::applyStrikeToCursor(cursor);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("hello world"));
    }

    void testHeadingAddsPrefix()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("plain"), 2, 2,
              [](QTextCursor &c) {
                  MarkdownEdit::applyHeadingToCursor(c, 2);
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("## plain"));
    }

    void testHeadingReplacesExistingLevel()
    {
        // 已是标题：改级不叠加（## → ###）。
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("## old"), 1, 1,
              [](QTextCursor &c) {
                  MarkdownEdit::applyHeadingToCursor(c, 3);
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("### old"));
    }

    void testHeadingSameLevelRemoves()
    {
        // 同级再点 = 去标题（循环语义与列表一致）。
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("### old"), 0, 0,
              [](QTextCursor &c) {
                  MarkdownEdit::applyHeadingToCursor(c, 3);
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("old"));
    }

    void testNumberedAddsIncrementing()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 0, 5,
              &MarkdownEdit::applyNumberedToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("1. aa\n2. bb"));
    }

    void testNumberedRemovesWhenAllNumbered()
    {
        // 全部已编号 → 本轮去编号。
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("1. aa\n2. bb"), 0, 11,
              &MarkdownEdit::applyNumberedToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
    }

    void testQuoteAddsAndSkipsEmptyLines()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\n\nbb"), 0, 6,
              &MarkdownEdit::applyQuoteToCursor, &document, &cursor);
        // 空行保持空行。
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("> aa\n\n> bb"));
    }

    void testQuoteRemovesWhenAllQuoted()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("> aa\n> bb"), 0, 9,
              &MarkdownEdit::applyQuoteToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
    }

    void testLinkWithSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("see docs here"), 4, 8,
              &MarkdownEdit::applyLinkToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("see [docs](url) here"));
        // 光标选区落在 url 上（等着重写）。
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("url"));
    }

    void testLinkWithoutSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("go"), 2, 2,
              &MarkdownEdit::applyLinkToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("go[](url)"));
        // 光标在方括号中间等着写链接文字（[ 是 index 2，中间 = 3）。
        QCOMPARE(cursor.position(), 3);
        QVERIFY(!cursor.hasSelection());
    }

    void testFenceWithLanguage()
    {
        // 整行围栏：开围栏插在首行行首、闭围栏补在末行行尾，选中的行整体
        // 变成代码块内容（不再前置一个空行——首行行首插即围栏行）。
        QTextDocument document;
        QTextCursor cursor;
        document.setPlainText(QStringLiteral("a\nbc"));
        cursor = QTextCursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        MarkdownEdit::applyFenceToCursor(cursor, QStringLiteral("cpp"));
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("```cpp\na\nbc\n```"));
        // 结果选区 = 围栏之间的内容行。
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("a\nbc"));
    }

    void testFenceSingleLineAlsoFences()
    {
        // 单行选区同样是围栏：一行内容配围栏是常见形态（把一行命令标成
        // bash 块），不再退化为行内代码（2026-10 用户报告：代码块按钮与
        // 行内代码没有区别）。
        QTextDocument document;
        QTextCursor cursor;
        document.setPlainText(QStringLiteral("foo bar"));
        cursor = QTextCursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(3, QTextCursor::KeepAnchor);
        MarkdownEdit::applyFenceToCursor(cursor, QStringLiteral("cpp"));
        QCOMPARE(document.toPlainText(), QStringLiteral("```cpp\nfoo bar\n```"));
    }

    void testFenceCursorOnEmptyLineInsertsSkeleton()
    {
        // 空选区 + 空行：围栏骨架，光标停在两道围栏之间等着写内容。
        QTextDocument document;
        QTextCursor cursor;
        document.setPlainText(QStringLiteral("x\n\ny"));
        cursor = QTextCursor(&document);
        cursor.setPosition(2);
        MarkdownEdit::applyFenceToCursor(cursor, QStringLiteral("bash"));
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("x\n```bash\n\n```\ny"));
        QCOMPARE(cursor.position(), 2 + QStringLiteral("```bash\n").length());
        QVERIFY(!cursor.hasSelection());
    }

    void testFenceCursorMidLineFencesWholeLine()
    {
        // 空选区落在有内容的行上：整行围栏（行首插开围栏、行尾补闭围栏）。
        QTextDocument document;
        QTextCursor cursor;
        document.setPlainText(QStringLiteral("hello"));
        cursor = QTextCursor(&document);
        cursor.setPosition(2);
        MarkdownEdit::applyFenceToCursor(cursor, QString());
        QCOMPARE(document.toPlainText(), QStringLiteral("```\nhello\n```"));
        QCOMPARE(cursor.selectedText(), QStringLiteral("hello"));
    }

    void testInsertTableSkeleton()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("x"), 1, 1,
              [](QTextCursor &c) {
                  MarkdownEdit::insertTableToCursor(c, 3);
              }, &document, &cursor);
        // 行中插入先补行；骨架 = 表头 + 分隔 + 一行示例。
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("x\nColumn 1 | Column 2 | Column 3\n"
                                "| ---- | ---- | ---- |\n"
                                "  |   |  |\n"));
        // 光标选中表头第一格的占位文字。
        QCOMPARE(normalizedSelection(cursor),
                 QStringLiteral("Column 1"));
    }

    void testInsertDividerNormalizesBlanks()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 行中 + 前一行有正文：两侧都补空行。
        apply(QStringLiteral("text\nhere"), 5, 5,
              &MarkdownEdit::insertDividerToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("text\n\n\n---\n\nhere"));
    }

    void testInsertDividerAtEmptyDocument()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral(""), 0, 0,
              &MarkdownEdit::insertDividerToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("\n---\n\n"));
    }

    // --- 通用插入（图片语法与模板落地共用） -------------------------------

    void testInsertMarkdownAtPosition()
    {
        QTextDocument document;
        QTextCursor cursor;
        const QString image = QStringLiteral("![a](a.png)");
        apply(QStringLiteral("hello world"), 5, 5,
              [](QTextCursor &c) {
                  MarkdownEdit::insertMarkdownToCursor(
                      c, QStringLiteral("![a](a.png)"));
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("hello![a](a.png) world"));
        QCOMPARE(cursor.position(), 5 + image.length());
    }

    void testInsertMarkdownReplacesSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        const QString image = QStringLiteral("![a](a.png)");
        apply(QStringLiteral("hello world"), 0, 5,
              [](QTextCursor &c) {
                  MarkdownEdit::insertMarkdownToCursor(
                      c, QStringLiteral("![a](a.png)"));
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("![a](a.png) world"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), image.length());
    }

    void testInsertMarkdownIsOneUndoStep()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("ab"), 1, 1,
              [](QTextCursor &c) {
                  MarkdownEdit::insertMarkdownToCursor(
                      c, QStringLiteral("![a](a)\n![b](b)"));
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("a![a](a)\n![b](b)b"));
        // 多张图片整段插入必须能一次撤销，否则用户拖入十张图要按十次。
        QVERIFY(document.isUndoAvailable());
        document.undo();
        QCOMPARE(document.toPlainText(), QStringLiteral("ab"));
        QVERIFY(!document.isUndoAvailable());
    }

    void testInsertMarkdownIgnoresEmptyText()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("abc"), 1, 1,
              [](QTextCursor &c) {
                  QVERIFY(!MarkdownEdit::insertMarkdownToCursor(c, QString()));
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("abc"));
        QVERIFY(!document.isUndoAvailable());
    }

    void testTaskListAddsAndRemoves()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("verify it works"), 0, 16,
              &MarkdownEdit::applyTaskToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("- [ ] verify it works"));
        // 全部已是任务行 → 去掉。
        apply(QStringLiteral("- [ ] verify"), 0, 12,
              &MarkdownEdit::applyTaskToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("verify"));
    }

    // --- M3 查询侧（B1：checkable 按钮的数据源）--------------------------

    void testQueryBoldAtCursor()
    {
        // 判据是 markdown 源码：光标在 **..** 内即加粗（纯文本标记，
        // 富文本格式在这个编辑器里不存在）。
        QTextDocument document;
        document.setPlainText(QStringLiteral("plain **bold** tail"));
        QTextCursor probe(&document);

        probe.setPosition(10);  // “bold”内部（7..11 区间）
        QVERIFY(MarkdownEdit::isBoldAtCursor(probe));
        probe.setPosition(2);   // 纯文本内
        QVERIFY(!MarkdownEdit::isBoldAtCursor(probe));
        // 选区跨纯文本与加粗：任一命中即真。
        probe.setPosition(4);
        probe.setPosition(10, QTextCursor::KeepAnchor);
        QVERIFY(MarkdownEdit::isBoldAtCursor(probe));
        // 标记本身（**）的位置不算在内容里：贴在标记外侧不算加粗。
        probe.setPosition(6);   // 正好贴在第一个 * 之后
        QVERIFY(!MarkdownEdit::isBoldAtCursor(probe));
    }

    void testQueryCodeAndHeadingAtCursor()
    {
        // 查询的输入面是 markdown 源码（我们的动作写的是纯文本标记，
        // 不是富文本格式）：源码语义上光标在 `..` 或 **..** 内即命中。
        QTextDocument document;
        QTextCursor cursor(&document);
        cursor.insertText(QStringLiteral("use `code` here"));

        QTextCursor probe(&document);
        probe.setPosition(6);   // `code` 内部
        QVERIFY(MarkdownEdit::isCodeAtCursor(probe));
        probe.setPosition(2);   // 普通文字内
        QVERIFY(!MarkdownEdit::isCodeAtCursor(probe));
        // **bold** 的判定同一条路。
        document.setPlainText(QStringLiteral("**bold** tail"));
        probe = QTextCursor(&document);
        probe.setPosition(4);   // bold 内部
        QVERIFY(MarkdownEdit::isBoldAtCursor(probe));
        probe.setPosition(10);  // tail 内
        QVERIFY(!MarkdownEdit::isBoldAtCursor(probe));
    }

    void testQueryHeadingFromTextForm()
    {
        // 我们自己的动作写入的是纯文本 #：文本判定也认。
        QTextDocument document;
        document.setPlainText(QStringLiteral("### title"));
        QTextCursor probe(&document);
        probe.setPosition(2);
        QCOMPARE(MarkdownEdit::headingLevelAtCursor(probe), 3);
        // 普通行返回 0。
        document.setPlainText(QStringLiteral("plain text"));
        probe = QTextCursor(&document);
        probe.setPosition(2);
        QCOMPARE(MarkdownEdit::headingLevelAtCursor(probe), 0);
    }

    void testQueryUnderlineAtCursor()
    {
        // <u>..</u> 内容区间命中即真；开闭标记不对称（3/4 字符），配对
        // 按文本先后交替。
        QTextDocument document;
        document.setPlainText(QStringLiteral("plain <u>under</u> tail"));
        QTextCursor probe(&document);

        probe.setPosition(10);  // “under”内部（7..13 区间）
        QVERIFY(MarkdownEdit::isUnderlineAtCursor(probe));
        probe.setPosition(2);   // 纯文本内
        QVERIFY(!MarkdownEdit::isUnderlineAtCursor(probe));
        // 选区跨纯文本与下划线：任一命中即真。
        probe.setPosition(4);
        probe.setPosition(10, QTextCursor::KeepAnchor);
        QVERIFY(MarkdownEdit::isUnderlineAtCursor(probe));
        // 未闭合的开标记不命中（打字中间态不算下划线内）。
        document.setPlainText(QStringLiteral("half <u>open"));
        probe = QTextCursor(&document);
        probe.setPosition(10);
        QVERIFY(!MarkdownEdit::isUnderlineAtCursor(probe));
        // 两对下划线：各自独立配对，第二对之后才是纯文本。
        document.setPlainText(QStringLiteral("<u>a</u> mid <u>b</u>"));
        probe = QTextCursor(&document);
        probe.setPosition(16);  // 第二对的 b 内部（13..14 区间）
        QVERIFY(MarkdownEdit::isUnderlineAtCursor(probe));
        probe.setPosition(10);  // mid 内（两对之间）
        QVERIFY(!MarkdownEdit::isUnderlineAtCursor(probe));
    }

    void testNewActionsAreOneUndoStep()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("aa\nbb"));
        QTextCursor cursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        MarkdownEdit::applyNumberedToCursor(cursor);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
    }

    // --- Tab 缩进（VSCode 语义） -------------------------------------------

    void testTabIndentsEverySelectedLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 选区 0..5 盖住 aa 与 bb 两行；终点 5 恰在第三行行首（没选到
        // 内容），cc 不参与缩进——与 applyBullet 同一条边界纪律。
        apply(QStringLiteral("aa\nbb\ncc"), 0, 5,
              &MarkdownEdit::handleTabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("\taa\n\tbb\ncc"));
        // 结果选区覆盖两行连同新增缩进：反复按 Tab 可以继续右移。
        QCOMPARE(cursor.selectionStart(), 0);
        QCOMPARE(cursor.selectionEnd(), 7);
        QCOMPARE(normalizedSelection(cursor),
                 QStringLiteral("\taa\n\tbb"));
        MarkdownEdit::handleTabToCursor(cursor, tabOptions());
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("\t\taa\n\t\tbb\ncc"));
        QCOMPARE(normalizedSelection(cursor),
                 QStringLiteral("\t\taa\n\t\tbb"));
    }

    void testTabIndentIsOneUndoStep()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 0, 5,
              &MarkdownEdit::handleTabToCursor, tabOptions(), &document, &cursor);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
    }

    void testTabSingleLineSelectionReplaced()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 单行选区保持原生语义：选区被替换为一个 Tab。
        apply(QStringLiteral("aa bb cc"), 3, 5,
              &MarkdownEdit::handleTabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa \t cc"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), 4);
    }

    void testTabWithoutSelectionInsertsTab()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("hello"), 2, 2,
              &MarkdownEdit::handleTabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("he\tllo"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), 3);
    }

    // --- Shift+Tab 反缩进 --------------------------------------------------

    void testBacktabOutdentsEverySelectedLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 与 Tab 同一条边界纪律：选区终点 7 恰在第三行行首，cc 不参与。
        apply(QStringLiteral("\taa\n\tbb\ncc"), 0, 7,
              &MarkdownEdit::handleBacktabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb\ncc"));
        // 选区随内容左移：反复按 Shift+Tab 可以继续退，键位方向保持。
        QCOMPARE(cursor.selectionStart(), 0);
        QCOMPARE(cursor.selectionEnd(), 5);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("aa\nbb"));
        QVERIFY(cursor.anchor() <= cursor.position());
    }

    void testTabAndBacktabAreInverse()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb\ncc"), 0, 5,
              &MarkdownEdit::handleTabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("\taa\n\tbb\ncc"));
        MarkdownEdit::handleBacktabToCursor(cursor, tabOptions());
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb\ncc"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("aa\nbb"));
    }

    void testBacktabLeavesUnindentedLinesAlone()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 8 空格退 4、2 空格全退、无缩进行不动：一次只退一个缩进单位。
        apply(QStringLiteral("        deep\n  two\nplain"), 0, 24,
              &MarkdownEdit::handleBacktabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("    deep\ntwo\nplain"));
    }

    void testBacktabWithoutSelectionOutdentsCurrentLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 无选区（光标在第二行中间）：只退光标所在行，光标随之左移。
        apply(QStringLiteral("\taa\n\tbb"), 5, 5,
              &MarkdownEdit::handleBacktabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("\taa\nbb"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), 4);
    }

    void testBacktabClampsCaretInsideIndent()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 光标停在缩进内部：退到行首为止，不会跑到上一行去。
        apply(QStringLiteral("\taa"), 0, 0,
              &MarkdownEdit::handleBacktabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa"));
        QCOMPARE(cursor.position(), 0);
    }

    void testBacktabWithoutIndentIsNoOp()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 0, 5,
              &MarkdownEdit::handleBacktabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("aa\nbb"));
        // 没有可退的缩进时不留一个空撤销步。
        QVERIFY(!document.isUndoAvailable());
    }

    void testBacktabIsOneUndoStep()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("\taa\n\tbb"), 0, 7,
              &MarkdownEdit::handleBacktabToCursor, tabOptions(), &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("\taa\n\tbb"));
    }

    // --- A4 可配置缩进（空格模式） ----------------------------------------

    void testTabInsertsSpacesWhenConfigured()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 无选区：插一个缩进单位 = 2 空格。
        apply(QStringLiteral("hello"), 2, 2,
              &MarkdownEdit::handleTabToCursor, spaceOptions(),
              &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("he  llo"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), 4);
        // 多行选区：每行行首插 2 空格，选区随之扩展。
        apply(QStringLiteral("aa\nbb\ncc"), 0, 5,
              &MarkdownEdit::handleTabToCursor, spaceOptions(),
              &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("  aa\n  bb\ncc"));
        QCOMPARE(normalizedSelection(cursor),
                 QStringLiteral("  aa\n  bb"));
    }

    void testBacktabRemovesOneConfiguredUnit()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 每级 2 空格：4 空格退 2、2 空格全退、无缩进不动（旧基准是固定
        // 退 4，配置生效的判据就是这里）。
        apply(QStringLiteral("    deep\n  two\nplain"), 0, 18,
              &MarkdownEdit::handleBacktabToCursor, spaceOptions(),
              &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("  deep\ntwo\nplain"));
    }

    void testTabAndBacktabAreInverseWithSpaces()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 0, 5,
              &MarkdownEdit::handleTabToCursor, spaceOptions(),
              &document, &cursor);
        MarkdownEdit::handleBacktabToCursor(cursor, spaceOptions());
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("aa\nbb"));
    }

    // --- A5 格式切换 -------------------------------------------------------

    void testBoldTogglesOffOnSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 选区恰好被 ** 夹住：去标记并保持内容选中。
        apply(QStringLiteral("a**bold**b"), 3, 7,
              &MarkdownEdit::applyBoldToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aboldb"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("bold"));
        // 再执行一次 = 重新包裹（切换是往返的）。
        MarkdownEdit::applyBoldToCursor(cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("a**bold**b"));
    }

    void testBoldTogglesOffWithCursorInside()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("a**bold**b"));
        QTextCursor cursor(&document);
        cursor.setPosition(5);   // **bold** 内部
        MarkdownEdit::applyBoldToCursor(cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aboldb"));
        QCOMPARE(cursor.position(), 1);   // 落在原开标记起点
    }

    void testBoldTogglesOffEmptySkeleton()
    {
        // 刚插入的骨架（光标夹在 **** 中间）再执行一次 = 整个删掉。
        QTextDocument document;
        document.setPlainText(QStringLiteral("a****b"));
        QTextCursor cursor(&document);
        cursor.setPosition(3);
        MarkdownEdit::applyBoldToCursor(cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("ab"));
        QCOMPARE(cursor.position(), 1);
    }

    void testItalicTogglesIndependentlyOfBold()
    {
        // 斜体骨架里光标处执行斜体 = 去标记。
        QTextDocument document;
        document.setPlainText(QStringLiteral("*em*"));
        QTextCursor cursor(&document);
        cursor.setPosition(2);
        MarkdownEdit::applyItalicToCursor(cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("em"));
        // 加粗动作不认单 * 的 run：同样的位置执行加粗是插入空骨架
        //（****），不会把斜体误拆。
        document.setPlainText(QStringLiteral("*em*"));
        cursor = QTextCursor(&document);
        cursor.setPosition(2);
        MarkdownEdit::applyBoldToCursor(cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("*e****m*"));
    }

    void testCodeTogglesOffOnSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        // "a`code`b"：code 在 2..6，两侧反引号在 1 与 6。
        apply(QStringLiteral("a`code`b"), 2, 6,
              &MarkdownEdit::applyCodeToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("acodeb"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("code"));
    }

    void testUnderlineTogglesOff()
    {
        // 选区被 <u>/</u> 夹住：去标记。
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("a<u>text</u>b"), 4, 8,
              &MarkdownEdit::applyUnderlineToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("atextb"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("text"));
        // 光标在对内：同样去掉这对标记。
        document.setPlainText(QStringLiteral("a<u>text</u>b"));
        cursor = QTextCursor(&document);
        cursor.setPosition(6);
        MarkdownEdit::applyUnderlineToCursor(cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("atextb"));
    }

    // --- A3 智能回车 -------------------------------------------------------

    void testSmartEnterContinuesBullet()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("- item"));
        QTextCursor cursor(&document);
        cursor.setPosition(6);
        QVERIFY(MarkdownEdit::smartEnterToCursor(cursor));
        QCOMPARE(document.toPlainText(), QStringLiteral("- item\n- "));
        QCOMPARE(cursor.position(), 9);
    }

    void testSmartEnterContinuesNumbered()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("3. third"));
        QTextCursor cursor(&document);
        cursor.setPosition(8);
        QVERIFY(MarkdownEdit::smartEnterToCursor(cursor));
        QCOMPARE(document.toPlainText(), QStringLiteral("3. third\n4. "));
    }

    void testSmartEnterContinuesTaskAsUnchecked()
    {
        // 已勾选的任务项延续时回到未勾选形态。
        QTextDocument document;
        document.setPlainText(QStringLiteral("- [x] done"));
        QTextCursor cursor(&document);
        cursor.setPosition(10);
        QVERIFY(MarkdownEdit::smartEnterToCursor(cursor));
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("- [x] done\n- [ ] "));
    }

    void testSmartEnterContinuesQuote()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("> text"));
        QTextCursor cursor(&document);
        cursor.setPosition(6);
        QVERIFY(MarkdownEdit::smartEnterToCursor(cursor));
        QCOMPARE(document.toPlainText(), QStringLiteral("> text\n> "));
    }

    void testSmartEnterKeepsIndent()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("  - item"));
        QTextCursor cursor(&document);
        cursor.setPosition(8);
        QVERIFY(MarkdownEdit::smartEnterToCursor(cursor));
        QCOMPARE(document.toPlainText(), QStringLiteral("  - item\n  - "));
    }

    void testSmartEnterExitsEmptyItem()
    {
        // 空项回车退出列表：前缀被删掉，只留一个空行。
        QTextDocument document;
        document.setPlainText(QStringLiteral("- "));
        QTextCursor cursor(&document);
        cursor.setPosition(2);
        QVERIFY(MarkdownEdit::smartEnterToCursor(cursor));
        QCOMPARE(document.toPlainText(), QString());
        QCOMPARE(cursor.position(), 0);
        // 退出后再按一次：普通空行，不接管（调用方走默认换行）。
        QVERIFY(!MarkdownEdit::smartEnterToCursor(cursor));
    }

    void testSmartEnterIgnoresPlainLine()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("plain"));
        QTextCursor cursor(&document);
        cursor.setPosition(3);
        QVERIFY(!MarkdownEdit::smartEnterToCursor(cursor));
        QCOMPARE(document.toPlainText(), QStringLiteral("plain"));
        // 带选区也不接管（默认行为是替换选区）。
        cursor.setPosition(1);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        QVERIFY(!MarkdownEdit::smartEnterToCursor(cursor));
    }

    // --- A6 输入配对 -------------------------------------------------------

    void testAutoPairOpensPairs()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("ab"));
        QTextCursor cursor(&document);
        cursor.setPosition(1);
        QVERIFY(MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("*")));
        QCOMPARE(document.toPlainText(), QStringLiteral("a**b"));
        QCOMPARE(cursor.position(), 2);
        // 反引号与括号同理。
        cursor.setPosition(4);
        QVERIFY(MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("`")));
        QCOMPARE(document.toPlainText(), QStringLiteral("a**b``"));
        cursor.setPosition(6);
        QVERIFY(MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("[")));
        QCOMPARE(document.toPlainText(), QStringLiteral("a**b``[]"));
        cursor.setPosition(8);
        QVERIFY(MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("(")));
        QCOMPARE(document.toPlainText(), QStringLiteral("a**b``[]()"));
    }

    void testAutoPairSkipsClosingMark()
    {
        // 光标停在闭合标记前：再输入同一字符只是越过它，不插入。
        QTextDocument document;
        document.setPlainText(QStringLiteral("*a*"));
        QTextCursor cursor(&document);
        cursor.setPosition(2);   // 闭合 * 之前
        QVERIFY(MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("*")));
        QCOMPARE(document.toPlainText(), QStringLiteral("*a*"));
        QCOMPARE(cursor.position(), 3);
    }

    void testAutoPairUpgradesEmptyPair()
    {
        // 斜体骨架里再按一次 * = 加粗骨架（*|* → **|**）。
        QTextDocument document;
        document.setPlainText(QStringLiteral("**"));
        QTextCursor cursor(&document);
        cursor.setPosition(1);
        QVERIFY(MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("*")));
        QCOMPARE(document.toPlainText(), QStringLiteral("****"));
        QCOMPARE(cursor.position(), 2);
        // ~ 同理升级出删除线骨架。
        document.setPlainText(QStringLiteral("~~"));
        cursor = QTextCursor(&document);
        cursor.setPosition(1);
        QVERIFY(MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("~")));
        QCOMPARE(document.toPlainText(), QStringLiteral("~~~~"));
        QCOMPARE(cursor.position(), 2);
    }

    void testAutoPairPassesThroughOtherChars()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("a"));
        QTextCursor cursor(&document);
        cursor.setPosition(1);
        QVERIFY(!MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("x")));
        QVERIFY(!MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("ab")));
        QCOMPARE(document.toPlainText(), QStringLiteral("a"));
    }

    void testAutoPairIsOneUndoStep()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("a"));
        QTextCursor cursor(&document);
        cursor.setPosition(1);
        MarkdownEdit::autoTypeToCursor(cursor, QStringLiteral("*"));
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("a"));
    }

    // --- B2 查找 / 替换 ---------------------------------------------------

    void testFindForwardAndWrap()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("foo bar foo baz"));
        // 从 0 找：第一个 foo 在 0。
        auto hit = MarkdownEdit::findFrom(&document,
                                          QStringLiteral("foo"), 0,
                                          false, false);
        QCOMPARE(hit.start, 0);
        QCOMPARE(hit.length, 3);
        QVERIFY(!hit.wrapped);
        // 从 8 找：第二个 foo 在 8。
        hit = MarkdownEdit::findFrom(&document, QStringLiteral("foo"), 8,
                                     false, false);
        QCOMPARE(hit.start, 8);
        // 从 12 找：尾部没有 → 回卷到 0。
        hit = MarkdownEdit::findFrom(&document, QStringLiteral("foo"), 12,
                                     false, false);
        QCOMPARE(hit.start, 0);
        QVERIFY(hit.wrapped);
        // 找不到的词：-1 且不回卷标志。
        hit = MarkdownEdit::findFrom(&document, QStringLiteral("zzz"), 0,
                                     false, false);
        QCOMPARE(hit.start, -1);
        QVERIFY(!hit.wrapped);
    }

    void testFindCaseAndWholeWords()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("Foo foo foobar"));
        // 大小写不敏感：第一个命中 0。
        auto hit = MarkdownEdit::findFrom(&document,
                                          QStringLiteral("foo"), 0,
                                          false, false);
        QCOMPARE(hit.start, 0);
        // 区分大小写：跳过大写的 Foo，命中 4。
        hit = MarkdownEdit::findFrom(&document, QStringLiteral("foo"), 0,
                                     true, false);
        QCOMPARE(hit.start, 4);
        // 全词：foobar 里的 foo 不算。从 5 向后只有 foobar（非全词），
        // 回卷后命中 0 的 Foo（大小写不敏感 + 全词）。
        hit = MarkdownEdit::findFrom(&document, QStringLiteral("foo"), 5,
                                     false, true);
        QCOMPARE(hit.start, 0);
        QVERIFY(hit.wrapped);
        // 区分大小写 + 全词：Foo（大写）不命中，foobar 也不命中 → 只命中 4。
        hit = MarkdownEdit::findFrom(&document, QStringLiteral("foo"), 0,
                                     true, true);
        QCOMPARE(hit.start, 4);
        // 全词 + 区分大小写，从 5 起：4 已在身后，foobar 不算 → 回卷
        // 也只回卷到 4。
        hit = MarkdownEdit::findFrom(&document, QStringLiteral("foo"), 5,
                                     true, true);
        QCOMPARE(hit.start, 4);
        QVERIFY(hit.wrapped);
    }

    void testReplaceAndFindNext()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("one two one two"));
        QTextCursor selection(&document);
        // 选中第一个 "one"（0..3）。
        selection.setPosition(0);
        selection.setPosition(3, QTextCursor::KeepAnchor);
        const auto next = MarkdownEdit::replaceAndFindNext(
            &document, selection, QStringLiteral("1"),
            QStringLiteral("one"), false, false);
        QCOMPARE(document.toPlainText(), QStringLiteral("1 two one two"));
        // 下一个 one 在 6。
        QCOMPARE(next.start, 6);
        QVERIFY(!next.wrapped);
    }

    void testReplaceAllCountsAndUndo()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("cat dog cat cat"));
        const int count = MarkdownEdit::replaceAll(
            &document, QStringLiteral("cat"), QStringLiteral("dog"),
            false, false);
        QCOMPARE(count, 3);
        QCOMPARE(document.toPlainText(), QStringLiteral("dog dog dog dog"));
        // 一次撤销全部回退。
        QTextCursor cursor(&document);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("cat dog cat cat"));
    }

    void testReplaceAllWithSelfContainingNeedle()
    {
        // 替换串包含关键词：不进死循环（从替换串末尾继续找）。
        QTextDocument document;
        document.setPlainText(QStringLiteral("a a a"));
        const int count = MarkdownEdit::replaceAll(
            &document, QStringLiteral("a"), QStringLiteral("aa"),
            false, false);
        QCOMPARE(count, 3);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa aa aa"));
    }

    void testFindEmptyNeedleMisses()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("anything"));
        const auto hit = MarkdownEdit::findFrom(&document, QString(), 0,
                                                false, false);
        QCOMPARE(hit.start, -1);
        QCOMPARE(MarkdownEdit::replaceAll(&document, QString(),
                                          QStringLiteral("x"), false,
                                          false),
                 0);
    }

    // --- 行级动作（B 批次） -------------------------------------------------

    void testMoveLineUpSwapsWithPreviousLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 光标在第二行行首：整行与上一行交换，光标随行落到新位置行首。
        apply(QStringLiteral("aa\nbb"), 3, 3,
              &MarkdownEdit::moveLineUpToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("bb\naa"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), 0);
    }

    void testMoveLineUpMovesSelectedBlock()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 两行选区整体上移，选区仍罩住同一段文字。
        apply(QStringLiteral("aa\nbb\ncc"), 0, 5,
              &MarkdownEdit::moveLineUpToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("aa\nbb\ncc"));
        // 首块已在文档顶：整段无处可挪，原样返回。
        QVERIFY(!document.isUndoAvailable());
    }

    void testMoveLineDownSwapsWithNextLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 光标在第一行中间（pos 1）：与下一行交换后光标落在同一列。
        apply(QStringLiteral("aa\nbb\ncc"), 1, 1,
              &MarkdownEdit::moveLineDownToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("bb\naa\ncc"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), 4);
    }

    void testMoveLineDownMovesSelectedBlock()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 两行选区下移到第三行之后，选区跟着走。
        apply(QStringLiteral("aa\nbb\ncc"), 0, 5,
              &MarkdownEdit::moveLineDownToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("cc\naa\nbb"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("aa\nbb"));
    }

    void testMoveLineAtEdgeIsNoOp()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 0, 0,
              &MarkdownEdit::moveLineUpToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
        QVERIFY(!document.isUndoAvailable());
        apply(QStringLiteral("aa\nbb"), 4, 4,
              &MarkdownEdit::moveLineDownToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
        QVERIFY(!document.isUndoAvailable());
    }

    void testMoveLineIsOneUndoStep()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 3, 3,
              &MarkdownEdit::moveLineUpToCursor, &document, &cursor);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
    }

    void testDuplicateLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 无选区：当前行在下方复制，光标落到副本行首。
        apply(QStringLiteral("aa\nbb"), 0, 0,
              &MarkdownEdit::duplicateLineToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("aa\naa\nbb"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), 3);
    }

    void testDuplicateLineSelectionLandsOnCopy()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 有选区：副本插在整段之后，选区罩住副本——继续输入作用于副本。
        apply(QStringLiteral("aa\nbb"), 0, 2,
              &MarkdownEdit::duplicateLineToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("aa\naa\nbb"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("aa"));
        QCOMPARE(cursor.selectionStart(), 3);
    }

    void testDuplicateLineIsOneUndoStep()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 3, 3,
              &MarkdownEdit::duplicateLineToCursor, &document, &cursor);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
    }

    void testDeleteLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 删中间行：不留空行，光标落到紧随其后的行行首。
        apply(QStringLiteral("aa\nbb\ncc"), 4, 4,
              &MarkdownEdit::deleteLineToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\ncc"));
        QVERIFY(!cursor.hasSelection());
        QCOMPARE(cursor.position(), 3);
    }

    void testDeleteLineCoversSelection()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 选区盖住两行：连行尾分隔符一起删。
        apply(QStringLiteral("aa\nbb\ncc"), 0, 5,
              &MarkdownEdit::deleteLineToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("cc"));
        QCOMPARE(cursor.position(), 0);
    }

    void testDeleteLineIsOneUndoStep()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("aa\nbb"), 0, 0,
              &MarkdownEdit::deleteLineToCursor, &document, &cursor);
        document.undo(&cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("aa\nbb"));
    }

    void testSelectLine()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 无选区：选中光标所在行（不含行尾换行）。
        apply(QStringLiteral("aa\nbb"), 1, 1,
              &MarkdownEdit::selectLineToCursor, &document, &cursor);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("aa"));
        // 多行选区：扩到整行边界（起点在行中算整行，终点收到行尾）。
        apply(QStringLiteral("aa\nbb"), 1, 4,
              &MarkdownEdit::selectLineToCursor, &document, &cursor);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("aa\nbb"));
    }

    void testSelectWord()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 光标在词中间：选中整个词。
        apply(QStringLiteral("say hi"), 1, 1,
              &MarkdownEdit::selectWordToCursor, &document, &cursor);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("say"));
        // 光标在词左边界（左侧是空白）：取右侧的词。
        apply(QStringLiteral("say hi"), 4, 4,
              &MarkdownEdit::selectWordToCursor, &document, &cursor);
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("hi"));
        // 光标在空白上：不动。
        QTextDocument blank;
        blank.setPlainText(QStringLiteral("a  b"));
        QTextCursor blankCursor(&blank);
        blankCursor.setPosition(2);
        MarkdownEdit::selectWordToCursor(blankCursor);
        QVERIFY(!blankCursor.hasSelection());
    }

    void testConvertCaseUpperLower()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 选区转换，空白保留。
        apply(QStringLiteral("say hi"), 0, 3,
              [](QTextCursor &c) {
                  MarkdownEdit::convertCaseToCursor(
                      c, MarkdownEdit::UpperCase);
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("SAY hi"));
        QCOMPARE(normalizedSelection(cursor), QStringLiteral("SAY"));
        apply(QStringLiteral("SAY HI"), 4, 6,
              [](QTextCursor &c) {
                  MarkdownEdit::convertCaseToCursor(
                      c, MarkdownEdit::LowerCase);
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("SAY hi"));
    }

    void testConvertCaseTitle()
    {
        QTextDocument document;
        QTextCursor cursor;
        apply(QStringLiteral("say hi there"), 0, 12,
              [](QTextCursor &c) {
                  MarkdownEdit::convertCaseToCursor(
                      c, MarkdownEdit::TitleCase);
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("Say Hi There"));
    }

    void testConvertCaseWithoutSelectionUsesWordAtCaret()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 无选区：转光标处的词。
        apply(QStringLiteral("say hi"), 2, 2,
              [](QTextCursor &c) {
                  MarkdownEdit::convertCaseToCursor(
                      c, MarkdownEdit::UpperCase);
              }, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("SAY hi"));
        // 已是目标形态：原样返回，不留空撤销步。
        QTextDocument done;
        done.setPlainText(QStringLiteral("SAY"));
        QTextCursor doneCursor(&done);
        doneCursor.setPosition(1);
        MarkdownEdit::convertCaseToCursor(doneCursor,
                                          MarkdownEdit::UpperCase);
        QCOMPARE(done.toPlainText(), QStringLiteral("SAY"));
        QVERIFY(!done.isUndoAvailable());
        // 光标在空白上：不动。
        QTextDocument blank;
        blank.setPlainText(QStringLiteral("  "));
        QTextCursor blankCursor(&blank);
        blankCursor.setPosition(1);
        MarkdownEdit::convertCaseToCursor(blankCursor,
                                          MarkdownEdit::UpperCase);
        QCOMPARE(blank.toPlainText(), QStringLiteral("  "));
    }

    void testPromoteAndDemoteHeading()
    {
        QTextDocument document;
        QTextCursor cursor;
        // 升级：## → #。
        apply(QStringLiteral("## old"), 0, 0,
              &MarkdownEdit::promoteHeadingToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("# old"));
        // H1 再升 = 去标题（回到段落）。
        apply(QStringLiteral("# old"), 0, 0,
              &MarkdownEdit::promoteHeadingToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("old"));
        // 降级：# → ##，## → ###（不封顶在 3）。
        apply(QStringLiteral("# a\n## b"), 0, 7,
              &MarkdownEdit::demoteHeadingToCursor, &document, &cursor);
        QCOMPARE(document.toPlainText(), QStringLiteral("## a\n### b"));
        // 非标题行两头都不动，不留空撤销步。
        QTextDocument plain;
        plain.setPlainText(QStringLiteral("plain"));
        QTextCursor plainCursor(&plain);
        plainCursor.setPosition(0);
        MarkdownEdit::promoteHeadingToCursor(plainCursor);
        MarkdownEdit::demoteHeadingToCursor(plainCursor);
        QCOMPARE(plain.toPlainText(), QStringLiteral("plain"));
        QVERIFY(!plain.isUndoAvailable());
        // H6 再降：原样。
        QTextDocument six;
        six.setPlainText(QStringLiteral("###### deep"));
        QTextCursor sixCursor(&six);
        sixCursor.setPosition(0);
        MarkdownEdit::demoteHeadingToCursor(sixCursor);
        QCOMPARE(six.toPlainText(), QStringLiteral("###### deep"));
        QVERIFY(!six.isUndoAvailable());
    }

    // --- 匹配计数（C 批次） -------------------------------------------------

    void testCountMatchesAndMatchIndex()
    {
        QTextDocument document;
        document.setPlainText(QStringLiteral("foo bar foo baz"));
        QCOMPARE(MarkdownEdit::countMatches(
                     &document, QStringLiteral("foo"), false, false), 2);
        // 全词：foobar 里的 foo 不算。
        document.setPlainText(QStringLiteral("foobar foo"));
        QCOMPARE(MarkdownEdit::countMatches(
                     &document, QStringLiteral("foo"), false, true), 1);
        // 序号（1 起）：从 0 数到 1，从 8 起是 2，尾部之后没有命中是 0。
        document.setPlainText(QStringLiteral("foo bar foo baz"));
        QCOMPARE(MarkdownEdit::matchIndexAt(
                     &document, QStringLiteral("foo"), 0, false, false), 1);
        QCOMPARE(MarkdownEdit::matchIndexAt(
                     &document, QStringLiteral("foo"), 8, false, false), 2);
        QCOMPARE(MarkdownEdit::matchIndexAt(
                     &document, QStringLiteral("foo"), 12, false, false), 0);
        // 空关键词一律 0。
        QCOMPARE(MarkdownEdit::countMatches(&document, QString(),
                                            false, false), 0);
        QCOMPARE(MarkdownEdit::matchIndexAt(&document, QString(), 0,
                                            false, false), 0);
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
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
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
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();
        QCOMPARE(formatAt(document.firstBlock(), 0).length, 0);
    }

    void testFencedCodeBlock()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("```cpp\nint x;\n```\nplain"));
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
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
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
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
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
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
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
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
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        QVERIFY(!formatAt(block, 2).format.fontItalic());
        QCOMPARE(formatAt(block, 2).format.fontWeight(),
                 static_cast<int>(QFont::Bold));
    }

    void testUnderlineRule()
    {
        // <u>..</u>：整段下划线字形，开（3 字符）闭（4 字符）标记各上
        // 强调色；未闭合的开标记不高亮。
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("<u>word</u> open<u>"));
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        QCOMPARE(formatAt(block, 0).format.foreground().color(), colors.emphasis);
        QCOMPARE(formatAt(block, 0).format.fontUnderline(), true);
        QVERIFY(formatAt(block, 3).format.fontUnderline());
        QCOMPARE(formatAt(block, 8).format.foreground().color(), colors.emphasis);
        // 尾部未闭合的 <u>：没有命中区间，保持普通格式。
        QCOMPARE(formatAt(block, 16).length, 0);
    }

    void testColorSwapRehighlight()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("`code`"));
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
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

    // --- 查找高亮（C 批次） ------------------------------------------------

    void testSearchHighlightMarksEveryHit()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("foo bar foo"));
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
        highlighter.setSearch(QStringLiteral("foo"), false, false);

        QCOMPARE(formatAt(document.firstBlock(), 0)
                     .format.background().color(), colors.search);
        QCOMPARE(formatAt(document.firstBlock(), 8)
                     .format.background().color(), colors.search);
        // 命中之外的字符不带查找底色。
        QCOMPARE(formatAt(document.firstBlock(), 5).length, 0);
    }

    void testSearchHighlightWorksOnHeadingLines()
    {
        // 标题行在语法规则里提前返回——查找高亮必须照样生效，且只叠背景
        // 色不盖掉语法前景色（setFormat 的合并语义）。
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("# foo"));
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);
        highlighter.setSearch(QStringLiteral("foo"), false, false);

        const QTextLayout::FormatRange hit =
                formatAt(document.firstBlock(), 2);
        QVERIFY(hit.length > 0);
        QCOMPARE(hit.format.background().color(), colors.search);
        QCOMPARE(hit.format.foreground().color(), colors.heading);
    }

    void testSearchHighlightWholeWordsAndClear()
    {
        const MarkdownColors colors = distinctColors();
        QTextDocument document;
        document.setPlainText(QStringLiteral("foobar foo"));
        awb::markdownedit::MarkdownHighlighter highlighter(&document, colors);

        // 全词：foobar 里的 foo 不高亮。
        highlighter.setSearch(QStringLiteral("foo"), false, true);
        QCOMPARE(formatAt(document.firstBlock(), 0).length, 0);
        QCOMPARE(formatAt(document.firstBlock(), 7)
                     .format.background().color(), colors.search);

        // 空关键词清除查找高亮。
        highlighter.setSearch(QString(), false, false);
        QCOMPARE(formatAt(document.firstBlock(), 7).length, 0);
    }
};

AWB_TEST(TestMarkdownEdit)
AWB_TEST(TestMarkdownHighlighter)
#include "tst_markdownedit.moc"
