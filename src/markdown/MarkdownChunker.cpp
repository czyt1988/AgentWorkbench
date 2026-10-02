#include "markdown/MarkdownChunker.h"

#include <QChar>
#include <QCryptographicHash>
#include <QStringList>
#include <QVector>

#include <utility>

namespace awb::markdown {

namespace {

// 本文件的行判定全部是「CommonMark 顶层块边界」级别的最小实现：只用来决定
// 在哪里切块，不用来判断语义。判错的后果是逐块渲染与整篇渲染不等价（内容变
// 形而不是报错），所以有两道闸：tst_markdownchunker 用整个 docs/ 语料检查块
// 的覆盖与顺序，scripts/spike-markdown-chunker.js 用同一个 markdown-it 实例
// 比较两种渲染的 HTML。改动这里的判定后两道都要跑。

const QString kFenceBacktick = QStringLiteral("```");
const QString kFenceTilde = QStringLiteral("~~~");

// 行首最多 3 个空格的缩进才能开启块级构造（CommonMark 的 content 缩进规则）
bool hasBlockIndent(const QString &line)
{
    int spaces = 0;
    for (const QChar &ch : line) {
        if (ch == QLatin1Char(' ')) {
            ++spaces;
        } else if (ch == QLatin1Char('\t')) {
            spaces += 4;
        } else {
            break;
        }
    }
    return spaces < 4;
}

QString trimmedLeft(const QString &line)
{
    int i = 0;
    while (i < line.size()
           && (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t'))) {
        ++i;
    }
    return line.mid(i);
}

bool isBlank(const QString &line)
{
    return trimmedLeft(line).isEmpty();
}

// 围栏起始：``` 或 ~~~ 开头（其后跟可选_info 串）；结束要求同字符、长度不短于起始
bool isFenceStart(const QString &line)
{
    const QString t = trimmedLeft(line);
    return hasBlockIndent(line)
           && (t.startsWith(kFenceBacktick) || t.startsWith(kFenceTilde));
}

int fenceLength(const QString &line)
{
    const QString t = trimmedLeft(line);
    const QChar marker = t.at(0);
    int len = 0;
    while (len < t.size() && t.at(len) == marker) {
        ++len;
    }
    return len;
}

bool closesFence(const QString &line, QChar marker, int minLength)
{
    if (isBlank(line)) {
        return false;
    }
    const QString t = trimmedLeft(line);
    if (t.at(0) != marker || !hasBlockIndent(line)) {
        return false;
    }
    int len = 0;
    while (len < t.size() && t.at(len) == marker) {
        ++len;
    }
    // 结束行除尾随空白外不得再有别的内容
    return len >= minLength && t.mid(len).trimmed().isEmpty();
}

// 显示数学：$$ 单独成行或起头。块内不能出现空行分隔，否则 KaTeX 规则只看到
// 半截 $$，公式就会渲染成原文——所以这里显式把它当成一个不可分块。
bool isMathStart(const QString &line)
{
    return trimmedLeft(line).startsWith(QStringLiteral("$$"));
}

bool closesMath(const QString &line)
{
    return trimmedLeft(line).contains(QStringLiteral("$$"));
}

bool isCommentStart(const QString &line)
{
    return trimmedLeft(line).startsWith(QStringLiteral("<!--"));
}

bool closesComment(const QString &line)
{
    return line.contains(QStringLiteral("-->"));
}

bool isAtxHeading(const QString &line)
{
    const QString t = trimmedLeft(line);
    if (!hasBlockIndent(line) || t.isEmpty() || t.at(0) != QLatin1Char('#')) {
        return false;
    }
    int hashes = 0;
    while (hashes < t.size() && t.at(hashes) == QLatin1Char('#')) {
        ++hashes;
    }
    return hashes <= 6
           && (hashes == t.size() || t.at(hashes) == QLatin1Char(' ')
               || t.at(hashes) == QLatin1Char('\t'));
}

bool isBlockquote(const QString &line)
{
    return hasBlockIndent(line) && trimmedLeft(line).startsWith(QLatin1Char('>'));
}

// 无序列表项：- / * / + 后必须跟空白（`-foo` 是段落文本）
bool isListItem(const QString &line)
{
    const QString t = trimmedLeft(line);
    if (!hasBlockIndent(line) || t.size() < 2) {
        return false;
    }
    const QChar marker = t.at(0);
    if (marker != QLatin1Char('-') && marker != QLatin1Char('*')
        && marker != QLatin1Char('+')) {
        return false;
    }
    const QChar after = t.at(1);
    return after == QLatin1Char(' ') || after == QLatin1Char('\t');
}

bool isOrderedItem(const QString &line)
{
    const QString t = trimmedLeft(line);
    if (!hasBlockIndent(line)) {
        return false;
    }
    int i = 0;
    while (i < t.size() && t.at(i).isDigit()) {
        ++i;
    }
    if (i == 0 || i > 9 || i + 1 >= t.size()) {
        return false;
    }
    const QChar delim = t.at(i);
    if (delim != QLatin1Char('.') && delim != QLatin1Char(')')) {
        return false;
    }
    const QChar after = t.at(i + 1);
    return after == QLatin1Char(' ') || after == QLatin1Char('\t');
}

bool isListLike(const QString &line)
{
    return isListItem(line) || isOrderedItem(line);
}

// 分隔线：---、***、___（≥3，除空格外无其它字符）
bool isThematicBreak(const QString &line)
{
    const QString t = trimmedLeft(line).remove(QLatin1Char(' '));
    if (t.size() < 3 || !hasBlockIndent(line)) {
        return false;
    }
    const QChar marker = t.at(0);
    if (marker != QLatin1Char('-') && marker != QLatin1Char('*')
        && marker != QLatin1Char('_')) {
        return false;
    }
    for (const QChar &ch : t) {
        if (ch != marker) {
            return false;
        }
    }
    return true;
}

// setext 下划线：===… 或 ---…，只在「前面是一行普通段落」时才吞进上一块。
// CommonMark 里它优先于分隔线，所以判定顺序必须先问它、再问 thematic break。
bool isSetextUnderline(const QString &line)
{
    const QString t = trimmedLeft(line).remove(QLatin1Char(' '));
    if (t.size() < 2 || !hasBlockIndent(line)) {
        return false;
    }
    const QChar marker = t.at(0);
    if (marker != QLatin1Char('=') && marker != QLatin1Char('-')) {
        return false;
    }
    for (const QChar &ch : t) {
        if (ch != marker) {
            return false;
        }
    }
    return true;
}

bool isTableDelimiter(const QString &line)
{
    const QString t = trimmedLeft(line).remove(QLatin1Char(' '));
    return t.contains(QLatin1Char('-')) && t.contains(QLatin1Char('|'))
           && !isThematicBreak(line);
}

bool isTableRow(const QString &line)
{
    return trimmedLeft(line).contains(QLatin1Char('|'));
}

// 表格块：表头行 + 分隔行成立才算，普通段落里出现 | 不该被并进表格逻辑
bool startsTable(const QStringList &lines, int index)
{
    if (index + 1 >= lines.size()) {
        return false;
    }
    return isTableRow(lines.at(index)) && isTableDelimiter(lines.at(index + 1));
}

// 下一行是否开启一个新的顶层块（即当前块到此结束）
bool interrupts(const QString &first, const QString &next)
{
    if (isFenceStart(next) || isMathStart(next) || isCommentStart(next)
        || isAtxHeading(next)) {
        return true;
    }
    const bool currentIsList = isListLike(first);
    const bool currentIsQuote = isBlockquote(first);
    const bool currentIsTable = isTableRow(first) && !currentIsList;
    if (isListLike(next) && !currentIsList) {
        // 列表项可以打断段落（CommonMark 0.15+）；列表内部继续是列表，不打断
        return true;
    }
    if (isBlockquote(next) && !currentIsQuote) {
        return true;
    }
    if (currentIsTable && !isTableRow(next)) {
        return true;
    }
    return false;
}

} // namespace

/**
 * @brief 空差异判定
 * @return 既没有删除也没有插入时为 true
 */
bool MarkdownChunker::Diff::isNull() const
{
    return removeCount == 0 && inserted.isEmpty();
}

/**
 * @brief 按顶层块切分 Markdown 源码
 *
 * 行级状态机：先识别「不可分块」的构造（围栏、$$ 显示数学、HTML 注释、表格），
 * 它们在内部空行处不切断；普通块在空行处切断，但松散列表的延续段、blockquote
 * 的 `>` 延续段跨空行仍归同一块（否则会把一个 `<ul>` 拆成两个）。
 *
 * 空行只在「块内部」保留；块与块之间的空行被丢弃——它们在 markdown-it 的
 * token 流里本就不产生内容，所以丢弃不影响逐块拼接后的等价性。
 *
 * @param source 已归一为 \n 换行的 Markdown 全文
 * @return 块序列；source 为空或全空白时返回空序列
 */
QVector<MarkdownBlock> MarkdownChunker::split(const QString &source)
{
    QVector<MarkdownBlock> blocks;
    if (source.trimmed().isEmpty()) {
        return blocks;
    }

    const QStringList lines = source.split(QLatin1Char('\n'));
    const int count = lines.size();

    auto addBlock = [&blocks, &lines](int start, int end) {
        MarkdownBlock block;
        block.index = blocks.size();
        block.startLine = start;
        block.endLine = end;
        QStringList parts;
        for (int i = start; i <= end; ++i) {
            parts.append(lines.at(i));
        }
        block.text = parts.join(QLatin1Char('\n'));
        block.hash = MarkdownChunker::blockHash(block.text);
        blocks.append(block);
    };

    int i = 0;
    while (i < count) {
        if (isBlank(lines.at(i))) {
            ++i;
            continue;
        }

        const QString first = lines.at(i);

        // 围栏：吃到配对的结束行或文件结尾
        if (isFenceStart(first)) {
            const QChar marker = trimmedLeft(first).at(0);
            const int minLength = fenceLength(first);
            int end = i;
            while (end + 1 < count && !closesFence(lines.at(end + 1), marker, minLength)) {
                ++end;
            }
            // 结束行本身属于本块（未配对时 end 停在最后一行）
            if (end + 1 < count) {
                ++end;
            }
            addBlock(i, end);
            i = end + 1;
            continue;
        }

        // 显示数学：$$ 起始行若同行闭合则自成一块，否则吃到下一个 $$
        if (isMathStart(first)) {
            const bool closedSameLine = trimmedLeft(first).size() > 2
                                        && closesMath(trimmedLeft(first).mid(2));
            int end = i;
            if (!closedSameLine) {
                while (end + 1 < count && !closesMath(lines.at(end + 1))) {
                    ++end;
                }
                if (end + 1 < count) {
                    ++end;
                }
            }
            addBlock(i, end);
            i = end + 1;
            continue;
        }

        // HTML 注释：吃到 -->
        if (isCommentStart(first) && !closesComment(first)) {
            int end = i;
            while (end + 1 < count && !closesComment(lines.at(end + 1))) {
                ++end;
            }
            if (end + 1 < count) {
                ++end;
            }
            addBlock(i, end);
            i = end + 1;
            continue;
        }

        // 表格：表头 + 分隔行 + 连续的数据行
        if (startsTable(lines, i)) {
            int end = i + 1;
            while (end + 1 < count && isTableRow(lines.at(end + 1))) {
                ++end;
            }
            addBlock(i, end);
            i = end + 1;
            continue;
        }

        // 一般块：段落 / setext 标题 / 列表 / blockquote / HTML 标签行
        int end = i;
        // 块内再开围栏的状态（列表项里的缩进围栏是唯一进来的路径）。必须
        // 吃到配对的结束行：围栏里的 `## 标题`、`|` 表格行、乃至那行 ``` 本身
        // 都会被下面的判定当成「新块起始」，于是一个 <ul> 被切成两块、代码块
        // 从项里掉出来——逐块渲染与整篇渲染从此不等价。
        QChar fenceMarker;
        int fenceMin = 0;
        // 缩进的围栏只在「本块是列表」时属于项内内容；顶格 ``` 是另一回事，
        // 由循环开头的分支决定断不断。
        auto openFence = [&](int line) {
            const QString text = lines.at(line);
            if (fenceMarker.isNull() && isListLike(first) && isFenceStart(text)
                && text != trimmedLeft(text)) {
                fenceMarker = trimmedLeft(text).at(0);
                fenceMin = fenceLength(text);
            }
        };
        while (end + 1 < count) {
            const QString next = lines.at(end + 1);
            if (!fenceMarker.isNull()) {
                if (closesFence(next, fenceMarker, fenceMin)) {
                    fenceMarker = QChar();
                }
                ++end;
                continue;
            }
            if (isBlank(next)) {
                int probe = end + 1;
                while (probe < count && isBlank(lines.at(probe))) {
                    ++probe;
                }
                if (probe >= count) {
                    break;
                }
                const QString after = lines.at(probe);
                const QString current = lines.at(i);
                const bool listContinues = isListLike(current)
                    && (isListLike(after) || trimmedLeft(after) != after);
                const bool quoteContinues = isBlockquote(current) && isBlockquote(after);
                if (!listContinues && !quoteContinues) {
                    break;
                }
                // 跨空行的延续段：空行属于本块内部，必须一起吞进来。吞进来的
                // 这一行可能就是项内围栏的起始行，状态要在这里开——漏了它，
                // 围栏内的下一行（`# 标题` 之类）会被当成新块而把列表切开。
                end = probe;
                openFence(end);
                continue;
            }
            if (isSetextUnderline(next) && !isThematicBreak(lines.at(end))
                && !isListLike(lines.at(i)) && !isBlockquote(lines.at(i))) {
                ++end;
                continue;
            }
            // 缩进的围栏属于列表项内容（顶格 ``` 才打断列表）：进去而不是断开
            if (isFenceStart(next) && next != trimmedLeft(next)) {
                ++end;
                openFence(end);
                if (fenceMarker.isNull()) {
                    // 不属于本块的缩进围栏（比如段落里的）仍按打断处理
                    break;
                }
                continue;
            }
            if (interrupts(lines.at(i), next)) {
                break;
            }
            ++end;
        }

        addBlock(i, end);
        i = end + 1;
    }

    return blocks;
}

/**
 * @brief 求两次切分之间的最小替换区间
 *
 * 先掐头去尾对齐 hash 相同的公共前缀与公共后缀，剩下的中段一律按「删旧插新」
 * 处理。中段可能远小于实际改动，比如文件开头插一段就把后面全部当作新插入——
 * 这是有意为之：DOM 层做一次 splice 的成本远低于逐块 LCS，而中段之外的块根本
 * 不会被重新解析。
 *
 * @param oldBlocks 上一次交给页面的块序列
 * @param newBlocks 本次 split() 的结果
 * @return 替换区间；无变化时 isNull() 为 true
 */
MarkdownChunker::Diff MarkdownChunker::diff(const QVector<MarkdownBlock> &oldBlocks,
                                            const QVector<MarkdownBlock> &newBlocks)
{
    Diff result;

    int first = 0;
    const int oldCount = oldBlocks.size();
    const int newCount = newBlocks.size();
    while (first < oldCount && first < newCount
           && oldBlocks.at(first).hash == newBlocks.at(first).hash) {
        ++first;
    }

    int oldTail = oldCount - 1;
    int newTail = newCount - 1;
    while (oldTail >= first && newTail >= first
           && oldBlocks.at(oldTail).hash == newBlocks.at(newTail).hash) {
        --oldTail;
        --newTail;
    }

    result.startIndex = first;
    result.removeCount = oldTail - first + 1;
    for (int i = first; i <= newTail; ++i) {
        MarkdownBlock block = newBlocks.at(i);
        block.index = i;
        result.inserted.append(block);
    }
    return result;
}

/**
 * @brief 块内容的短摘要
 *
 * sha1 取前 16 位十六进制：只在「同一次会话内比较两段文本是否相同」这个用途
 * 上使用，不用于安全场景，所以不需要全长；16 个十六进制位（64 bit）在单文档
 * 的块数量级上碰撞概率可忽略。
 *
 * @param text 块原文
 * @return 16 字符的十六进制摘要
 */
QString MarkdownChunker::blockHash(const QString &text)
{
    const QByteArray digest = QCryptographicHash::hash(
        text.toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(digest.toHex().left(16));
}

} // namespace awb::markdown
