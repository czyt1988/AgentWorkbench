#include "tools/PromptChecks.h"

#include <QCoreApplication>
#include <QPair>
#include <QRegularExpression>

namespace awb::tools {
namespace {

/**
 * @brief 规则表（id → 标题，顺序即文档表顺序）
 *
 * 标题是英文源串，界面层翻译。新增规则往表里加一行 + 单测一组正反例：
 * 规则 id 稳定（关开的持久化按它记），永远不要改已有 id 的拼写。
 */
struct RuleDef
{
    const char *id;
    const char *title;
};
const RuleDef kRules[] = {
    {"vague-wording", "Vague wording"},
    {"question-only", "Asks instead of instructing"},
    {"no-acceptance", "No acceptance criteria"},
    {"backslash-path", "Windows-style paths"},
    {"no-context", "No tech stack or files named"},
    {"token-risk", "Context budget risk"},
    {"flat-structure", "Long prompt without structure"},
    {"unfilled-placeholder", "Unfilled placeholders"},
    {"trailing-whitespace", "Trailing whitespace / blank runs"},
    {"smart-quotes", "Smart quotes and dashes"},
};

/** 模糊措辞词表（中英各一份；命中任意一个即记一条，区间取首个命中）。 */
const QString kVagueWords[] = {
    QStringLiteral("optimize a bit"), QStringLiteral("try your best"),
    QStringLiteral("as much as possible"), QStringLiteral("etc"),
    QStringLiteral("优化一下"), QStringLiteral("尽量"),
    QStringLiteral("看看"), QStringLiteral("等等"),
    QStringLiteral("最好"),
};

/** 验收类词汇（无任何一个命中 = 触发 no-acceptance）。 */
const QString kAcceptanceWords[] = {
    QStringLiteral("test"), QStringLiteral("expect"),
    QStringLiteral("verify"), QStringLiteral("assert"),
    QStringLiteral("pass"), QStringLiteral("验收"),
    QStringLiteral("测试"), QStringLiteral("期望"),
    QStringLiteral("断言"), QStringLiteral("通过"),
};

/** 语言/框架/文件线索（一个都没有 = 触发 no-context）。 */
const QString kContextHints[] = {
    QStringLiteral(".cpp"), QStringLiteral(".h"), QStringLiteral(".py"),
    QStringLiteral(".js"), QStringLiteral(".ts"), QStringLiteral(".rs"),
    QStringLiteral(".qml"), QStringLiteral(".json"), QStringLiteral("./"),
    QStringLiteral("src/"), QStringLiteral("test"), QStringLiteral("C++"),
    QStringLiteral("CMake"), QStringLiteral("Python"), QStringLiteral("Rust"),
    QStringLiteral("Qt"), QStringLiteral("Vue"), QStringLiteral("React"),
};

/**
 * @brief 智能引号/破折号区间扫描（trailing-whitespace 与 smart-quotes 用）
 */
QList<QPair<int, int>> lineRangesOf(const QString &text,
                                    bool (*predicate)(const QString &))
{
    QList<QPair<int, int>> result;
    int lineStart = 0;
    for (int i = 0; i <= text.length(); ++i) {
        const bool atEnd = i == text.length();
        const bool atNewline = !atEnd && text.at(i) == QLatin1Char('\n');
        if (atEnd || atNewline) {
            const QString line = text.mid(lineStart, i - lineStart);
            if (predicate(line)) {
                result.append({lineStart, i - lineStart});
            }
            lineStart = i + 1;
        }
    }
    return result;
}

bool hasTrailingWhitespace(const QString &line)
{
    return line.length() > 0 && line.at(line.length() - 1).isSpace();
}

bool hasSmartPunctuation(const QString &line)
{
    return line.contains(QChar(0x201C))   // "
        || line.contains(QChar(0x201D))   // "
        || line.contains(QChar(0x2018))   // '
        || line.contains(QChar(0x2019))   // '
        || line.contains(QChar(0x2014));  // —
}

/**
 * @brief 数行数（空串 = 0 行；结尾换行不另起一行）
 */
int lineCount(const QString &text)
{
    if (text.isEmpty()) {
        return 0;
    }
    int lines = 1;
    for (int i = 0; i < text.length(); ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++lines;
        }
    }
    // 结尾恰好是换行：最后一行是空的，不计。
    if (text.endsWith(QLatin1Char('\n'))) {
        --lines;
    }
    return lines;
}

} // namespace

/**
 * @brief 全部规则 id（稳定顺序）
 */
QStringList PromptChecks::ruleIds()
{
    QStringList ids;
    for (const RuleDef &rule : kRules) {
        ids.append(QString::fromLatin1(rule.id));
    }
    return ids;
}

/**
 * @brief 规则的显示名
 */
QString PromptChecks::ruleTitle(const QString &ruleId)
{
    for (const RuleDef &rule : kRules) {
        if (ruleId == QLatin1String(rule.id)) {
            return QString::fromLatin1(rule.title);
        }
    }
    return QString();
}

/**
 * @brief 跑全部规则
 *
 * 每条规则独立判定、独立给区间；空文本/过短文本大多不触发。区间语义：
 * 能定位的（模糊词、反斜杠路径、占位符）给具体区间；整体性规则（缺验收、
 * 无上下文、预算超限、结构扁平）给 -1（无区间，界面只列建议不跳转）。
 * 每条规则每次至多一条命中（模糊词/路径可能多处命中，只报第一处——
 * 清单式建议的语义是「有这类问题」而不是逐个列出）。
 */
QList<PromptCheck> PromptChecks::run(const QString &text)
{
    QList<PromptCheck> result;
    const bool empty = text.trimmed().isEmpty();
    const int tokens = ContextBudget::estimateTokens(text);
    // 预算超限的阈值留给调用方配置；纯层按一个保守默认（超过窗口的
    // 60% 即超限——按 32k 默认窗口即 ~19200 tokens）判定，调用方带
    // 自己的窗口来 level()，这里只发 token-risk 的「量级」命中。
    const int kTokenWarnThreshold = 19200;

    // --- vague-wording：模糊措辞 ---
    if (!empty) {
        for (const QString &word : kVagueWords) {
            const int index = text.indexOf(word, 0, Qt::CaseInsensitive);
            if (index >= 0) {
                PromptCheck check;
                check.ruleId = QStringLiteral("vague-wording");
                check.severity = 0;
                check.start = index;
                check.length = word.length();
                check.message = QStringLiteral(
                    "Vague wording here: replace with something verifiable.");
                result.append(check);
                break;
            }
        }
    }

    // --- question-only：问句为主且无祈使 ---
    if (!empty) {
        static const QRegularExpression questionEnd(
            QStringLiteral("[?？](\\s|$)"));
        static const QRegularExpression imperative(
            QStringLiteral(
                "(?m)^(write|add|fix|refactor|remove|make|use|run|list|"
                "explain|implement|check|改|写|加|删|跑|检查|实现)"),
            QRegularExpression::CaseInsensitiveOption);
        int questions = 0;
        int sentences = 0;
        QRegularExpressionMatchIterator it =
                questionEnd.globalMatch(text);
        while (it.hasNext()) {
            it.next();
            ++questions;
        }
        // 粗略句数：以 [.!?。！？\n] 结尾的段。
        static const QRegularExpression sentenceEnd(
            QStringLiteral("[.!?\n！？。]"));
        QRegularExpressionMatchIterator sentencesIt =
                sentenceEnd.globalMatch(text);
        while (sentencesIt.hasNext()) {
            sentencesIt.next();
            ++sentences;
        }
        sentences = qMax(sentences, questions);
        if (sentences > 0 && questions * 2 > sentences
            && !imperative.match(text).hasMatch()) {
            PromptCheck check;
            check.ruleId = QStringLiteral("question-only");
            check.severity = 0;
            check.start = -1;
            check.message = QStringLiteral(
                "Most sentences ask questions. Agents follow instructions "
                "better than answers.");
            result.append(check);
        }
    }

    // --- no-acceptance：无验收类词汇 ---
    if (!empty) {
        bool hasAcceptance = false;
        for (const QString &word : kAcceptanceWords) {
            if (text.contains(word, Qt::CaseInsensitive)) {
                hasAcceptance = true;
                break;
            }
        }
        if (!hasAcceptance) {
            PromptCheck check;
            check.ruleId = QStringLiteral("no-acceptance");
            check.severity = 1;
            check.start = -1;
            check.message = QStringLiteral(
                "No acceptance criteria: state what to run and what the "
                "expected result is.");
            result.append(check);
        }
    }

    // --- backslash-path：反斜杠路径 / 裸盘符 ---
    if (!empty) {
        static const QRegularExpression backslashPath(
            QStringLiteral("(\\.\\\\[\\w./\\\\-]+|[A-Za-z]:\\\\"
                           "[\\w./\\\\-]+)"));
        const QRegularExpressionMatch match =
                backslashPath.match(text);
        if (match.hasMatch()) {
            PromptCheck check;
            check.ruleId = QStringLiteral("backslash-path");
            check.severity = 1;
            check.start = match.capturedStart();
            check.length = match.capturedLength();
            check.message = QStringLiteral(
                "Windows-style path: use `./forward/slashes` so the agent "
                "does not trip on escaping.");
            result.append(check);
        }
    }

    // --- no-context：无语言/框架/文件线索 ---
    if (!empty) {
        bool hasContext = false;
        for (const QString &hint : kContextHints) {
            if (text.contains(hint, Qt::CaseInsensitive)) {
                hasContext = true;
                break;
            }
        }
        if (!hasContext) {
            PromptCheck check;
            check.ruleId = QStringLiteral("no-context");
            check.severity = 1;
            check.start = -1;
            check.message = QStringLiteral(
                "The agent does not know the stack: name the language, "
                "framework or files involved.");
            result.append(check);
        }
    }

    // --- token-risk：预算量级 ---
    if (tokens > kTokenWarnThreshold) {
        PromptCheck check;
        check.ruleId = QStringLiteral("token-risk");
        check.severity = 1;
        check.start = -1;
        check.message = QStringLiteral(
            "This prompt is long: prefer file references over inlined "
            "content.");
        result.append(check);
    }

    // --- flat-structure：长而无结构 ---
    if (!empty) {
        const int lines = lineCount(text);
        static const QRegularExpression structure(
            QStringLiteral("(?m)^(#{1,6}\\s|[-*+>]\\s|\\d+\\.\\s)"));
        const bool hasStructure =
                structure.match(text).hasMatch();
        if (lines > 40 && !hasStructure) {
            PromptCheck check;
            check.ruleId = QStringLiteral("flat-structure");
            check.severity = 0;
            check.start = -1;
            check.message = QStringLiteral(
                "Over 40 lines with no headings or lists: structure helps "
                "the agent find the point.");
            result.append(check);
        }
    }

    // --- unfilled-placeholder：还有 {{...}} ---
    if (!empty) {
        static const QRegularExpression placeholder(
            QStringLiteral("\\{\\{[^}]*\\}\\}"));
        const QRegularExpressionMatch match =
                placeholder.match(text);
        if (match.hasMatch()) {
            PromptCheck check;
            check.ruleId = QStringLiteral("unfilled-placeholder");
            check.severity = 0;
            check.start = match.capturedStart();
            check.length = match.capturedLength();
            check.message = QStringLiteral(
                "Unfilled placeholders remain: fill them or remove them "
                "before copying.");
            result.append(check);
        }
    }

    // --- trailing-whitespace：行尾空白 / 3+ 连续空行 ---
    if (!empty) {
        const QList<QPair<int, int>> lines =
                lineRangesOf(text, &hasTrailingWhitespace);
        if (!lines.isEmpty()) {
            PromptCheck check;
            check.ruleId = QStringLiteral("trailing-whitespace");
            check.severity = 0;
            check.start = lines.constFirst().first;
            check.length = lines.constFirst().second;
            check.message = QStringLiteral(
                "Trailing whitespace: clean it up to keep diffs tidy.");
            result.append(check);
        } else if (text.contains(QStringLiteral("\n\n\n\n"))) {
            // 3+ 空行 = 4 个连续换行。
            const int index = text.indexOf(QStringLiteral("\n\n\n\n"));
            PromptCheck check;
            check.ruleId = QStringLiteral("trailing-whitespace");
            check.severity = 0;
            check.start = index;
            check.length = 4;
            check.message = QStringLiteral(
                "Blank-run over three lines: collapse it to two.");
            result.append(check);
        }
    }

    // --- smart-quotes：智能引号 / 长破折号 ---
    if (!empty) {
        const QList<QPair<int, int>> lines =
                lineRangesOf(text, &hasSmartPunctuation);
        if (!lines.isEmpty()) {
            PromptCheck check;
            check.ruleId = QStringLiteral("smart-quotes");
            check.severity = 0;
            check.start = lines.constFirst().first;
            check.length = lines.constFirst().second;
            check.message = QStringLiteral(
                "Smart quotes or long dashes: agents' shells prefer the "
                "plain ASCII forms.");
            result.append(check);
        }
    }

    return result;
}

/**
 * @brief 估算 token 数（草案公式：ASCII/4 + 非ASCII×0.9）
 */
int ContextBudget::estimateTokens(const QString &text)
{
    int ascii = 0;
    int nonAscii = 0;
    for (const QChar &ch : text) {
        if (ch.row() == 0) {
            ++ascii;
        } else {
            ++nonAscii;
        }
    }
    return ascii / 4 + static_cast<int>(nonAscii * 0.9);
}

/**
 * @brief 分级（0 安全 / 1 提醒 / 2 超限）
 */
int ContextBudget::level(int tokens, int contextWindowTokens)
{
    const int window = qMax(1, contextWindowTokens);
    if (tokens * 100 >= window * 60) {
        return 2;
    }
    if (tokens * 100 >= window * 25) {
        return 1;
    }
    return 0;
}

} // namespace awb::tools
