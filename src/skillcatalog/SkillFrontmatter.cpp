#include "skillcatalog/SkillFrontmatter.h"

#include <QRegularExpression>
#include <QStringList>
#include <utility>

namespace awb::skillcatalog {

namespace {

/**
 * @brief 剥掉值文本最外层的一对配对引号
 *
 * 双引号内把 `\"` 还原成 `"`、`\\` 还原成 `\`；单引号内把 `''` 还原成 `'`。
 *
 * @param value 待清洗的值文本
 * @return 去掉配对引号并反转义后的值；不是引号包裹时按原样返回
 */
QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
        && value.endsWith(QLatin1Char('"'))) {
        QString inner = value.mid(1, value.size() - 2);
        inner.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        inner.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        return inner;
    }
    if (value.size() >= 2 && value.startsWith(QLatin1Char('\''))
        && value.endsWith(QLatin1Char('\''))) {
        QString inner = value.mid(1, value.size() - 2);
        inner.replace(QStringLiteral("''"), QStringLiteral("'"));
        return inner;
    }
    return value;
}

/**
 * @brief 取「key: value」行的匹配正则（value 可为空）
 *
 * 键名字符集只覆盖真实 skill 用到的那一小撮：字母、数字、点、横线、
 * 下划线。捕获组：1 = 行首缩进，2 = 键名，3 = 冒号之后的剩余部分。
 *
 * @return 静态持有的正则实例
 */
const QRegularExpression &keyLineRegex()
{
    static const QRegularExpression re(
        QStringLiteral("^([ \\t]*)([A-Za-z0-9_.-]+)\\s*:(.*)$"));
    return re;
}

/**
 * @brief 量出一行的缩进宽度
 *
 * @param line 原始行
 * @return 行首连续空格与制表符的个数
 */
int indentOf(const QString &line)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == QLatin1Char(' ')
                               || line.at(i) == QLatin1Char('\t'))) {
        ++i;
    }
    return i;
}

/**
 * @brief 判断一行是否为空白行或整行注释
 *
 * @param line 原始行
 * @return trim 后为空、或以 `#` 开头时返回 true
 */
bool isCommentOrBlank(const QString &line)
{
    const QString trimmed = line.trimmed();
    return trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'));
}

/**
 * @brief 把解析出的文本派发给 name / description / extras
 *
 * 只有 name 与 description 有专属字段，其余键一律进 extras。
 *
 * @param out 解析结果，就地更新
 * @param key 键名
 * @param value 原始值文本（写入前再 trim + unquote）
 */
void setValue(SkillFrontmatter &out, const QString &key, const QString &value)
{
    const QString clean = value.trimmed();
    if (key == QStringLiteral("name")) {
        out.name = unquote(clean);
    }
    else if (key == QStringLiteral("description")) {
        out.description = unquote(clean);
    }
    else {
        out.extras.insert(key, unquote(clean));
    }
}

/**
 * @brief 给键当前持有的值追加一条续行
 *
 * 续行（缩进文本或 `- item` 列表行）以空格拼到已有值后面——本子集
 * 不区分列表与多行文本，一律按文本收。
 *
 * @param out 解析结果，就地更新
 * @param key 正在收集的键名
 * @param piece 已 trim 的续行文本
 */
void appendValue(SkillFrontmatter &out, const QString &key,
                 const QString &piece)
{
    QString current;
    if (key == QStringLiteral("name")) {
        current = out.name;
    }
    else if (key == QStringLiteral("description")) {
        current = out.description;
    }
    else {
        current = out.extras.value(key);
    }

    const QString joined = current.isEmpty() ? piece
                                             : current + QLatin1Char(' ')
                                               + piece;
    setValue(out, key, joined);
}

} // namespace

/**
 * @brief 解析 SKILL.md 内容开头的 frontmatter 块
 *
 * 块必须从第一行 `---` 开始、以另一条 `---` 结束，未闭合的块按「没有
 * frontmatter」处理。嵌套映射展平成 parent.child 键；块标量（`>` 折叠 /
 * `|` 字面）按 YAML 规则折算成值文本。
 *
 * @param content SKILL.md 的完整文件内容
 * @return 解析结果；没有 frontmatter 块时 valid 为 false
 */
SkillFrontmatter SkillFrontmatterParser::parse(const QByteArray &content)
{
    SkillFrontmatter result;

    // 容忍 UTF-8 BOM 与 CRLF/CR 换行。
    QByteArray body = content;
    if (body.startsWith("\xEF\xBB\xBF")) {
        body.remove(0, 3);
    }
    QString text = QString::fromUtf8(body);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\r"), QStringLiteral("\n"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.isEmpty()) {
        return result;
    }
    // 块必须从第一行开始。
    if (lines.first().trimmed() != QStringLiteral("---")) {
        return result;
    }

    QString currentKey; // 值仍在续行收集中的键
    int currentIndent = 0;

    // 块标量（`>` 折叠 / `|` 字面）的解析状态。
    bool inBlock = false;
    bool folded = false;
    int blockKeyIndent = 0;
    int blockContentIndent = -1;
    QStringList blockLines;

    // 把攒下的块标量行折算成值文本写回 currentKey；块闭合或缩进回退时调用。
    auto flushBlock = [&]() {
        if (currentKey.isEmpty()) {
            return;
        }
        QString value;
        if (folded) {
            // `>` 把换行折成空格；空行保留为段落分隔。
            QStringList parts;
            for (const QString &raw : std::as_const(blockLines)) {
                if (raw.trimmed().isEmpty()) {
                    parts.append(QStringLiteral("\n"));
                }
                else {
                    parts.append(raw.trimmed());
                }
            }
            value = parts.join(QStringLiteral(" "));
            value.replace(QStringLiteral(" \n "), QStringLiteral("\n"));
            value.replace(QStringLiteral(" \n"), QStringLiteral("\n"));
            value.replace(QStringLiteral("\n "), QStringLiteral("\n"));
        } else {
            QStringList stripped;
            for (const QString &raw : std::as_const(blockLines)) {
                stripped.append(blockContentIndent >= 0
                                    ? raw.mid(blockContentIndent) : raw);
            }
            value = stripped.join(QStringLiteral("\n"));
        }
        setValue(result, currentKey, value);
        currentKey.clear();
        blockLines.clear();
        blockContentIndent = -1;
    };

    for (int i = 1; i < lines.size(); ++i) {
        const QString line = lines.at(i);

        if (line.trimmed() == QStringLiteral("---")) {
            if (inBlock) {
                flushBlock();
            }
            result.valid = true;
            return result;
        }

        if (inBlock) {
            const int indent = indentOf(line);
            const bool blank = line.trimmed().isEmpty();
            if (blank || indent > blockKeyIndent) {
                if (blockContentIndent < 0 && !blank) {
                    blockContentIndent = indent;
                }
                blockLines.append(line);
                continue;
            }
            // 缩进回退即块标量结束；本行交回下方按普通行重新处理。
            flushBlock();
            inBlock = false;
        }

        if (isCommentOrBlank(line)) {
            continue;
        }

        const QRegularExpressionMatch match = keyLineRegex().match(line);
        if (!match.hasMatch()) {
            // 当前键的续行：缩进文本或 `- item` 列表行（本子集一律按文本收）。
            if (!currentKey.isEmpty() && indentOf(line) > currentIndent) {
                appendValue(result, currentKey, line.trimmed());
            }
            continue;
        }

        const int indent = match.captured(1).length();
        const QString key = match.captured(2);
        const QString rest = match.captured(3).trimmed();

        if (!currentKey.isEmpty() && indent > currentIndent) {
            // 嵌套标量键 -> 按 parent.child 展平。
            const QString flatKey = currentKey + QLatin1Char('.') + key;
            if (rest.isEmpty()) {
                currentKey = flatKey;
                currentIndent = indent;
            } else {
                setValue(result, flatKey, rest);
            }
            continue;
        }

        if (rest.isEmpty()) {
            // 值（或嵌套子键）在后续行给出。
            currentKey = key;
            currentIndent = indent;
            continue;
        }

        if (rest == QLatin1Char('>') || rest == QLatin1Char('|')
            || rest.startsWith(QStringLiteral(">-"))
            || rest.startsWith(QStringLiteral(">+"))
            || rest.startsWith(QStringLiteral("|-"))
            || rest.startsWith(QStringLiteral("|+"))) {
            currentKey = key;
            currentIndent = indent;
            inBlock = true;
            folded = rest.startsWith(QLatin1Char('>'));
            blockKeyIndent = indent;
            blockContentIndent = -1;
            blockLines.clear();
            continue;
        }

        setValue(result, key, rest);
        currentKey.clear();
    }

    // 块未闭合 —— 按完全没有 frontmatter 处理。
    return SkillFrontmatter();
}

} // namespace awb::skillcatalog
