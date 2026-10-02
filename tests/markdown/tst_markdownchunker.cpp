#include "awbtest.h"

#include "markdown/MarkdownChunker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QtTest>

using awb::markdown::MarkdownBlock;
using awb::markdown::MarkdownChunker;

namespace {

// 逐行拆好，测试里比行号比拆字符串省事
QStringList toLines(const QString &text)
{
    return text.split(QLatin1Char('\n'));
}

QString joinBlocks(const QVector<MarkdownBlock> &blocks)
{
    QStringList texts;
    for (const MarkdownBlock &block : blocks) {
        texts.append(block.text);
    }
    return texts.join(QLatin1Char('\n'));
}

QString hashOf(const QString &text)
{
    return MarkdownChunker::blockHash(text);
}

// 与 MarkdownChunker 的顶层块判定同源的那组「行首形态」，只用于检查
// 「无空行相邻」的两块是不是合法邻居。这里刻意不复用产品代码的 static
// 函数（它们在 .cpp 的匿名 namespace 里），因为测试要能独立否决实现：
// 实现把一段普通段落切成两块时，测试必须报错，而不是跟着实现一起认为它合法。
bool startsNewBlock(const QString &line)
{
    static const QRegularExpression interrupting(QStringLiteral(
        "^\\s{0,3}("
        "#{1,6}(\\s|$)"          // ATX 标题
        "|`{3,}|~{3,}"            // 围栏
        "|\\$\\$"                  // 显示数学
        "|<!--"                    // HTML 注释
        "|>"                       // 引用
        "|([-*+]|\\d{1,9}[.)])(\\s|$)"  // 列表项
        "|\\|"                     // 表格行
        "|<"                       // HTML 块
        "|([-*_])(\\s*\\1){2,}\\s*$"    // 分隔线
        ")"));
    return interrupting.match(line).hasMatch();
}

// 递归收 docs/ 下的 Markdown 语料
QStringList corpusFiles(const QString &root)
{
    QStringList found;
    QDir dir(root);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            found.append(corpusFiles(entry.absoluteFilePath()));
        } else if (entry.suffix().compare(QStringLiteral("md"),
                                          Qt::CaseInsensitive) == 0) {
            found.append(entry.absoluteFilePath());
        }
    }
    return found;
}

//  spike 复现钩子：AWB_MD_CHUNK_DUMP=<目录> 时把每份语料的块写成 JSON，
// 交给 scripts/spike-markdown-chunker.js 与 markdown-it 的整篇渲染比对。
// 默认关闭——正常测试跑不写任何文件。
void dumpBlocks(const QString &dirPath, const QString &file,
                const QVector<MarkdownBlock> &blocks)
{
    QJsonArray array;
    for (const MarkdownBlock &block : blocks) {
        QJsonObject item;
        item.insert(QStringLiteral("i"), block.index);
        item.insert(QStringLiteral("s"), block.startLine);
        item.insert(QStringLiteral("e"), block.endLine);
        item.insert(QStringLiteral("h"), block.hash);
        item.insert(QStringLiteral("t"), block.text);
        array.append(item);
    }
    QJsonObject root;
    root.insert(QStringLiteral("path"), file);
    root.insert(QStringLiteral("blocks"), array);

    QDir().mkpath(dirPath);
    const QString name = QFileInfo(file).completeBaseName();
    QFile out(QDir(dirPath).filePath(QStringLiteral("%1.json").arg(name)));
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        out.close();
    }
}

} // namespace

/**
 * @brief MarkdownChunker 的测试类：切分规则、行号一致性与 docs/ 语料保真
 */
class TestMarkdownChunker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testEmptySource();
    void testParagraphsAndBlankLines();
    void testFencedCode();
    void testMathBlocks();
    void testHtmlComments();
    void testTables();
    void testSetextAndThematicBreak();
    void testLooseListStaysOneBlock();
    void testFenceInsideListStaysOneBlock();
    void testBlockquoteContinuation();
    void testListInterruptsParagraph();
    void testIndexesAndHashes();
    void testDiffUnchanged();
    void testDiffSingleBlockEdit();
    void testDiffInsertAtFront();
    void testCorpusFidelity();
};

AWB_TEST(TestMarkdownChunker)

/**
 * @brief 空文本与全空白文本都不产生块
 *
 * 「空文档 → 0 块」而不是「1 个空块」：页面按块数决定要不要显示空状态提示，
 * 多一个空块会让空文档渲染成一个空白 section。
 */
void TestMarkdownChunker::testEmptySource()
{
    QCOMPARE(MarkdownChunker::split(QString()).size(), 0);
    QCOMPARE(MarkdownChunker::split(QStringLiteral("\n \n\t\n")).size(), 0);
}

/**
 * @brief 普通段落只在空行处切，块内不留块间空行
 */
void TestMarkdownChunker::testParagraphsAndBlankLines()
{
    // 行号：alpha=0 beta=1 空=2 gamma=3 空=4 空=5 delta=6
    const QString src = QStringLiteral("alpha\nbeta\n\ngamma\n\n\ndelta");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(src);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks.at(0).text, QStringLiteral("alpha\nbeta"));
    QCOMPARE(blocks.at(1).text, QStringLiteral("gamma"));
    QCOMPARE(blocks.at(2).text, QStringLiteral("delta"));
    // 块与块之间的空行不进任何块
    QCOMPARE(blocks.at(0).startLine, 0);
    QCOMPARE(blocks.at(0).endLine, 1);
    QCOMPARE(blocks.at(1).startLine, 3);
    QCOMPARE(blocks.at(1).endLine, 3);
    QCOMPARE(blocks.at(2).startLine, 6);
    QCOMPARE(blocks.at(2).endLine, 6);
}

/**
 * @brief 围栏内的空行、假标题、假分隔线都不切块
 *
 * 围栏是「不可分块」的第一号来源：``` 里面一行 `# 标题` 或 `---` 若在状态机
 * 之外被判定，代码块会被拦腰切开，代码里剩下的部分当 Markdown 解析——渲染出
 * 来的是标题而不是文本。
 */
void TestMarkdownChunker::testFencedCode()
{
    const QString src = QStringLiteral(
        "# Title\n\n```js\nlet a = 1;\n\n---\n\n# not a heading\n```\n\nafter");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(src);
    QCOMPARE(blocks.size(), 3);
    QVERIFY(blocks.at(1).text.startsWith(QStringLiteral("```js")));
    QVERIFY(blocks.at(1).text.endsWith(QStringLiteral("```")));
    QCOMPARE(blocks.at(1).text.count(QLatin1Char('\n')), 6);

    // 结束围栏必须同字符且不短于起始；短的 ``` 不闭合 ````
    const QString nested = QStringLiteral("````md\n```\ncode\n```\n````\n");
    const QVector<MarkdownBlock> outer = MarkdownChunker::split(nested);
    QCOMPARE(outer.size(), 1);

    // 波浪围栏
    QCOMPARE(MarkdownChunker::split(QStringLiteral("~~~\na\n\nb\n~~~\n")).size(), 1);

    // 未闭合的围栏吃到文件结尾，一个字都不丢：末尾那个空行也属于围栏内容
    const QString unterminated = QStringLiteral("```\na\n\nb\n");
    const QVector<MarkdownBlock> open = MarkdownChunker::split(unterminated);
    QCOMPARE(open.size(), 1);
    QCOMPARE(open.at(0).text, unterminated);
}

/**
 * @brief 显示数学自成一块，同行闭合与跨行闭合都算
 */
void TestMarkdownChunker::testMathBlocks()
{
    const QVector<MarkdownBlock> sameLine =
        MarkdownChunker::split(QStringLiteral("$$x^2$$\n\ntext\n"));
    QCOMPARE(sameLine.size(), 2);

    const QString multi = QStringLiteral("before\n\n$$\n\\frac{a}{b}\n\nx\n$$\n\nafter");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(multi);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks.at(1).text, QStringLiteral("$$\n\\frac{a}{b}\n\nx\n$$"));
}

/**
 * @brief HTML 注释吃到 -->，中间的空行不切块
 */
void TestMarkdownChunker::testHtmlComments()
{
    const QString src = QStringLiteral("<!--\ntodo\n\nmore\n-->\nafter");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks.at(0).text, QStringLiteral("<!--\ntodo\n\nmore\n-->"));
}

/**
 * @brief 表格作为整块；普通段落里出现 | 不算表格
 */
void TestMarkdownChunker::testTables()
{
    const QString table = QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(table);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks.at(0).endLine, 3);

    // 没有分隔行的两行 | 只是段落，不能因为「像表格」就改变切分
    const QVector<MarkdownBlock> prose =
        MarkdownChunker::split(QStringLiteral("a | b\nc | d"));
    QCOMPARE(prose.size(), 1);
    QCOMPARE(prose.at(0).text, QStringLiteral("a | b\nc | d"));
}

/**
 * @brief setext 下划线并入上一块；分隔线不并入
 *
 * `标题\n===` 与 `段落\n---` 是同一个块：切开渲染会得到两个段落加分隔线，
 * 而整篇渲染是 h1/h2——这是切分器最容易踩错的一条。
 */
void TestMarkdownChunker::testSetextAndThematicBreak()
{
    const QVector<MarkdownBlock> setext1 =
        MarkdownChunker::split(QStringLiteral("Title\n===\n\ntext"));
    QCOMPARE(setext1.size(), 2);
    QCOMPARE(setext1.at(0).text, QStringLiteral("Title\n==="));

    const QVector<MarkdownBlock> setext2 =
        MarkdownChunker::split(QStringLiteral("Sub\n---\n\ntext"));
    QCOMPARE(setext2.size(), 2);
    QCOMPARE(setext2.at(0).text, QStringLiteral("Sub\n---"));

    // 分隔线自己起头时（前一块是标题/围栏），--- 是新块，不能被吞成 setext
    const QVector<MarkdownBlock> afterHr =
        MarkdownChunker::split(QStringLiteral("# H\n\n---\n\ntext"));
    QCOMPARE(afterHr.size(), 3);
    QCOMPARE(afterHr.at(1).text, QStringLiteral("---"));

    // 长分隔线（4 个 -）是 thematic break 还是 setext？CommonMark 里紧跟段落
    // 时它是 setext 的下划线，所以这里仍然并进段落块——渲染结果与整篇一致。
    const QVector<MarkdownBlock> longDash =
        MarkdownChunker::split(QStringLiteral("para\n----\n"));
    QCOMPARE(longDash.size(), 1);
}

/**
 * @brief 松散列表的延续段跨空行仍属同一块
 *
 * 列表中间断成两块会得到两个 `<ul>`，序号与嵌套都乱掉；blockquote 同理。
 */
void TestMarkdownChunker::testLooseListStaysOneBlock()
{
    const QString src = QStringLiteral(
        "- one\n\n  continuation of one\n\n- two\n\n  body of two\n");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(src);
    QCOMPARE(blocks.size(), 1);
    // 空行在块内部，必须一起吞进来，否则回写的文本没有松散列表的空行
    QCOMPARE(blocks.at(0).text,
             QStringLiteral("- one\n\n  continuation of one\n\n- two\n\n  body of two"));

    // 密集列表（无空行）也是一块
    QCOMPARE(MarkdownChunker::split(QStringLiteral("- a\n- b\n- c")).size(), 1);

    // 有序列表
    QCOMPARE(MarkdownChunker::split(QStringLiteral("1. a\n\n2. b\n")).size(), 1);
}

/**
 * @brief 列表项里的缩进围栏属于列表块，顶格围栏才另起一块
 *
 * 这是逐块渲染与整篇渲染最容易分道扬镳的一处：项内的 ``` 若被判成「新块起始」，
 * 一个 `<ul>` 会被切成两块、代码块从 `<li>` 里掉出来，渲染结果不再是同一份内容
 * 的排版而是**变了形**——不报错，只变形。仓库自己的 docs/zh/standards/
 * coding-standard.md 里就有这个形状，语料比对第一次跑就抓到了它。
 */
void TestMarkdownChunker::testFenceInsideListStaysOneBlock()
{
    const QString src = QStringLiteral(
        "- first\n\n  ```\n  # not a heading\n  | not | a | table |\n  ```\n\n"
        "- second\n");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(src);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks.at(0).text, src.trimmed());

    // 围栏未闭合时也要一路吃到文件结尾：切在中间会让后半截当正文解析
    const QString unclosed = QStringLiteral("- one\n\n  ```\n  body\n");
    QCOMPARE(MarkdownChunker::split(unclosed).size(), 1);

    // 顶格的 ``` 不属于列表项，另起一块（CommonMark 的 content 缩进规则）
    const QVector<MarkdownBlock> topLevel = MarkdownChunker::split(
        QStringLiteral("- one\n```\ncode\n```\n"));
    QCOMPARE(topLevel.size(), 2);
    QCOMPARE(topLevel.at(0).text, QStringLiteral("- one"));
    QCOMPARE(topLevel.at(1).text, QStringLiteral("```\ncode\n```"));

    // 段落里的缩进围栏仍然是新块：段落延续不能吞掉围栏
    const QVector<MarkdownBlock> fromParagraph = MarkdownChunker::split(
        QStringLiteral("para\n  ```\ncode\n  ```\n"));
    QCOMPARE(fromParagraph.size(), 2);
}

/**
 * @brief 连续引用段跨空行合成一块
 */
void TestMarkdownChunker::testBlockquoteContinuation()
{
    const QString src = QStringLiteral("> one\n\n> two\n\nplain");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks.at(0).text, QStringLiteral("> one\n\n> two"));
}

/**
 * @brief 列表与引用可以打断段落，段落自身永远不被行内空行切开
 */
void TestMarkdownChunker::testListInterruptsParagraph()
{
    const QVector<MarkdownBlock> viaList =
        MarkdownChunker::split(QStringLiteral("para\n- item\n"));
    QCOMPARE(viaList.size(), 2);
    QCOMPARE(viaList.at(0).text, QStringLiteral("para"));

    const QVector<MarkdownBlock> viaQuote =
        MarkdownChunker::split(QStringLiteral("para\n> quoted\n"));
    QCOMPARE(viaQuote.size(), 2);

    const QVector<MarkdownBlock> viaHeading =
        MarkdownChunker::split(QStringLiteral("para\n## H\n"));
    QCOMPARE(viaHeading.size(), 2);
}

/**
 * @brief index 连续、行号与文本自洽、hash 稳定且只随内容变
 */
void TestMarkdownChunker::testIndexesAndHashes()
{
    const QString src = QStringLiteral("# H\n\ntext\n\n```c\ncode\n```\n");
    const QVector<MarkdownBlock> blocks = MarkdownChunker::split(src);
    const QStringList lines = toLines(src);
    for (int i = 0; i < blocks.size(); ++i) {
        const MarkdownBlock &block = blocks.at(i);
        QCOMPARE(block.index, i);
        QVERIFY(block.startLine <= block.endLine);
        QVERIFY(block.endLine < lines.size());
        QCOMPARE(block.text,
                 QStringList(lines.mid(block.startLine,
                                       block.endLine - block.startLine + 1))
                     .join(QLatin1Char('\n')));
        QCOMPARE(block.hash, hashOf(block.text));
        QCOMPARE(block.hash.size(), 16);
    }

    // 同内容同键；改一个字符换键
    const QVector<MarkdownBlock> again = MarkdownChunker::split(src);
    QCOMPARE(again.at(1).hash, blocks.at(1).hash);
    QVERIFY(hashOf(QStringLiteral("text")) != hashOf(QStringLiteral("Text")));
}

/**
 * @brief 完全相同的两次切分之间没有差异
 */
void TestMarkdownChunker::testDiffUnchanged()
{
    const QString src = QStringLiteral("a\n\nb\n\nc");
    const QVector<MarkdownBlock> first = MarkdownChunker::split(src);
    const QVector<MarkdownBlock> second = MarkdownChunker::split(src);
    QVERIFY(MarkdownChunker::diff(first, second).isNull());
}

/**
 * @brief 改中间一块时，差异区间只覆盖那一块
 *
 * 这是增量重渲染的全部价值：一次编辑只让页面重排一个 <section>。
 */
void TestMarkdownChunker::testDiffSingleBlockEdit()
{
    const QVector<MarkdownBlock> oldBlocks =
        MarkdownChunker::split(QStringLiteral("a\n\nb\n\nc"));
    const QVector<MarkdownBlock> newBlocks =
        MarkdownChunker::split(QStringLiteral("a\n\nB\n\nc"));

    const MarkdownChunker::Diff change = MarkdownChunker::diff(oldBlocks, newBlocks);
    QCOMPARE(change.startIndex, 1);
    QCOMPARE(change.removeCount, 1);
    QCOMPARE(change.inserted.size(), 1);
    QCOMPARE(change.inserted.at(0).text, QStringLiteral("B"));
    // 替换块带着它在**新**序列里的索引，页面据此重建 section id
    QCOMPARE(change.inserted.at(0).index, 1);
}

/**
 * @brief 开头插入时，中段按「删旧插新」处理；尾部公共前缀仍被利用
 */
void TestMarkdownChunker::testDiffInsertAtFront()
{
    const QVector<MarkdownBlock> oldBlocks =
        MarkdownChunker::split(QStringLiteral("a\n\nb"));
    const QVector<MarkdownBlock> newBlocks =
        MarkdownChunker::split(QStringLiteral("new\n\na\n\nb"));

    const MarkdownChunker::Diff change = MarkdownChunker::diff(oldBlocks, newBlocks);
    QCOMPARE(change.startIndex, 0);
    QCOMPARE(change.removeCount, 0);
    QCOMPARE(change.inserted.size(), 1);
    QCOMPARE(change.inserted.at(0).text, QStringLiteral("new"));

    // 删掉最后一块
    const MarkdownChunker::Diff removed =
        MarkdownChunker::diff(newBlocks, MarkdownChunker::split(QStringLiteral("new\n\na")));
    QCOMPARE(removed.startIndex, 2);
    QCOMPARE(removed.removeCount, 1);
    QVERIFY(removed.inserted.isEmpty());
}

/**
 * @brief 用仓库自己的 docs/ 全量语料守住切分保真性
 *
 * 三条断言，逐份文件检查：
 *  1. 不丢内容行——块区间之外的每一行都必须是空行（切分器只准吃空行）；
 *  2. 不重不漏——区间按序、互不重叠，且 text 与 startLine/endLine 自洽；
 *  3. 不错切——两个相邻块之间没有空行时，后一块的首行必须是 CommonMark 认可
 *     的「能紧跟在上一块之后的块起始」（标题、围栏、列表项…）。段落被从中间
 *     切开就违反这条：那会把一句话渲染成两个段落。
 *
 * 这三条与「逐块渲染再拼接 == 整篇渲染」的关系是必要条件的组合：语料级 HTML
 * 等价由 scripts/spike-markdown-chunker.js 在同一批块上做（spike S4，结论记录
 * 在 docs/research/markdown-preview-runtime.md）。这里跑的是无需 Node 的那部分，
 * 所以它进 `build.sh --test`。
 */
void TestMarkdownChunker::testCorpusFidelity()
{
    const QString root = QStringLiteral(AWB_DOCS_DIR);
    const QStringList files = corpusFiles(root);
    QVERIFY2(files.size() >= 20,
             qPrintable(QStringLiteral("expected a real docs corpus under %1, found %2")
                            .arg(root).arg(files.size())));

    const QString dumpDir = qgetenv("AWB_MD_CHUNK_DUMP");
    int checked = 0;
    for (const QString &file : files) {
        QFile in(file);
        QVERIFY2(in.open(QIODevice::ReadOnly),
                 qPrintable(QStringLiteral("cannot read %1").arg(file)));
        // 语料按 \n 喂给切分器（与 MarkdownDocument 载入后的内存形态一致）
        const QString text =
            QString::fromUtf8(in.readAll()).replace(QStringLiteral("\r\n"),
                                                   QStringLiteral("\n"));
        in.close();

        const QStringList lines = toLines(text);
        const QVector<MarkdownBlock> blocks = MarkdownChunker::split(text);

        const QString label = QFileInfo(file).fileName();
        int cursor = 0;
        for (int i = 0; i < blocks.size(); ++i) {
            const MarkdownBlock &block = blocks.at(i);
            QVERIFY2(block.startLine >= cursor,
                     qPrintable(QStringLiteral("%1: block %2 overlaps the previous one")
                                    .arg(label).arg(i)));
            QVERIFY2(block.endLine < lines.size(),
                     qPrintable(QStringLiteral("%1: block %2 runs past the file")
                                    .arg(label).arg(i)));
            for (int l = cursor; l < block.startLine; ++l) {
                QVERIFY2(lines.at(l).trimmed().isEmpty(),
                         qPrintable(QStringLiteral("%1: line %2 is in no block and not blank: %3")
                                        .arg(label).arg(l).arg(lines.at(l))));
            }
            cursor = block.endLine + 1;

            // 断言 3：无空行相邻时后块必须自己起头
            if (i + 1 < blocks.size() && blocks.at(i + 1).startLine == cursor) {
                QVERIFY2(startsNewBlock(lines.at(blocks.at(i + 1).startLine)),
                         qPrintable(QStringLiteral(
                                        "%1: block %2 starts mid-paragraph: %3")
                                        .arg(label).arg(i + 1)
                                        .arg(lines.at(blocks.at(i + 1).startLine))));
            }
        }
        for (int l = cursor; l < lines.size(); ++l) {
            QVERIFY2(lines.at(l).trimmed().isEmpty(),
                     qPrintable(QStringLiteral("%1: trailing content line %2 not covered")
                                    .arg(label).arg(l)));
        }

        if (!dumpDir.isEmpty()) {
            dumpBlocks(dumpDir, file, blocks);
        }
        ++checked;
    }
    QVERIFY2(checked == files.size(), "every corpus file must be checked");
}

#include "tst_markdownchunker.moc"
