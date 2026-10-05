#include "awbtest.h"

#include "tools/PromptChecks.h"

#include <QtTest>

namespace PC = awb::tools::PromptChecks;
namespace CB = awb::tools::ContextBudget;
using awb::tools::PromptCheck;

namespace {

/// 取某条规则的命中（fixture 文本常会顺带触发别的规则，断言只看目标）。
QList<PromptCheck> hitsOf(const QString &ruleId, const QString &text)
{
    QList<PromptCheck> result;
    for (const PromptCheck &check : PC::run(text)) {
        if (check.ruleId == ruleId) {
            result.append(check);
        }
    }
    return result;
}

} // namespace

/**
 * @brief 提示词体检与预算估算的纯函数测试：每条规则的正例/反例、
 *        区间定位、幂等性；token 公式的 ASCII/非 ASCII 分账与分级阈值。
 */
class TestPromptChecks : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init() { QStandardPaths::setTestModeEnabled(true); }

    /// 规则表：id 稳定有序、title 非空、未知 id 得空串。
    void testRuleTable()
    {
        const QStringList ids = PC::ruleIds();
        QCOMPARE(ids.size(), 10);
        QCOMPARE(ids.constFirst(), QStringLiteral("vague-wording"));
        QVERIFY(ids.contains(QStringLiteral("smart-quotes")));
        for (const QString &id : ids) {
            QVERIFY2(!PC::ruleTitle(id).isEmpty(),
                     qPrintable(QStringLiteral("no title for %1").arg(id)));
        }
        QVERIFY(PC::ruleTitle(QStringLiteral("no-such-rule")).isEmpty());
    }

    /// 空文本：零命中、零 token。
    void testEmptyTextIsClean()
    {
        QCOMPARE(PC::run(QString()).size(), 0);
        QCOMPARE(PC::run(QStringLiteral("   \n  \n")).size(), 0);
        QCOMPARE(CB::estimateTokens(QString()), 0);
    }

    /// 同输入同输出（纯函数契约）。
    void testIsDeterministic()
    {
        const QString text = QStringLiteral(
            "Fix the bug in ./src/a.cpp\nrun the tests, expect pass\n");
        QCOMPARE(PC::run(text), PC::run(text));
        QCOMPARE(CB::estimateTokens(text), CB::estimateTokens(text));
    }

    /// 模糊措辞：中英各一正例，区间定位到词。
    void testVagueWording()
    {
        // hitsOf 只取目标规则的命中（fixture 会顺带触发别的规则）。
        const QList<PromptCheck> hits = hitsOf(
            QStringLiteral("vague-wording"),
            QStringLiteral("Please 优化一下 this function."));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).start, 7);
        QCOMPARE(hits.at(0).length, 4);

        // 英文模糊词同样命中（大小写不敏感）。
        const QList<PromptCheck> en = hitsOf(
            QStringLiteral("vague-wording"),
            QStringLiteral("Do TRY YOUR BEST here"));
        QCOMPARE(en.size(), 1);
    }

    /// 缺验收：无验收词触发；有「测试/期望」即解除。
    void testNoAcceptance()
    {
        const QList<PromptCheck> hit = PC::run(
            QStringLiteral("Add a login form using Qt Widgets."));
        bool found = false;
        for (const PromptCheck &check : hit) {
            if (check.ruleId == QLatin1String("no-acceptance")) {
                found = true;
            }
        }
        QVERIFY(found);
        QCOMPARE(hit.at(0).severity, 1);

        const QList<PromptCheck> ok = PC::run(
            QStringLiteral("Add a login form using Qt Widgets. "
                           "Expect the tests to pass."));
        for (const PromptCheck &check : ok) {
            QVERIFY(check.ruleId != QLatin1String("no-acceptance"));
        }
    }

    /// 反斜杠路径：.\x 与 C:\x 都命中，区间精确。
    void testBackslashPath()
    {
        const QList<PromptCheck> hits = hitsOf(
            QStringLiteral("backslash-path"),
            QStringLiteral("Look at .\\src\\a.cpp and fix it."));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).start, 8);
        QCOMPARE(hits.at(0).length, 11);

        const QList<PromptCheck> drive = hitsOf(
            QStringLiteral("backslash-path"),
            QStringLiteral("Open C:\\work\\file.txt"));
        QCOMPARE(drive.size(), 1);

        // 正斜杠引用不触发。
        const QList<PromptCheck> forward = PC::run(
            QStringLiteral("Look at `./src/a.cpp`"));
        for (const PromptCheck &check : forward) {
            QVERIFY(check.ruleId != QLatin1String("backslash-path"));
        }
    }

    /// 无上下文：提及文件/语言/框架解除。
    void testNoContext()
    {
        const QList<PromptCheck> hit = PC::run(
            QStringLiteral("Make it faster. Verify the result is correct."));
        bool found = false;
        for (const PromptCheck &check : hit) {
            if (check.ruleId == QLatin1String("no-context")) {
                found = true;
            }
        }
        QVERIFY(found);
    }

    /// 预算量级：默认阈值（~19200 tokens）以上触发 token-risk。
    void testTokenRisk()
    {
        // 80000 个 ASCII 字符 ≈ 20000 tokens > 19200。
        QString bulk;
        bulk.reserve(80000);
        for (int i = 0; i < 80000; ++i) {
            bulk += QLatin1Char('a');
        }
        const QList<PromptCheck> hits = PC::run(bulk);
        bool found = false;
        for (const PromptCheck &check : hits) {
            if (check.ruleId == QLatin1String("token-risk")) {
                found = true;
            }
        }
        QVERIFY(found);
        // 短文本不触发。
        for (const PromptCheck &check
             : PC::run(QStringLiteral("short one"))) {
            QVERIFY(check.ruleId != QLatin1String("token-risk"));
        }
    }

    /// 结构扁平：>40 行无标题/列表触发；有一行列表即解除。
    void testFlatStructure()
    {
        QString flat;
        for (int i = 0; i < 45; ++i) {
            flat += QStringLiteral("line of plain prose %1\n").arg(i);
        }
        bool found = false;
        for (const PromptCheck &check : PC::run(flat)) {
            if (check.ruleId == QLatin1String("flat-structure")) {
                found = true;
            }
        }
        QVERIFY(found);

        flat.prepend(QStringLiteral("- checklist item\n"));
        for (const PromptCheck &check : PC::run(flat)) {
            QVERIFY(check.ruleId != QLatin1String("flat-structure"));
        }
    }

    /// 占位符残留：区间 = 首个 {{...}}。
    void testUnfilledPlaceholder()
    {
        // "Fix " 占 0–4，{{module}} 从 4 开始，10 个字符（2+6+2）。
        const QList<PromptCheck> hits = hitsOf(
            QStringLiteral("unfilled-placeholder"),
            QStringLiteral("Fix {{module}} today"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).start, 4);
        QCOMPARE(hits.at(0).length, 10);
    }

    /// 行尾空白与空行连跑；智能引号。
    void testWhitespaceAndQuotes()
    {
        // 整行含行尾空格共 25 个字符（4+1+4+1+8+1+5+1）。
        const QList<PromptCheck> trailing = hitsOf(
            QStringLiteral("trailing-whitespace"),
            QStringLiteral("text with trailing space \nnext"));
        QCOMPARE(trailing.size(), 1);
        QCOMPARE(trailing.at(0).start, 0);
        QCOMPARE(trailing.at(0).length, 25);

        // 3+ 空行（4 个连续换行）。
        const QList<PromptCheck> blanks = hitsOf(
            QStringLiteral("trailing-whitespace"),
            QStringLiteral("a\n\n\n\nb"));
        QCOMPARE(blanks.size(), 1);

        // 智能引号。
        const QList<PromptCheck> smart = hitsOf(
            QStringLiteral("smart-quotes"),
            QStringLiteral("Use \u201Cquoted\u201D text"));
        QCOMPARE(smart.size(), 1);
    }

    /// 问句为主且无祈使 → question-only；带祈使动词解除。
    void testQuestionOnly()
    {
        const QList<PromptCheck> hit = PC::run(
            QStringLiteral("What is this?\nHow does it work?\n"
                           "Which one is better?"));
        bool found = false;
        for (const PromptCheck &check : hit) {
            if (check.ruleId == QLatin1String("question-only")) {
                found = true;
            }
        }
        QVERIFY(found);

        const QList<PromptCheck> imperative = PC::run(
            QStringLiteral("What is this?\nHow does it work?\n"
                           "Write the answer."));
        for (const PromptCheck &check : imperative) {
            QVERIFY(check.ruleId != QLatin1String("question-only"));
        }
    }

    /// token 公式：ASCII/4、非 ASCII ×0.9、混合分账。
    void testTokenEstimate()
    {
        QCOMPARE(CB::estimateTokens(QStringLiteral("12345678")), 2);
        QCOMPARE(CB::estimateTokens(QString(10, QChar(0x4E2D))), 9);
        // 混合：8 个 ASCII + 10 个中文 = 2 + 9 = 11。
        QCOMPARE(CB::estimateTokens(
                     QStringLiteral("12345678")
                         + QString(10, QChar(0x4E2D))),
                 11);
    }

    /// 分级阈值：25% 与 60% 两条边界。
    void testBudgetLevels()
    {
        const int window = 32000;
        QCOMPARE(CB::level(100, window), 0);     // 0.3%
        QCOMPARE(CB::level(7999, window), 0);    // 24.99%
        QCOMPARE(CB::level(8000, window), 1);    // 25%
        QCOMPARE(CB::level(19199, window), 1);   // 59.99%
        QCOMPARE(CB::level(19200, window), 2);   // 60%
        // 窗口为 0/负数按 1 兜底（不除零）。
        QCOMPARE(CB::level(1, 0), 2);
    }
};

AWB_TEST(TestPromptChecks)

#include "tst_promptchecks.moc"
