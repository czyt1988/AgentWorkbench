#include "markdownedit/MarkdownEdit.h"

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

namespace awb::markdownedit {

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
 * @brief 行首一个缩进单位的长度（Shift+Tab 反缩进用）
 *
 * 行首是一个 Tab 就去掉它；空格缩进按一个缩进单位（spaceUnit 个空格）
 * 算，不足就全去掉——空格式嵌套列表同样要能一次退一级。spaceUnit 由
 * 调用方的缩进偏好带入（空格模式下即每级空格数；\t 模式沿用 4 的宽
 * Tab 习惯值）。
 *
 * @param line 行文本
 * @param spaceUnit 一个缩进单位折算的空格数
 * @return 要去掉的字符数（0 = 这行没有缩进）
 */
int leadingIndentLength(const QString &line, int spaceUnit)
{
    if (line.startsWith(QLatin1Char('\t'))) {
        return 1;
    }
    int spaces = 0;
    while (spaces < spaceUnit && spaces < line.length()
           && line.at(spaces) == QLatin1Char(' ')) {
        ++spaces;
    }
    return spaces;
}

/// 把光标围住的标记对边界（doc 坐标）；openStart = -1 表示没找到。
struct EnclosedPair
{
    int openStart = -1;    ///< 开标记起点
    int contentStart = 0;  ///< 内容起点（开标记之后）
    int contentEnd = 0;    ///< 内容终点（闭标记之前）
    int closeEnd = 0;      ///< 闭标记终点（含）
};

/**
 * @brief 在光标所在块里找把光标围住的对称标记对
 *
 * 「围住」按查询侧的同一条边界语义：光标贴在内容边界上也算在内（那时
 * 「再执行一次会切换」的直觉依然成立）。只认恰好 markLen 长的标记 run
 *（** 里不认单 *，反之亦然），交替配对开闭标记。
 *
 * @param cursor 光标（只读）
 * @param marker 标记字符
 * @param markLen 标记长度（1 = `*`/`` ` ``，2 = `**`/`~~`）
 * @return 围住光标的对；openStart = -1 = 没有
 */
EnclosedPair enclosingMarkerPair(const QTextCursor &cursor,
                                 QLatin1Char marker, int markLen)
{
    EnclosedPair result;
    const QTextBlock block = cursor.block();
    if (!block.isValid()) {
        return result;
    }
    const int blockStart = block.position();
    const QString text = block.text();
    const int position = cursor.position();

    // at 处是否恰好是 markLen 长的 run：两侧（若有）不是同一字符。
    const auto isExactRunAt = [&](int at) {
        for (int i = 0; i < markLen; ++i) {
            if (text.at(at + i) != marker) {
                return false;
            }
        }
        return (at == 0 || text.at(at - 1) != marker)
               && (at + markLen >= text.length()
                   || text.at(at + markLen) != marker);
    };

    int open = -1;
    for (int i = 0; i + markLen <= text.length(); ++i) {
        if (!isExactRunAt(i)) {
            continue;
        }
        if (open < 0) {
            open = i;
            continue;
        }
        const int contentStart = blockStart + open + markLen;
        const int contentEnd = blockStart + i;
        if (position >= contentStart && position <= contentEnd) {
            result.openStart = blockStart + open;
            result.contentStart = contentStart;
            result.contentEnd = contentEnd;
            result.closeEnd = blockStart + i + markLen;
            return result;
        }
        // 未命中：这个闭标记转正为下一对的候选开标记（宽松交替配对）。
        open = i;
    }
    return result;
}

/**
 * @brief 光标前后是否正好是同一标记（空对骨架态，如 `**|**`）
 *
 * 刚由动作插入的空标记对没有内容可配对（run 长度是 markLen 的两倍），
 * enclosingMarkerPair 认不出它；骨架态的判定只看光标两侧是不是同一标
 * 记串。
 *
 * @param cursor 光标（无选区）
 * @param marker 标记字符
 * @param markLen 标记长度
 * @return 是骨架态为真
 */
bool insideEmptyPair(const QTextCursor &cursor, QLatin1Char marker,
                     int markLen)
{
    const int position = cursor.position();
    const QTextDocument *document = cursor.document();
    for (int i = 0; i < markLen; ++i) {
        if (document->characterAt(position - 1 - i) != marker
            || document->characterAt(position + i) != marker) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 删掉一对标记，保留内容（toggleWrap 的去标记路径）
 *
 * 闭标记先删（其坐标不受开标记删除影响）。空内容时 contentStart ==
 * contentEnd，一样适用。删完光标落在光标参数处。
 *
 * @param cursor 光标（持有编辑块）
 * @param openStart 开标记起点
 * @param contentStart 内容起点（开标记之后）
 * @param contentEnd 内容终点（闭标记之前）
 * @param closeEnd 闭标记终点（含）
 */
void unwrapRange(QTextCursor &cursor, int openStart, int contentStart,
                 int contentEnd, int closeEnd)
{
    cursor.beginEditBlock();
    cursor.setPosition(contentEnd);
    cursor.setPosition(closeEnd, QTextCursor::KeepAnchor);
    cursor.insertText(QString());
    cursor.setPosition(openStart);
    cursor.setPosition(contentStart, QTextCursor::KeepAnchor);
    cursor.insertText(QString());
    cursor.setPosition(openStart);
    cursor.endEditBlock();
}

// wrapSelectionToCursor 定义在本文件后面（包裹路径的唯一实现），切换
// 助手先声明后使用。
void wrapSelectionToCursor(QTextCursor &cursor, const QString &marker);

/**
 * @brief 对称标记对的切换包裹（A5：已是该格式时再执行一次 = 去标记）
 *
 * 有选区：去空白后的选区恰好被同名标记夹住 → 去标记并保持内容选中；
 * 否则包裹（原 wrapSelectionToCursor 行为）。无选区：光标在空标记骨架
 * 里或被一对标记围住 → 删掉这对标记；否则插入骨架（标记对 ×2、光标放
 * 中间）。isBoldAt 一类查询的判据在这里被动作真正消费。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 * @param marker 标记字符
 * @param markLen 标记长度（1 或 2）
 */
void toggleWrapToCursor(QTextCursor &cursor, QLatin1Char marker, int markLen)
{
    const QString markerText(markLen, marker);
    QTextDocument *document = cursor.document();

    if (cursor.hasSelection()) {
        const int selectionStart = cursor.selectionStart();
        const int selectionEnd = cursor.selectionEnd();
        const TrimmedSelection trimmed = trimWhitespace(selectedText(cursor));
        const int wrapStart = selectionStart + trimmed.start;
        const int wrapEnd = wrapStart + trimmed.length;
        const auto isRunAt = [&](int at) {
            for (int i = 0; i < markLen; ++i) {
                if (at + i < 0 || at + i >= document->characterCount()) {
                    return false;
                }
                if (document->characterAt(at + i) != marker) {
                    return false;
                }
            }
            return true;
        };
        if (isRunAt(wrapStart - markLen) && isRunAt(wrapEnd)) {
            // 去标记：闭标记先删，再把内容重新选回来。
            cursor.beginEditBlock();
            cursor.setPosition(wrapEnd);
            cursor.setPosition(wrapEnd + markLen, QTextCursor::KeepAnchor);
            cursor.insertText(QString());
            cursor.setPosition(wrapStart - markLen);
            cursor.setPosition(wrapStart, QTextCursor::KeepAnchor);
            cursor.insertText(QString());
            cursor.setPosition(wrapStart - markLen);
            cursor.setPosition(wrapEnd - markLen, QTextCursor::KeepAnchor);
            cursor.endEditBlock();
            return;
        }
        wrapSelectionToCursor(cursor, markerText);
        return;
    }

    const int position = cursor.position();
    if (insideEmptyPair(cursor, marker, markLen)) {
        unwrapRange(cursor, position - markLen, position, position,
                    position + markLen);
        return;
    }
    const EnclosedPair pair = enclosingMarkerPair(cursor, marker, markLen);
    if (pair.openStart >= 0) {
        unwrapRange(cursor, pair.openStart, pair.contentStart,
                    pair.contentEnd, pair.closeEnd);
        return;
    }
    wrapSelectionToCursor(cursor, markerText);
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

/**
 * @brief 选区覆盖到的块范围
 *
 * 行级动作的公共边界纪律（与 applyBulletToCursor 同一条）：选区起点落在
 * 行中算整行；终点恰好贴在换行后（光标位于下一行行首）时那行没选到内容，
 * 不参与。无选区 = 光标所在块。
 *
 * @param cursor 选区光标（只读，不修改）
 * @return 首末块；无效文档返回无效块
 */
struct BlockRange
{
    QTextBlock first;   ///< 覆盖到的第一个块
    QTextBlock last;    ///< 覆盖到的最后一个块
};

BlockRange coveredBlocks(const QTextCursor &cursor)
{
    QTextDocument *document = cursor.document();
    if (!document) {
        return BlockRange();
    }
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    QTextBlock first = document->findBlock(selectionStart);
    QTextBlock last = document->findBlock(
                selectionEnd > selectionStart ? selectionEnd - 1
                                              : selectionEnd);
    return BlockRange{first, last};
}

/**
 * @brief 选区端点在覆盖块范围内的相对偏移（夹取）
 *
 * 行级动作（移动/复制）把整段行文本原样搬到新位置，端点随内容走：把
 * 锚点/位置换算成段内偏移，搬完再加回新基点。终点贴着段尾（下一行行首）
 * 时夹到段长，方向（正反向选区）保持不变。
 *
 * @param value 原始文档坐标
 * @param base 段起点（覆盖块首块行首）
 * @param length 段长（含行尾分隔符）
 * @return 夹到 [0, length] 的段内偏移
 */
int clampedOffset(int value, int base, int length)
{
    return qBound(0, value - base, length);
}

/**
 * @brief 光标处词的文档区间
 *
 * 词 = 字母/数字/下划线连成的段（查找全词判据同一条）。光标贴在词边界
 * 时优先取右侧的词；两侧都不是词（空白、标点之间）时长度 0。
 *
 * @param cursor 光标（只读）
 * @return 区间：start 为文档坐标，length 为 0 表示光标不在词上
 */
struct WordSpan
{
    int start = 0;   ///< 词起点（文档坐标）
    int length = 0;  ///< 词长；0 = 光标不在词上
};

/**
 * @brief 块内容终点（合法光标位）
 *
 * 块 length 含尾部分隔符；position()+length() 对末块落在文档分隔符之后，
 * 那不是合法光标位（QTextCursor 的上限是 characterCount()-1）。行级动作
 * 统一在内容边界上操作，替换串自带分隔、原分隔符原位保留，块结构不变。
 *
 * @param block 目标块
 * @return 块内容末尾的文档位置
 */
int contentEndOf(const QTextBlock &block)
{
    return block.position() + block.length() - 1;
}

/**
 * @brief 覆盖块的连接文本（块间以 
 连接，不含末块分隔符）
 *
 * 行级动作的文本都以这个形态参与替换：插入 
 经
 * QTextCursor::insertText 落成分隔符，与块结构天然一致；末块分隔符从不
 * 进串——它不是合法光标位能选到的东西。
 *
 * @param first 首块
 * @param last 末块
 * @return 各块文本以 
 连接的结果；first 无效时为空串
 */
QString joinedBlockText(const QTextBlock &first, const QTextBlock &last)
{
    QString text;
    for (QTextBlock block = first; block.isValid();
         block = block.next()) {
        if (!text.isEmpty()) {
            text += QLatin1Char('\n');
        }
        text += block.text();
        if (block == last) {
            break;
        }
    }
    return text;
}

WordSpan wordAround(const QTextCursor &cursor)
{
    WordSpan span;
    const QTextBlock block = cursor.block();
    if (!block.isValid()) {
        return span;
    }
    const QString text = block.text();
    const int blockStart = block.position();
    const int position = qBound(blockStart, cursor.position(),
                                blockStart + text.length());
    const auto isWordChar = [](QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('_');
    };
    int start = position - blockStart;
    int end = start;
    while (start > 0 && isWordChar(text.at(start - 1))) {
        --start;
    }
    while (end < text.length() && isWordChar(text.at(end))) {
        ++end;
    }
    span.start = blockStart + start;
    span.length = end - start;
    return span;
}

/**
 * @brief 标题前缀的统一升降级（promote/demote 共用）
 *
 * 升（delta = -1）：# 数减一，H1 再升 = 去标题（循环语义与 applyHeading
 * 的「同级再点去掉」一致，键盘连按可以从 H3 一路升回段落）。降（delta =
 * +1）：# 数加一，封顶 6。非标题行不动——普通行变成标题走 applyHeading。
 * 先扫一遍有没有可动的行，没有就不进编辑块（不留空撤销步）。
 *
 * @param cursor 选区光标；返回时其选区覆盖被处理的整段行
 * @param delta -1 升级 / +1 降级
 */
void relevelHeading(QTextCursor &cursor, int delta)
{
    QTextDocument *document = cursor.document();
    const BlockRange range = coveredBlocks(cursor);
    if (!document || !range.first.isValid() || !range.last.isValid()) {
        return;
    }

    static const QRegularExpression headingPrefix(
        QStringLiteral("^(#{1,6})\\s+"));
    bool anyChange = false;
    for (QTextBlock block = range.first; block.isValid(); block = block.next()) {
        const QRegularExpressionMatch match =
                headingPrefix.match(block.text());
        // 升级对任何标题行都会动；降级只对 H6 以内的行会动。
        if (match.hasMatch()
                && (delta < 0 || match.captured(1).length() < 6)) {
            anyChange = true;
            break;
        }
        if (block == range.last) {
            break;
        }
    }
    if (!anyChange) {
        return;
    }

    cursor.beginEditBlock();
    for (QTextBlock block = range.first; block.isValid(); block = block.next()) {
        const QRegularExpressionMatch match =
                headingPrefix.match(block.text());
        if (match.hasMatch()) {
            const int level = match.captured(1).length();
            const int newLevel = level + delta;
            QString replacement;
            if (newLevel < 1) {
                replacement = QString();   // H1 再升 = 去标题
            } else if (newLevel <= 6) {
                replacement = QString(newLevel, QLatin1Char('#'))
                        + QLatin1Char(' ');
            } else {
                replacement = match.captured(0);   // H6 再降：原样
            }
            if (replacement.length() != match.capturedLength()) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + match.capturedLength(),
                                   QTextCursor::KeepAnchor);
                cursor.insertText(replacement);
            }
        }
        if (block == range.last) {
            break;
        }
    }
    // 结果选区：整段被处理的行（块句柄是活坐标，与 applyHeadingToCursor
    // 同一条收尾——按旧句柄的现值重查首末块）。
    cursor.setPosition(
                document->findBlock(range.first.position()).position());
    const QTextBlock newLast =
            document->findBlock(range.last.position() + 1);
    if (newLast.isValid()) {
        cursor.setPosition(newLast.position() + newLast.length() - 1,
                           QTextCursor::KeepAnchor);
    }
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
 * @brief 给编辑器的选区加下划线标记
 *
 * markdown 没有下划线的纯文本标记（GFM 把 _ 留给斜体），<u> 是唯一
 * 跨渲染器可移植的写法；预览侧对它做窄白名单放行（见
 * markdown-config.js），html:false 的注入面不因此扩大。
 *
 * @param textArea 目标编辑器
 * @sa applyUnderlineToCursor
 */
void MarkdownEdit::applyUnderline(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::applyUnderlineToCursor);
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
 * @brief 在指定位置插入一段 Markdown 文本
 *
 * position >= 0 时把光标移到该位置再插入（原有选区被收起）；position < 0
 * 时使用编辑器当前选区——有选区即替换。整段文本一次 insertText，只产生
 * 一步撤销，批量插图因此可以整体撤销。
 *
 * @param textArea 目标编辑器
 * @param position 插入位置；负数表示使用当前选区
 * @param text 要插入的 Markdown 文本
 * @return 文本非空且编辑器文档可用时为真
 * @sa insertMarkdownToCursor
 */
bool MarkdownEdit::insertMarkdown(QObject *textArea, int position,
                                  const QString &text)
{
    if (!textArea || text.isEmpty()) {
        return false;
    }
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument()) {
        return false;
    }

    QTextCursor cursor(quickDocument->textDocument());
    if (position >= 0) {
        cursor.setPosition(qBound(0, position,
                                  quickDocument->textDocument()
                                              ->characterCount()
                                          - 1));
    } else {
        cursor.setPosition(textArea->property("selectionStart").toInt());
        cursor.setPosition(textArea->property("selectionEnd").toInt(),
                           QTextCursor::KeepAnchor);
    }
    if (!insertMarkdownToCursor(cursor, text)) {
        return false;
    }
    QMetaObject::invokeMethod(textArea, "select",
                              Q_ARG(int, cursor.position()),
                              Q_ARG(int, cursor.position()));
    return true;
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
 * @brief Tab 键的编辑器行为
 *
 * @param textArea 目标编辑器
 * @param useSpaces true = 插空格；false = 插 \t
 * @param spacesPerIndent 每级缩进的空格数
 * @sa handleTabToCursor
 */
void MarkdownEdit::handleTab(QObject *textArea, bool useSpaces,
                             int spacesPerIndent)
{
    IndentOptions options;
    options.useSpaces = useSpaces;
    options.spacesPerIndent = spacesPerIndent;
    editTextArea(textArea, &MarkdownEdit::handleTabToCursor, options);
}

/**
 * @brief Shift+Tab 键的编辑器行为
 *
 * @param textArea 目标编辑器
 * @param useSpaces 缩进偏好：空格模式（决定反缩进一次退几个空格）
 * @param spacesPerIndent 每级缩进的空格数
 * @sa handleBacktabToCursor
 */
void MarkdownEdit::handleBacktab(QObject *textArea, bool useSpaces,
                                 int spacesPerIndent)
{
    IndentOptions options;
    options.useSpaces = useSpaces;
    options.spacesPerIndent = spacesPerIndent;
    editTextArea(textArea, &MarkdownEdit::handleBacktabToCursor, options);
}

/**
 * @brief 回车键的列表延续
 *
 * @param textArea 目标编辑器
 * @return 是否接管了这次回车（false = 调用方放行默认行为）
 * @sa smartEnterToCursor
 */
bool MarkdownEdit::handleEnter(QObject *textArea)
{
    if (!textArea) {
        return false;
    }
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument()) {
        return false;
    }
    QTextCursor cursor(quickDocument->textDocument());
    cursor.setPosition(textArea->property("selectionStart").toInt());
    cursor.setPosition(textArea->property("selectionEnd").toInt(),
                       QTextCursor::KeepAnchor);
    const bool handled = smartEnterToCursor(cursor);
    if (handled) {
        QMetaObject::invokeMethod(textArea, "select",
                                  Q_ARG(int, cursor.selectionStart()),
                                  Q_ARG(int, cursor.selectionEnd()));
    }
    return handled;
}

/**
 * @brief 输入单个字符时的自动配对
 *
 * @param textArea 目标编辑器
 * @param typed 输入的文本（只有单字符才可能被接管）
 * @return 是否接管（false = 调用方放行默认输入）
 * @sa autoTypeToCursor
 */
bool MarkdownEdit::autoType(QObject *textArea, const QString &typed)
{
    if (!textArea || typed.length() != 1) {
        return false;
    }
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument()) {
        return false;
    }
    QTextCursor cursor(quickDocument->textDocument());
    cursor.setPosition(textArea->property("selectionStart").toInt());
    cursor.setPosition(textArea->property("selectionEnd").toInt(),
                       QTextCursor::KeepAnchor);
    const bool handled = autoTypeToCursor(cursor, typed);
    if (handled) {
        QMetaObject::invokeMethod(textArea, "select",
                                  Q_ARG(int, cursor.selectionStart()),
                                  Q_ARG(int, cursor.selectionEnd()));
    }
    return handled;
}

/**
 * @brief 选区覆盖到的行与上一行交换
 * @param textArea 目标编辑器
 * @sa moveLineUpToCursor
 */
void MarkdownEdit::moveLineUp(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::moveLineUpToCursor);
}

/**
 * @brief 选区覆盖到的行与下一行交换
 * @param textArea 目标编辑器
 * @sa moveLineDownToCursor
 */
void MarkdownEdit::moveLineDown(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::moveLineDownToCursor);
}

/**
 * @brief 覆盖到的行在下方复制一份
 * @param textArea 目标编辑器
 * @sa duplicateLineToCursor
 */
void MarkdownEdit::duplicateLine(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::duplicateLineToCursor);
}

/**
 * @brief 删掉覆盖到的整行
 * @param textArea 目标编辑器
 * @sa deleteLineToCursor
 */
void MarkdownEdit::deleteLine(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::deleteLineToCursor);
}

/**
 * @brief 选区扩展到覆盖到的整行
 * @param textArea 目标编辑器
 * @sa selectLineToCursor
 */
void MarkdownEdit::selectLine(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::selectLineToCursor);
}

/**
 * @brief 选中光标处的词
 * @param textArea 目标编辑器
 * @sa selectWordToCursor
 */
void MarkdownEdit::selectWord(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::selectWordToCursor);
}

/**
 * @brief 选区文本的大小写转换
 * @param textArea 目标编辑器
 * @param mode CaseMode 之一（UpperCase / LowerCase / TitleCase）
 * @sa convertCaseToCursor
 */
void MarkdownEdit::convertCase(QObject *textArea, int mode)
{
    editTextArea(textArea, &MarkdownEdit::convertCaseToCursor, mode);
}

/**
 * @brief 标题升级（# 数减一，H1 再升去标题）
 * @param textArea 目标编辑器
 * @sa promoteHeadingToCursor
 */
void MarkdownEdit::promoteHeading(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::promoteHeadingToCursor);
}

/**
 * @brief 标题降级（# 数加一，封顶 6）
 * @param textArea 目标编辑器
 * @sa demoteHeadingToCursor
 */
void MarkdownEdit::demoteHeading(QObject *textArea)
{
    editTextArea(textArea, &MarkdownEdit::demoteHeadingToCursor);
}

/**
 * @brief 切换加粗标记
 *
 * A5 起是切换语义：选区已被 ** 夹住（去空白后恰好对齐）→ 去标记并保持
 * 内容选中；光标在 ** 骨架里或被 **..** 围住 → 删掉这对标记；其余情况
 * 维持原行为（空选区插入 **** 光标居中，有选区包裹后重选）。整个动作在
 * 一个编辑块内，一次撤销即可回退。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyBoldToCursor(QTextCursor &cursor)
{
    toggleWrapToCursor(cursor, QLatin1Char('*'), 2);
}

/**
 * @brief 切换斜体标记（单个 *；** 里的光标不算斜体内）
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyItalicToCursor(QTextCursor &cursor)
{
    toggleWrapToCursor(cursor, QLatin1Char('*'), 1);
}

/**
 * @brief 切换删除线标记（~~）
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyStrikeToCursor(QTextCursor &cursor)
{
    toggleWrapToCursor(cursor, QLatin1Char('~'), 2);
}

/**
 * @brief 切换下划线标记（<u>..</u>）
 *
 * markdown 没有下划线的纯文本标记，<u> 是唯一可移植写法。切换语义与
 * 对称标记一致：选区被 <u>/</u> 恰好夹住 → 去标记；光标在骨架里或被
 * 一对标记围住 → 删掉这对；否则包裹/插骨架。开闭标记长度不同（3/4），
 * 配对按块内文本先后交替（与查询侧 isUnderlineAtCursor 同一条纪律）。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyUnderlineToCursor(QTextCursor &cursor)
{
    const QString open = QStringLiteral("<u>");
    const QString close = QStringLiteral("</u>");
    QTextDocument *document = cursor.document();

    if (cursor.hasSelection()) {
        const int selectionStart = cursor.selectionStart();
        const int selectionEnd = cursor.selectionEnd();
        const TrimmedSelection trimmed = trimWhitespace(selectedText(cursor));
        const int wrapStart = selectionStart + trimmed.start;
        const int wrapEnd = wrapStart + trimmed.length;
        const auto isTextAt = [&](int at, const QString &text) {
            for (int i = 0; i < text.length(); ++i) {
                if (at + i < 0 || at + i >= document->characterCount()) {
                    return false;
                }
                if (document->characterAt(at + i) != text.at(i)) {
                    return false;
                }
            }
            return true;
        };
        if (isTextAt(wrapStart - open.length(), open)
            && isTextAt(wrapEnd, close)) {
            cursor.beginEditBlock();
            cursor.setPosition(wrapEnd);
            cursor.setPosition(wrapEnd + close.length(),
                                QTextCursor::KeepAnchor);
            cursor.insertText(QString());
            cursor.setPosition(wrapStart - open.length());
            cursor.setPosition(wrapStart, QTextCursor::KeepAnchor);
            cursor.insertText(QString());
            cursor.setPosition(wrapStart - open.length());
            cursor.setPosition(wrapEnd - open.length(),
                               QTextCursor::KeepAnchor);
            cursor.endEditBlock();
            return;
        }
        // 包裹路径沿用对称标记的通用包裹（空白留在标记外、重选内容）。
        cursor.beginEditBlock();
        cursor.setPosition(wrapStart);
        cursor.insertText(open);
        cursor.setPosition(wrapEnd + open.length());
        cursor.insertText(close);
        cursor.setPosition(wrapStart + open.length());
        cursor.setPosition(wrapEnd + open.length(),
                           QTextCursor::KeepAnchor);
        cursor.endEditBlock();
        return;
    }

    // 空选区：先找围住光标的那对 <u>..</u>（骨架态是它的空内容特例）。
    const QTextBlock block = cursor.block();
    const int position = cursor.position();
    if (block.isValid()) {
        const int blockStart = block.position();
        const QString text = block.text();
        int contentStart = -1;
        for (int scan = 0; scan < text.length();) {
            if (contentStart >= 0) {
                const int closeHit = text.indexOf(close, scan);
                if (closeHit < 0) {
                    break;
                }
                if (position >= blockStart + contentStart
                    && position <= blockStart + closeHit) {
                    unwrapRange(cursor, blockStart + contentStart
                                    - open.length(),
                                blockStart + contentStart,
                                blockStart + closeHit,
                                blockStart + closeHit + close.length());
                    return;
                }
                scan = closeHit + close.length();
                contentStart = -1;
            } else {
                const int openHit = text.indexOf(open, scan);
                if (openHit < 0) {
                    break;
                }
                contentStart = openHit + open.length();
                scan = contentStart;
            }
        }
    }

    const int at = cursor.position();
    cursor.beginEditBlock();
    cursor.insertText(open + close);
    cursor.setPosition(at + open.length());
    cursor.endEditBlock();
}

/**
 * @brief 切换代码标记
 *
 * 空选区：光标在 ` 骨架里或被 `..` 围住 → 删掉这对标记；否则插入 `` 并
 * 把光标放到中间。单行选区：恰好被 ` 夹住 → 去标记并保持内容选中；否则
 * 加行内代码 `text`（前后空白留在标记外）。含换行的选区改用独立围栏代
 * 码块（首尾各垫 \n```\n，选区首尾已带的换行不重复，包裹后重新选中原内
 * 容）——围栏不参与切换（块级形态的反向动作没有一致的语义）。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::applyCodeToCursor(QTextCursor &cursor)
{
    if (!cursor.hasSelection()) {
        toggleWrapToCursor(cursor, QLatin1Char('`'), 1);
        return;
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QString selected = selectedText(cursor);
    QTextDocument *document = cursor.document();

    if (!selected.contains(QLatin1Char('\n'))) {
        const TrimmedSelection trimmed = trimWhitespace(selected);
        const int wrapStart = selectionStart + trimmed.start;
        const int wrapEnd = wrapStart + trimmed.length;
        const bool wrapped = trimmed.length > 0
                && document->characterAt(wrapStart - 1) == QLatin1Char('`')
                && document->characterAt(wrapEnd) == QLatin1Char('`');
        if (wrapped) {
            cursor.beginEditBlock();
            cursor.setPosition(wrapEnd);
            cursor.setPosition(wrapEnd + 1, QTextCursor::KeepAnchor);
            cursor.insertText(QString());
            cursor.setPosition(wrapStart - 1);
            cursor.setPosition(wrapStart, QTextCursor::KeepAnchor);
            cursor.insertText(QString());
            cursor.setPosition(wrapStart - 1);
            cursor.setPosition(wrapEnd - 1, QTextCursor::KeepAnchor);
            cursor.endEditBlock();
            return;
        }
        cursor.beginEditBlock();
        cursor.setPosition(wrapStart);
        cursor.insertText(QStringLiteral("`"));
        cursor.setPosition(wrapEnd + 1);
        cursor.insertText(QStringLiteral("`"));
        cursor.setPosition(wrapStart + 1);
        cursor.setPosition(wrapEnd + 1, QTextCursor::KeepAnchor);
        cursor.endEditBlock();
        return;
    }

    cursor.beginEditBlock();
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
 * 围栏必须独占一行：动作永远按**整行**围栏——开围栏行插在首行行首、
 * 闭围栏行补在末行行尾，选区或光标所在行整体变成代码块内容（行内包裹
 * 不是合法的代码块，那是 applyCode 的行内形态）。
 * 唯一的例外是空选区落在空行上：插入围栏骨架（```lang + 空行 + ```），
 * 光标停在空行行首等着写内容。
 *
 * @param cursor 选区光标
 * @param language 语言标注（原样写入，调用方负责合法值）
 */
void MarkdownEdit::applyFenceToCursor(QTextCursor &cursor,
                                      const QString &language)
{
    const QString fence = QStringLiteral("```") + language;
    QTextDocument *document = cursor.document();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock firstBlock = document->findBlock(selectionStart);
    // 选区末尾恰好贴在换行后（光标位于下一行行首）时那行没选到内容，
    // 围栏只罩到有内容的最后一行（与 applyBullet 同一条边界纪律）。
    const QTextBlock lastBlock = document->findBlock(
                selectionEnd > selectionStart ? selectionEnd - 1
                                              : selectionStart);
    if (!firstBlock.isValid() || !lastBlock.isValid()) {
        return;
    }

    // 空选区 + 空行：围栏骨架，光标停在两道围栏之间。
    if (!cursor.hasSelection() && firstBlock.text().isEmpty()) {
        const int position = firstBlock.position();
        cursor.beginEditBlock();
        cursor.insertText(fence + QLatin1Char('\n') + QLatin1Char('\n')
                          + QStringLiteral("```"));
        cursor.setPosition(position + fence.length() + 1);
        cursor.endEditBlock();
        return;
    }

    // 块 length 含尾部分隔符（末块也含一份），文字终点 = position+length-1。
    // 这两个终点都在开围栏插入**之前**算出（旧坐标）：QTextBlock 句柄读的是
    // 活坐标，插入之后再读会拿到平移后的值，再补 openLength 就会双重偏移
    // （单块场景下句柄还会指到新围栏块）。纯位置算术不受插入影响：
    // 开围栏在 firstPos 处插入 openLength 个字符，其后一切坐标平移那么多。
    const int firstPos = firstBlock.position();
    const int lineEnd = lastBlock.position() + lastBlock.length() - 1;

    cursor.beginEditBlock();
    // 开围栏插在首行行首：行内已有文字被推到下一行，围栏独占一行。
    cursor.setPosition(firstPos);
    cursor.insertText(fence + QLatin1Char('\n'));
    const int openLength = fence.length() + 1;
    // 闭围栏补在末行文字之后：行尾分隔保留在闭围栏后面，围栏自成一行。
    cursor.setPosition(lineEnd + openLength);
    cursor.insertText(QStringLiteral("\n```"));
    // 结果选区：围栏之间的内容行（键入即替换，与 applyCode 的换行分支
    // 同一交互）。
    const int contentStart = firstPos + openLength;
    const int contentEnd = lineEnd + openLength;
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
 * @brief 在光标处插入文本（有选区则替换）
 *
 * QTextCursor::insertText 在光标持有选区时本身就是替换，因此插入图片
 * 语法这类整段文本只产生一步撤销，不需要先删选区再插。
 *
 * @param cursor 光标；返回时位于插入文本末尾
 * @param text 要插入的文本
 * @return 文本非空时为真
 */
bool MarkdownEdit::insertMarkdownToCursor(QTextCursor &cursor,
                                          const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    cursor.insertText(text);
    return true;
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
 * @brief Tab 键的编辑器行为（VSCode 语义）
 *
 * 多行选区（选区起点与终点落在不同块上）是行级缩进：覆盖到的每行行首
 * 插一个缩进单位（由 options 决定：空格 × 每级宽度，或字面 \t），完成
 * 后重新选中整段（含新增缩进），反复按 Tab 连续右移。无选区或单行选区
 * 保持编辑器原生语义：插入一个缩进单位（单行选区被替换）。选区末尾恰
 * 好贴在换行后（光标位于下一行行首）时那行没选到内容，不参与缩进（与
 * applyBullet 同一条边界纪律）。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 * @param options 缩进偏好（空格/\t 与每级空格数）
 */
void MarkdownEdit::handleTabToCursor(QTextCursor &cursor,
                                     const IndentOptions &options)
{
    const int width = qBound(1, options.spacesPerIndent, 8);
    const QString unit = options.useSpaces
            ? QString(width, QLatin1Char(' '))
            : QStringLiteral("\t");
    if (!cursor.hasSelection()) {
        cursor.insertText(unit);
        return;
    }
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
    cursor.beginEditBlock();
    if (firstBlock == lastBlock) {
        // 单行选区：原生替换语义（删选区、插一个缩进单位）。
        cursor.removeSelectedText();
        cursor.insertText(unit);
    } else {
        for (QTextBlock block = firstBlock; block.isValid();
             block = block.next()) {
            cursor.setPosition(block.position());
            cursor.insertText(unit);
            if (block == lastBlock) {
                break;
            }
        }
        cursor.setPosition(firstBlock.position());
        cursor.setPosition(lastBlock.position() + lastBlock.length() - 1,
                           QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
}

/**
 * @brief Shift+Tab 键的编辑器行为（Tab 的反向动作）
 *
 * 覆盖到的每行行首去掉一个缩进单位（一个 Tab，或至多一个单位的空格，
 * 见 leadingIndentLength）。与 handleTabToCursor 的行范围同一条边界纪律：
 * 选区末尾恰好贴在换行后的那行没选到内容，不参与反缩进。没有选区时
 * 作用于光标所在行——「Shift+Tab = 退一级」是编辑器的普遍预期，而单插
 * 一个缩进单位退不回任何东西。
 *
 * 选区端点随内容左移（只减掉落在它前面的缩进，光标停在缩进内部时减到
 * 行首为止），所以多行选区反复按 Shift+Tab 会逐级左移；整段都没有缩进
 * 时原样返回，不留一个空撤销步。删除从后往前做，前面各行的位置在删除
 * 过程中保持有效。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 * @param options 缩进偏好（空格模式下一次退一个单位宽的空格）
 */
void MarkdownEdit::handleBacktabToCursor(QTextCursor &cursor,
                                         const IndentOptions &options)
{
    QTextDocument *document = cursor.document();
    if (!document) {
        return;
    }
    const int spaceUnit = options.useSpaces
            ? qBound(1, options.spacesPerIndent, 8) : 4;
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

    struct Removal
    {
        int position = 0;  ///< 行首位置（文档坐标）
        int length = 0;    ///< 要去掉的字符数
    };

    QList<Removal> removals;
    for (QTextBlock block = firstBlock; block.isValid(); block = block.next()) {
        const int indent = leadingIndentLength(block.text(), spaceUnit);
        if (indent > 0) {
            removals.append({block.position(), indent});
        }
        if (block == lastBlock) {
            break;
        }
    }
    if (removals.isEmpty()) {
        return;
    }

    // 端点左移量：只算完全落在它前面的行，本行内算到端点为止。
    const auto shifted = [&removals](int position) {
        int removed = 0;
        for (const Removal &removal : std::as_const(removals)) {
            if (removal.position >= position) {
                break;
            }
            removed += qMin(removal.length, position - removal.position);
        }
        return position - removed;
    };
    const int newAnchor = shifted(cursor.anchor());
    const int newPosition = shifted(cursor.position());

    cursor.beginEditBlock();
    for (int index = removals.size() - 1; index >= 0; --index) {
        const Removal &removal = removals.at(index);
        cursor.setPosition(removal.position);
        cursor.setPosition(removal.position + removal.length,
                           QTextCursor::KeepAnchor);
        cursor.insertText(QString());
    }
    // 先落锚点再带保持落位置，反向选区也保持原方向。
    cursor.setPosition(newAnchor);
    cursor.setPosition(newPosition, QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 回车延续列表 / 编号 / 引用 / 任务项（A3）
 *
 * 光标（无选区）落在带结构前缀的行上时：内容非空 → 插入换行并带上同样
 * 的前缀（编号递增、任务项回到未勾选、缩进保留）；内容为空（只剩前缀）
 * → 退出该结构：删掉前缀只留一个空行（再按一次回车就是普通空行）。行
 * 上没有结构前缀、或光标带着选区时不接管（调用方放行默认行为）。
 *
 * 前缀只认行首：引用 `>` 可叠写（`>> ` 一并延续），任务项认
 * `- [x] `/`- [ ] ` 的勾选两态。
 *
 * @param cursor 选区光标
 * @return 是否接管了这次回车
 */
bool MarkdownEdit::smartEnterToCursor(QTextCursor &cursor)
{
    if (cursor.hasSelection()) {
        return false;
    }
    QTextDocument *document = cursor.document();
    if (!document) {
        return false;
    }
    const QTextBlock block = document->findBlock(cursor.position());
    if (!block.isValid()) {
        return false;
    }
    const QString text = block.text();

    // 任务项要在无序列表之前尝试（`- ` 会先吃掉 `- [ ] ` 的前半）。
    static const QRegularExpression structurePrefix(
        QStringLiteral(
            "^(\\s*)(?:(\\d+)\\. |([-*+]) \\[[xX]\\] |[-*+] |(>+?) ?)"));
    const QRegularExpressionMatch match = structurePrefix.match(text);
    if (!match.hasMatch()) {
        return false;
    }
    const QString indent = match.captured(1);
    // 前缀 = 整个匹配去掉缩进（captured(2) 是编号分支的数字，不能用它
    // 当通用前缀）。
    const QString prefix = match.captured(0).mid(indent.length());
    const int prefixLength = match.capturedLength();

    cursor.beginEditBlock();
    if (text.mid(prefixLength).trimmed().isEmpty()) {
        // 空项退出：删掉整段前缀（缩进 + 标记），留一个空行。
        cursor.setPosition(block.position());
        cursor.setPosition(block.position() + prefixLength,
                           QTextCursor::KeepAnchor);
        cursor.insertText(QString());
        cursor.setPosition(block.position());
    } else {
        QString nextPrefix = prefix;
        if (match.capturedLength(3) > 0) {
            // 任务项：延续为未勾选形态。
            nextPrefix = match.captured(3)
                    + QStringLiteral(" [ ] ");
        } else if (match.capturedLength(2) > 0) {
            // 编号列表：数字 + 1。
            const int number = match.captured(2).toInt();
            nextPrefix = QStringLiteral("%1. ").arg(number + 1);
        }
        cursor.insertText(QStringLiteral("\n") + indent + nextPrefix);
    }
    cursor.endEditBlock();
    return true;
}

/**
 * @brief 输入单个字符时的标记自动配对（A6）
 *
 * 对称标记（`*`、反引号、`~`）三态：光标后是同一字符 → 跳过闭合（光标
 * 右移一位，不插入）；光标夹在空对中间再输入同一字符 → 升级成双标记
 * （`*|*` → `**|**`，即斜体骨架升级加粗；反引号升级双反引号；`~` 升级
 * 删除线）；其余情况开新对（`X|X`）。括号类（`[`、`(`）插入对称括号把
 * 光标放到中间，`]`/`)` 在光标后正好有同一字符时跳过。带选区时不接管
 * （默认替换选区）；其余字符一律放行。
 *
 * @param cursor 选区光标（调用方保证无选区才有意义；带选区直接放行）
 * @param typed 输入文本（单字符）
 * @return 是否接管（true = 已插入配对/跳过，调用方吞掉默认输入）
 */
bool MarkdownEdit::autoTypeToCursor(QTextCursor &cursor, const QString &typed)
{
    if (typed.length() != 1 || cursor.hasSelection()) {
        return false;
    }
    QTextDocument *document = cursor.document();
    if (!document) {
        return false;
    }
    const QChar ch = typed.at(0);
    const int position = cursor.position();
    const auto charAt = [document](int at) {
        return (at >= 0 && at < document->characterCount())
                ? document->characterAt(at) : QChar();
    };

    if (ch == QLatin1Char('*') || ch == QLatin1Char('`')
        || ch == QLatin1Char('~')) {
        const QChar next = charAt(position);
        const QChar previous = charAt(position - 1);
        if (previous == ch && next == ch) {
            // 空对升级：X|X → XX|XX。
            cursor.beginEditBlock();
            cursor.insertText(typed);
            cursor.setPosition(position + 1);
            cursor.insertText(typed);
            cursor.setPosition(position + 1);
            cursor.endEditBlock();
            return true;
        }
        if (next == ch) {
            // 闭合字符跳过：光标越过它，不重复输入。
            cursor.setPosition(position + 1);
            return true;
        }
        cursor.beginEditBlock();
        cursor.insertText(typed + typed);
        cursor.setPosition(position + 1);
        cursor.endEditBlock();
        return true;
    }

    if (ch == QLatin1Char('[') || ch == QLatin1Char('(')) {
        const QChar close = (ch == QLatin1Char('[')) ? QLatin1Char(']')
                                                     : QLatin1Char(')');
        cursor.beginEditBlock();
        cursor.insertText(QString(ch) + QString(close));
        cursor.setPosition(position + 1);
        cursor.endEditBlock();
        return true;
    }

    if (ch == QLatin1Char(']') || ch == QLatin1Char(')')) {
        if (charAt(position) == ch) {
            cursor.setPosition(position + 1);
            return true;
        }
    }
    return false;
}

/**
 * @brief 覆盖到的行与上一行交换（Alt+↑）
 *
 * 实现按 span 交换：span = 上一行 + 分隔符 + 覆盖行，整段选中后替换成
 * 「覆盖行 + 分隔符 + 上一行」。只选中到末块**内容**边界（分隔符之后不
 * 是合法光标位——QTextCursor 的上限是 characterCount()-1），替换串自
 * 带分隔，原有分隔符原位保留，块结构不变。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::moveLineUpToCursor(QTextCursor &cursor)
{
    const BlockRange range = coveredBlocks(cursor);
    if (!range.first.isValid() || !range.last.isValid()) {
        return;
    }
    const QTextBlock previous = range.first.previous();
    if (!previous.isValid()) {
        return;
    }

    const int spanStart = previous.position();
    const int spanEnd = contentEndOf(range.last);
    const QString moved = joinedBlockText(range.first, range.last);
    const int previousLength = previous.length() - 1;
    const int oldAnchor = cursor.anchor();
    const int oldPosition = cursor.position();

    cursor.beginEditBlock();
    cursor.setPosition(spanStart);
    cursor.setPosition(spanEnd, QTextCursor::KeepAnchor);
    cursor.insertText(moved + QLatin1Char('\n') + previous.text());
    cursor.endEditBlock();

    // 端点随内容走：span = X（上一行）+ \n + Y（覆盖行）交换成
    // Y + \n + X。落在 X 里的端点平移 len(Y)+1，落在 Y 里的平移
    // -(len(X)+1)，span 外的不动；先落锚点再带锚落位置，方向保持。
    const auto shifted = [spanStart, spanEnd, previousLength,
                          &moved](int position) -> int {
        const int xEnd = spanStart + previousLength;
        const int yStart = xEnd + 1;
        if (position >= spanStart && position <= xEnd) {
            return spanStart + moved.length() + 1
                    + (position - spanStart);
        }
        if (position >= yStart && position <= spanEnd) {
            return spanStart + (position - yStart);
        }
        return position;
    };
    cursor.setPosition(shifted(oldAnchor));
    cursor.setPosition(shifted(oldPosition), QTextCursor::KeepAnchor);
}

/**
 * @brief 覆盖到的行与下一行交换（Alt+↓）
 *
 * 与 moveLineUpToCursor 对称：整段搬到下一行之后。删掉区间后下一块从
 * firstPos 起头（块句柄是活坐标，length 不受影响），插入点 = firstPos +
 * 下一块 length。已在文档底时原样返回。
 *
 * @param cursor 选区光标；返回时其位置/选区即动作结果
 */
void MarkdownEdit::moveLineDownToCursor(QTextCursor &cursor)
{
    const BlockRange range = coveredBlocks(cursor);
    if (!range.first.isValid() || !range.last.isValid()) {
        return;
    }
    const QTextBlock next = range.last.next();
    if (!next.isValid()) {
        return;
    }

    // span = 覆盖行 + 分隔符 + 下一行，交换成「下一行 + 分隔符 + 覆盖
    // 行」。终点收到下一块的**内容**末尾：末块分隔符之后不是合法光标位
    //（QTextCursor 的上限是 characterCount()-1），行级动作统一在内容
    // 边界上操作。
    const int spanStart = range.first.position();
    const int spanEnd = contentEndOf(next);
    const QString moved = joinedBlockText(range.first, range.last);
    const int nextLength = next.length() - 1;
    const int oldAnchor = cursor.anchor();
    const int oldPosition = cursor.position();

    cursor.beginEditBlock();
    cursor.setPosition(spanStart);
    cursor.setPosition(spanEnd, QTextCursor::KeepAnchor);
    cursor.insertText(next.text() + QLatin1Char('\n') + moved);
    cursor.endEditBlock();

    // 端点重算与 moveLineUpToCursor 同一条：X = 覆盖行、Y = 下一行，
    // 交换后 Y 在前。
    const auto shifted = [spanStart, spanEnd, nextLength,
                          &moved](int position) -> int {
        const int xEnd = spanStart + moved.length();
        const int yStart = xEnd + 1;
        if (position >= spanStart && position <= xEnd) {
            return spanStart + nextLength + 1
                    + (position - spanStart);
        }
        if (position >= yStart && position <= spanEnd) {
            return spanStart + (position - yStart);
        }
        return position;
    };
    cursor.setPosition(shifted(oldAnchor));
    cursor.setPosition(shifted(oldPosition), QTextCursor::KeepAnchor);
}

/**
 * @brief 覆盖到的行在下方复制一份（Shift+Alt+↓）
 *
 * 副本插在原段紧后（含末行分隔符，副本自成完整行），选区落到副本上——
 * 连续按住可以把同一行滚成多份，继续输入作用于最后那份。
 *
 * @param cursor 选区光标；返回时其选区落在副本上
 */
void MarkdownEdit::duplicateLineToCursor(QTextCursor &cursor)
{
    const BlockRange range = coveredBlocks(cursor);
    QTextDocument *document = cursor.document();
    if (!range.first.isValid() || !range.last.isValid() || !document) {
        return;
    }
    const int firstPos = range.first.position();
    const QString copy = joinedBlockText(range.first, range.last);
    const int relAnchor = clampedOffset(cursor.anchor(), firstPos,
                                        copy.length());
    const int relPosition = clampedOffset(cursor.position(), firstPos,
                                         copy.length());

    // 副本插在整段紧后。覆盖段不在文档末尾时插入点 = 下一块行首（合法
    // 光标位），插入「副本 + 分隔」；覆盖段就是末块时，它的分隔符之后没
    // 有光标位，改在内容末尾插入「分隔 + 副本」——原分隔符变成覆盖段的
    // 收尾，副本顶到文档末尾。
    const bool atEnd = range.last == document->lastBlock();
    const int insertAt = atEnd ? contentEndOf(range.last)
                               : range.last.position()
                                       + range.last.length();
    const QString inserted = atEnd
            ? QLatin1Char('\n') + copy : copy + QLatin1Char('\n');
    const int copyStart = atEnd ? insertAt + 1 : insertAt;

    cursor.beginEditBlock();
    cursor.setPosition(insertAt);
    cursor.insertText(inserted);
    cursor.setPosition(copyStart + relAnchor);
    cursor.setPosition(copyStart + relPosition, QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 删掉覆盖到的整行（Ctrl+Shift+K）
 *
 * 连同行尾分隔符一起删（不留空行），光标落到紧随其后的行行首；删的是
 * 文档末尾的行时停在文档末尾。
 *
 * @param cursor 选区光标；返回时是无选区的纯光标
 */
void MarkdownEdit::deleteLineToCursor(QTextCursor &cursor)
{
    const BlockRange range = coveredBlocks(cursor);
    QTextDocument *document = cursor.document();
    if (!range.first.isValid() || !range.last.isValid() || !document) {
        return;
    }
    const int firstPos = range.first.position();

    // 删除边界：覆盖段不在文档末尾时删到下一块行首（吃掉末行分隔符）；
    // 覆盖段是末块时只能删到内容末尾（分隔符之后没有光标位），改吃上一
    // 行的分隔符补位，文档末尾不留空行。
    const bool atEnd = range.last == document->lastBlock();
    const bool atStart = range.first == document->firstBlock();
    int removeStart = firstPos;
    int removeEnd = atEnd ? contentEndOf(range.last)
                          : range.last.position()
                                  + range.last.length();
    if (atEnd && !atStart) {
        removeStart = firstPos - 1;
    }

    cursor.beginEditBlock();
    cursor.setPosition(removeStart);
    cursor.setPosition(removeEnd, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.setPosition(removeStart);
    cursor.endEditBlock();
}

/**
 * @brief 选区扩展到覆盖到的整行（Ctrl+L）
 *
 * 起点扩到首行行首，终点收到末行行尾（不含换行）。无选区 = 选中光标所
 * 在行。纯选区操作，不改文档、不进编辑块。
 *
 * @param cursor 选区光标
 */
void MarkdownEdit::selectLineToCursor(QTextCursor &cursor)
{
    const BlockRange range = coveredBlocks(cursor);
    if (!range.first.isValid() || !range.last.isValid()) {
        return;
    }
    const int start = range.first.position();
    const int end = range.last.position() + range.last.length() - 1;
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
}

/**
 * @brief 选中光标处的词（Ctrl+D）
 *
 * 词 = 字母/数字/下划线连成的段；光标贴在词边界时取右侧的词；不在词上
 * （两侧都是空白/标点）时原样返回。已有选区时保持不动——这个动作的语义
 * 是「把光标处的东西选出来」，不是「扩选」。
 *
 * @param cursor 光标
 */
void MarkdownEdit::selectWordToCursor(QTextCursor &cursor)
{
    if (cursor.hasSelection()) {
        return;
    }
    const WordSpan word = wordAround(cursor);
    if (word.length == 0) {
        return;
    }
    cursor.setPosition(word.start);
    cursor.setPosition(word.start + word.length, QTextCursor::KeepAnchor);
}

/**
 * @brief 选区文本的大小写转换
 *
 * 有选区：只转选区文本，换行与空白原样保留。无选区：转光标处的词（词
 * 语义与 selectWordToCursor 同一条）；光标不在词上时原样返回。三种形态：
 * UpperCase 全大写、LowerCase 全小写、TitleCase 词首大写其余小写（词首
 * = 前一个字符不是字母/数字）。转换后选区落在被转换的区间上。
 *
 * @param cursor 选区光标
 * @param mode CaseMode 之一
 */
void MarkdownEdit::convertCaseToCursor(QTextCursor &cursor, int mode)
{
    if (mode < UpperCase || mode > TitleCase) {
        return;
    }
    int start = 0;
    int length = 0;
    if (cursor.hasSelection()) {
        start = cursor.selectionStart();
        length = cursor.selectionEnd() - start;
    } else {
        const WordSpan word = wordAround(cursor);
        if (word.length == 0) {
            return;
        }
        start = word.start;
        length = word.length;
    }
    if (length == 0) {
        return;
    }

    QTextDocument *document = cursor.document();
    if (!document) {
        return;
    }
    QTextCursor reader(document);
    reader.setPosition(start);
    reader.setPosition(start + length, QTextCursor::KeepAnchor);
    const QString source = reader.selectedText();

    const auto isWordChar = [](QChar ch) {
        return ch.isLetterOrNumber();
    };
    QString converted;
    converted.resize(source.length());
    for (int i = 0; i < source.length(); ++i) {
        const QChar ch = source.at(i);
        QChar out = ch;
        if (mode == UpperCase) {
            out = ch.toUpper();
        } else if (mode == LowerCase) {
            out = ch.toLower();
        } else {
            const bool wordStart = i == 0 || !isWordChar(source.at(i - 1));
            out = wordStart ? ch.toUpper() : ch.toLower();
        }
        converted[i] = out;
    }
    if (converted == source) {
        return;   // 已经是目标形态：不留空撤销步
    }

    cursor.beginEditBlock();
    reader.setPosition(start);
    reader.setPosition(start + length, QTextCursor::KeepAnchor);
    reader.insertText(converted);
    cursor.setPosition(start);
    cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

/**
 * @brief 标题升级（# 数减一，H1 再升去标题）
 * @param cursor 选区光标
 * @sa relevelHeading
 */
void MarkdownEdit::promoteHeadingToCursor(QTextCursor &cursor)
{
    relevelHeading(cursor, -1);
}

/**
 * @brief 标题降级（# 数加一，封顶 6）
 * @param cursor 选区光标
 * @sa relevelHeading
 */
void MarkdownEdit::demoteHeadingToCursor(QTextCursor &cursor)
{
    relevelHeading(cursor, 1);
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
    // 档还要走 regex）；判定统一走 needleMatchesAt，与计数/高亮同口径。
    for (int at = start; at + needle.length() <= haystack.length(); ++at) {
        if (needleMatchesAt(haystack, needle, at, caseSensitive,
                            wholeWords)) {
            result.start = at;
            result.length = needle.length();
            return result;
        }
    }
    // 尾部没找到：回卷到开头再找一遍（范围到 start 为止——start 之后
    // 的区域刚扫过）。
    for (int at = 0;
         at < start && at + needle.length() <= haystack.length(); ++at) {
        if (needleMatchesAt(haystack, needle, at, caseSensitive,
                            wholeWords)) {
            result.start = at;
            result.length = needle.length();
            result.wrapped = true;
            return result;
        }
    }
    return result;
}

/**
 * @brief 关键词在全文的命中总数（不重叠计数）
 *
 * 命中后跳过整个关键词再继续找——与 replaceAll 的替换口径一致（替换后
 * 的文本不再参与本轮计数），「3/12」的分母。
 *
 * @param document 文档（只读）
 * @param needle 关键词（空串返回 0）
 * @param caseSensitive 区分大小写
 * @param wholeWords 全词匹配
 * @return 命中总数
 */
int MarkdownEdit::countMatches(const QTextDocument *document,
                               const QString &needle, bool caseSensitive,
                               bool wholeWords)
{
    if (!document || needle.isEmpty()) {
        return 0;
    }
    const QString haystack = document->toPlainText();
    const int needleLength = needle.length();
    int count = 0;
    int at = 0;
    while (at + needleLength <= haystack.length()) {
        if (needleMatchesAt(haystack, needle, at, caseSensitive,
                            wholeWords)) {
            ++count;
            at += needleLength;
        } else {
            ++at;
        }
    }
    return count;
}

/**
 * @brief 从 from 起第一个命中的序号（1 起）
 *
 * 跳过 from 之前的命中（同样按不重叠口径数），「3/12」的分子：findNext
 * 选中一个命中后，用它的起点当 from，返回值即它在全集里的位置。
 *
 * @param document 文档（只读）
 * @param needle 关键词
 * @param from 计数起点（夹到 [0, length]）
 * @param caseSensitive 区分大小写
 * @param wholeWords 全词匹配
 * @return 1 起的序号；没有命中返回 0
 */
int MarkdownEdit::matchIndexAt(const QTextDocument *document,
                               const QString &needle, int from,
                               bool caseSensitive, bool wholeWords)
{
    if (!document || needle.isEmpty()) {
        return 0;
    }
    const QString haystack = document->toPlainText();
    const int start = qBound(0, from, haystack.length());
    const int needleLength = needle.length();
    int index = 0;
    int at = 0;
    while (at + needleLength <= haystack.length()) {
        if (needleMatchesAt(haystack, needle, at, caseSensitive,
                            wholeWords)) {
            ++index;
            if (at >= start) {
                return index;
            }
            at += needleLength;
        } else {
            ++at;
        }
    }
    return 0;
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
 * @brief QML 版命中计数（编辑器全文）
 * @sa countMatches
 */
int MarkdownEdit::findCount(QObject *textArea, const QString &needle,
                            bool caseSensitive, bool wholeWords) const
{
    if (!textArea)
        return 0;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return 0;
    return countMatches(quickDocument->textDocument(), needle,
                        caseSensitive, wholeWords);
}

/**
 * @brief QML 版命中序号（编辑器全文，1 起）
 * @sa matchIndexAt
 */
int MarkdownEdit::findIndexOf(QObject *textArea, const QString &needle,
                              int from, bool caseSensitive,
                              bool wholeWords) const
{
    if (!textArea)
        return 0;
    QQuickTextDocument *quickDocument =
            qvariant_cast<QQuickTextDocument *>(
                textArea->property("textDocument"));
    if (!quickDocument || !quickDocument->textDocument())
        return 0;
    return matchIndexAt(quickDocument->textDocument(), needle, from,
                        caseSensitive, wholeWords);
}

/**
 * @brief 给编辑器里全部命中叠背景高亮
 *
 * 经 MarkdownEdit::attach 挂着的语法高亮器完成（同一个高亮器实例上叠
 * 加，见 MarkdownHighlighter::setSearch）；编辑器没 attach 过（异常
 * 路径）时静默跳过。needle 为空 = 清除高亮。
 */
void MarkdownEdit::setSearchHighlight(QObject *textArea,
                                      const QString &needle,
                                      bool caseSensitive, bool wholeWords)
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
        if (highlighter->document() == document) {
            highlighter->setSearch(needle, caseSensitive, wholeWords);
            return;
        }
    }
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
    // 查找命中：accent 的低透明度背景叠在语法色之下（setFormat 是合并
    // 语义，只带背景色的格式不盖前景色），深浅两套主题下都可读。
    colors.search = theme->alpha(theme->accent(), 0.30);
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

/**
 * @brief 带缩进偏好操作的通用路径（handleTab/handleBacktab）
 *
 * 与无参版同一套取文档/选区/写回纪律，只是操作多带缩进偏好参数。
 *
 * @param textArea 目标编辑器
 * @param operation 静态操作
 * @param options 缩进偏好（空格/\t 与每级空格数）
 */
void MarkdownEdit::editTextArea(
    QObject *textArea, void (*operation)(QTextCursor &, const IndentOptions &),
    const IndentOptions &options)
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
    operation(cursor, options);

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
 * @brief 光标处是否处于下划线内
 *
 * @param textArea 目标编辑器
 * @return 命中为真
 */
bool MarkdownEdit::isUnderlineAt(QObject *textArea) const
{
    return queryTextAreaBool(textArea, &MarkdownEdit::isUnderlineAtCursor);
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
 * @brief 光标处是否处于下划线内（<u>..</u> 内容区间）
 *
 * <u> 是三字符开标记 + 四字符闭标记，与单字符标记表（inlineMarkerRanges）
 * 不同形，这里按块文本独立配对：`<u>` 开、`</u>` 闭，交替配对（未闭合的
 * 开标记配上其后的下一个闭标记，宽松处理打字中间态）。判据同
 * insideMarker：光标/选区与内容区间相交即真。
 */
bool MarkdownEdit::isUnderlineAtCursor(const QTextCursor &cursor)
{
    const QTextBlock block = cursor.block();
    if (!block.isValid()) {
        return false;
    }
    const int blockStart = block.position();
    const QString text = block.text();

    const QString open = QStringLiteral("<u>");
    const QString close = QStringLiteral("</u>");
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();

    int scan = 0;
    // 当前配对的内容区间起点（开标记之后的文档位置）；-1 = 还在标记外。
    int contentStart = -1;
    while (scan < text.length()) {
        if (contentStart >= 0) {
            const int closeHit = text.indexOf(close, scan);
            if (closeHit < 0) {
                break;
            }
            const int contentEnd = blockStart + closeHit;
            if (!cursor.hasSelection()) {
                if ((start > contentStart && start <= contentEnd)
                    || (start >= contentStart && start < contentEnd)) {
                    return true;
                }
            } else {
                if (end > contentStart && start < contentEnd) {
                    return true;
                }
            }
            scan = closeHit + close.length();
            contentStart = -1;
        } else {
            const int openHit = text.indexOf(open, scan);
            if (openHit < 0) {
                break;
            }
            contentStart = blockStart + openHit + open.length();
            scan = openHit + open.length();
        }
    }
    return false;
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

} // namespace awb::markdownedit
