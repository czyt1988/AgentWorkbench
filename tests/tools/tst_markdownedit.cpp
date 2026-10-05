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
        QTextDocument document;
        QTextCursor cursor;
        document.setPlainText(QStringLiteral("a\nbc"));
        cursor = QTextCursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        MarkdownEdit::applyFenceToCursor(cursor, QStringLiteral("cpp"));
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("\n```cpp\na\nbc\n```\n"));
    }

    void testFenceSingleLineFallsBackToInline()
    {
        // 单行内容配围栏没有意义：退化为行内代码（applyCode 的形态）。
        QTextDocument document;
        QTextCursor cursor;
        document.setPlainText(QStringLiteral("foo bar"));
        cursor = QTextCursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(3, QTextCursor::KeepAnchor);
        MarkdownEdit::applyFenceToCursor(cursor, QStringLiteral("cpp"));
        QCOMPARE(document.toPlainText(), QStringLiteral("`foo` bar"));
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
