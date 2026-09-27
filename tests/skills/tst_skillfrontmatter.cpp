#include "awbtest.h"

#include <QtTest>

#include "skills/SkillFrontmatter.h"

using awb::skills::SkillFrontmatter;
using awb::skills::SkillFrontmatterParser;

// Covers quotes, folded/literal scalars, multi-line indentation,
// BOM, CRLF, missing frontmatter, colons inside values.
class TestSkillFrontmatter : public QObject
{
    Q_OBJECT

private slots:
    void testMissingFrontmatter()
    {
        const SkillFrontmatter result =
            SkillFrontmatterParser::parse("# Just a heading\nSome text.\n");
        QVERIFY(!result.valid);
        QVERIFY(result.name.isEmpty());
    }

    void testUnterminatedBlock()
    {
        const SkillFrontmatter result =
            SkillFrontmatterParser::parse("---\nname: broken\nno close");
        QVERIFY(!result.valid);
    }

    void testBasicScalars()
    {
        const SkillFrontmatter result = SkillFrontmatterParser::parse(
            "---\n"
            "name: tdd\n"
            "description: Test-driven development workflow\n"
            "version: 1.2.3\n"
            "---\n"
            "# Body\n");
        QVERIFY(result.valid);
        QCOMPARE(result.name, QStringLiteral("tdd"));
        QCOMPARE(result.description,
                 QStringLiteral("Test-driven development workflow"));
        QCOMPARE(result.extras.value(QStringLiteral("version")),
                 QStringLiteral("1.2.3"));
    }

    // A colon inside the description must not split the key.
    void testColonInValue()
    {
        const SkillFrontmatter result = SkillFrontmatterParser::parse(
            "---\n"
            "description: Use this when: debugging hard bugs\n"
            "---\n");
        QVERIFY(result.valid);
        QCOMPARE(result.description,
                 QStringLiteral("Use this when: debugging hard bugs"));
    }

    void testQuotedValues()
    {
        const SkillFrontmatter result = SkillFrontmatterParser::parse(
            "---\n"
            "name: \"quoted name\"\n"
            "description: 'single ''quoted'' text'\n"
            "author: \"a \\\"quoted\\\" author\"\n"
            "---\n");
        QVERIFY(result.valid);
        QCOMPARE(result.name, QStringLiteral("quoted name"));
        QCOMPARE(result.description,
                 QStringLiteral("single 'quoted' text"));
        QCOMPARE(result.extras.value(QStringLiteral("author")),
                 QStringLiteral("a \"quoted\" author"));
    }

    void testLiteralBlockScalar()
    {
        const SkillFrontmatter result = SkillFrontmatterParser::parse(
            "---\n"
            "description: |-\n"
            "  Line one.\n"
            "  Line two with a colon: still here.\n"
            "allowed-tools: |\n"
            "  Bash(git log:*)\n"
            "  Read\n"
            "---\n");
        QVERIFY(result.valid);
        QCOMPARE(result.description,
                 QStringLiteral("Line one.\nLine two with a colon: still here."));
        QVERIFY(result.extras.value(QStringLiteral("allowed-tools"))
                    .contains(QStringLiteral("Bash(git log:*)")));
    }

    void testFoldedBlockScalar()
    {
        const SkillFrontmatter result = SkillFrontmatterParser::parse(
            "---\n"
            "description: >\n"
            "  folds these lines\n"
            "  into one paragraph\n"
            "---\n");
        QVERIFY(result.valid);
        QCOMPARE(result.description,
                 QStringLiteral("folds these lines into one paragraph"));
    }

    void testNestedKeysFlatten()
    {
        const SkillFrontmatter result = SkillFrontmatterParser::parse(
            "---\n"
            "name: flat\n"
            "metadata:\n"
            "  author: someone\n"
            "  version: 0.1.0\n"
            "---\n");
        QVERIFY(result.valid);
        QCOMPARE(result.extras.value(QStringLiteral("metadata.author")),
                 QStringLiteral("someone"));
        QCOMPARE(result.extras.value(QStringLiteral("metadata.version")),
                 QStringLiteral("0.1.0"));
    }

    void testCrlfAndBom()
    {
        QByteArray content = "---\r\nname: windows-file\r\ndescription: from CRLF\r\n---\r\n";
        content.prepend("\xEF\xBB\xBF");
        const SkillFrontmatter result = SkillFrontmatterParser::parse(content);
        QVERIFY(result.valid);
        QCOMPARE(result.name, QStringLiteral("windows-file"));
        QCOMPARE(result.description, QStringLiteral("from CRLF"));
    }

    void testListLinesJoinValue()
    {
        const SkillFrontmatter result = SkillFrontmatterParser::parse(
            "---\n"
            "name: listed\n"
            "allowed-tools:\n"
            "  - Read\n"
            "  - Edit\n"
            "---\n");
        QVERIFY(result.valid);
        const QString tools = result.extras.value(QStringLiteral("allowed-tools"));
        QVERIFY(tools.contains(QStringLiteral("Read")));
        QVERIFY(tools.contains(QStringLiteral("Edit")));
    }
};

#include "tst_skillfrontmatter.moc"
AWB_TEST(TestSkillFrontmatter)
