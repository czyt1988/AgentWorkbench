#include "skillcatalog/SkillFrontmatter.h"

#include <QRegularExpression>
#include <QStringList>

namespace awb::skillcatalog {

namespace {

// Strip one pair of matching quotes; '' / \" are unescaped inside.
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

// key: value (value may be empty) — the tiny subset used in real skills:
// letters, digits, dot, dash, underscore.
const QRegularExpression &keyLineRegex()
{
    static const QRegularExpression re(
        QStringLiteral("^([ \\t]*)([A-Za-z0-9_.-]+)\\s*:(.*)$"));
    return re;
}

int indentOf(const QString &line)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == QLatin1Char(' ')
                               || line.at(i) == QLatin1Char('\t')))
        ++i;
    return i;
}

bool isCommentOrBlank(const QString &line)
{
    const QString trimmed = line.trimmed();
    return trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'));
}

// Assign parsed text to name / description / extras.
void setValue(SkillFrontmatter &out, const QString &key, const QString &value)
{
    const QString clean = value.trimmed();
    if (key == QLatin1String("name"))
        out.name = unquote(clean);
    else if (key == QLatin1String("description"))
        out.description = unquote(clean);
    else
        out.extras.insert(key, unquote(clean));
}

// Append a continuation line (indented text or `- item`) to whatever the
// key currently holds.
void appendValue(SkillFrontmatter &out, const QString &key,
                 const QString &piece)
{
    QString current;
    if (key == QLatin1String("name"))
        current = out.name;
    else if (key == QLatin1String("description"))
        current = out.description;
    else
        current = out.extras.value(key);

    const QString joined = current.isEmpty() ? piece
                                             : current + QLatin1Char(' ')
                                               + piece;
    setValue(out, key, joined);
}

} // namespace

SkillFrontmatter SkillFrontmatterParser::parse(const QByteArray &content)
{
    SkillFrontmatter result;

    // Tolerate a UTF-8 BOM and CRLF/CR line endings.
    QByteArray body = content;
    if (body.startsWith("\xEF\xBB\xBF"))
        body.remove(0, 3);
    QString text = QString::fromUtf8(body);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\r"), QStringLiteral("\n"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.isEmpty())
        return result;
    // The block must start on the very first line.
    if (lines.first().trimmed() != QStringLiteral("---"))
        return result;

    QString currentKey; // key whose value is still being collected
    int currentIndent = 0;

    // Block scalar (`>` folded / `|` literal) state.
    bool inBlock = false;
    bool folded = false;
    int blockKeyIndent = 0;
    int blockContentIndent = -1;
    QStringList blockLines;

    auto flushBlock = [&]() {
        if (currentKey.isEmpty())
            return;
        QString value;
        if (folded) {
            // `>` folds line breaks into spaces; blank lines survive as
            // paragraph breaks.
            QStringList parts;
            for (const QString &raw : blockLines) {
                if (raw.trimmed().isEmpty())
                    parts.append(QStringLiteral("\n"));
                else
                    parts.append(raw.trimmed());
            }
            value = parts.join(QStringLiteral(" "));
            value.replace(QStringLiteral(" \n "), QStringLiteral("\n"));
            value.replace(QStringLiteral(" \n"), QStringLiteral("\n"));
            value.replace(QStringLiteral("\n "), QStringLiteral("\n"));
        } else {
            QStringList stripped;
            for (const QString &raw : blockLines) {
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
            if (inBlock)
                flushBlock();
            result.valid = true;
            return result;
        }

        if (inBlock) {
            const int indent = indentOf(line);
            const bool blank = line.trimmed().isEmpty();
            if (blank || indent > blockKeyIndent) {
                if (blockContentIndent < 0 && !blank)
                    blockContentIndent = indent;
                blockLines.append(line);
                continue;
            }
            // Dedent ends the block scalar; re-process this line below.
            flushBlock();
            inBlock = false;
        }

        if (isCommentOrBlank(line))
            continue;

        const QRegularExpressionMatch match = keyLineRegex().match(line);
        if (!match.hasMatch()) {
            // Continuation of the current key: indented text or a `- item`
            // list line (kept as text in this tiny subset).
            if (!currentKey.isEmpty() && indentOf(line) > currentIndent)
                appendValue(result, currentKey, line.trimmed());
            continue;
        }

        const int indent = match.captured(1).length();
        const QString key = match.captured(2);
        const QString rest = match.captured(3).trimmed();

        if (!currentKey.isEmpty() && indent > currentIndent) {
            // Nested scalar key -> flatten as parent.child.
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
            // The value (or nested children) follow on later lines.
            currentKey = key;
            currentIndent = indent;
            continue;
        }

        if (rest == QLatin1Char('>') || rest == QLatin1Char('|')
            || rest.startsWith(QLatin1String(">-"))
            || rest.startsWith(QLatin1String(">+"))
            || rest.startsWith(QLatin1String("|-"))
            || rest.startsWith(QLatin1String("|+"))) {
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

    // Unterminated block — treat as no frontmatter at all.
    return SkillFrontmatter();
}

} // namespace awb::skillcatalog
