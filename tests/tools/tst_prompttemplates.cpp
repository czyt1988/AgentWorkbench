#include "awbtest.h"

#include "core/JsonStore.h"
#include "tools/PromptTemplate.h"
#include "tools/PromptTemplateLibrary.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace T = awb::tools;

namespace {

/// 写一个用户模板文件（内容原样）。
bool writeTemplateFile(const QString &dir, const QString &name,
                       const QString &content)
{
    QFile file(QDir(dir).filePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(content.toUtf8());
    file.close();
    return true;
}

} // namespace

/**
 * @brief 模板与变量的纯层测试：头部解析/序列化、变量提取/求值、
 *        三层合并、检索与存删
 */
class TestPromptTemplates : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init() { QStandardPaths::setTestModeEnabled(true); }

    /// 资源在位（排障用，顺手留住：随包 JSON 忘登记资源清单在这里红，
    /// 比在界面「模板列表是空的」好查得多）。
    void testBuiltinResourcePresent()
    {
        QVERIFY(QFile::exists(
            QStringLiteral(":/config/default_prompt_templates.json")));
        const QJsonObject root = awb::core::JsonStore::readFile(
            QStringLiteral(":/config/default_prompt_templates.json"));
        QCOMPARE(root.value(QStringLiteral("templates")).toArray().size(),
                 10);
    }

    // --- 头部解析 -------------------------------------------------------

    /// 完整头部 → 全字段；序列化往返保真。
    void testHeaderRoundTrip()
    {
        const QString raw = QStringLiteral(
            "---\n"
            "id: my-template\n"
            "title: My Template\n"
            "category: Mine\n"
            "tags: rust, async\n"
            "description: Does the thing.\n"
            "---\n"
            "\n"
            "Hello {{name}}.\n");
        const T::PromptTemplate parsed = T::PromptTemplateIo::parse(
            raw, QStringLiteral("fallback"),
            T::PromptTemplate::Scope::User);
        QCOMPARE(parsed.id, QStringLiteral("my-template"));
        QCOMPARE(parsed.title, QStringLiteral("My Template"));
        QCOMPARE(parsed.category, QStringLiteral("Mine"));
        QCOMPARE(parsed.tags,
                 QStringList({ QStringLiteral("rust"),
                               QStringLiteral("async") }));
        QCOMPARE(parsed.description, QStringLiteral("Does the thing."));
        QCOMPARE(parsed.body, QStringLiteral("Hello {{name}}.\n"));
        QCOMPARE(parsed.scope, T::PromptTemplate::Scope::User);

        // 序列化回来再解析：字段全部保真（正文含结尾换行）。
        const T::PromptTemplate reparsed = T::PromptTemplateIo::parse(
            T::PromptTemplateIo::serialize(parsed), parsed.id,
            T::PromptTemplate::Scope::User);
        QCOMPARE(reparsed.id, parsed.id);
        QCOMPARE(reparsed.title, parsed.title);
        QCOMPARE(reparsed.category, parsed.category);
        QCOMPARE(reparsed.tags, parsed.tags);
        QCOMPARE(reparsed.description, parsed.description);
        QCOMPARE(reparsed.body, parsed.body);
    }

    /// 无头部 / 半截头部 / 坏行：能救的救，救不了的整篇当正文。
    void testHeaderTolerance()
    {
        // 无头部：全正文，标题回落 fallback。
        const T::PromptTemplate noHeader = T::PromptTemplateIo::parse(
            QStringLiteral("Just text.\n"),
            QStringLiteral("fallback"), T::PromptTemplate::Scope::User);
        QCOMPARE(noHeader.body, QStringLiteral("Just text.\n"));
        QCOMPARE(noHeader.title, QStringLiteral("fallback"));
        QCOMPARE(noHeader.category, QStringLiteral("My templates"));

        // 头部开了没关：整篇退回正文，头部字段作废。
        const T::PromptTemplate unclosed = T::PromptTemplateIo::parse(
            QStringLiteral("---\ntitle: Lost\nBut no close.\n"),
            QStringLiteral("fallback"), T::PromptTemplate::Scope::User);
        QCOMPARE(unclosed.body,
                 QStringLiteral("---\ntitle: Lost\nBut no close.\n"));

        // 坏行（无冒号、不认识的键）跳过，好行照收。
        const T::PromptTemplate dirty = T::PromptTemplateIo::parse(
            QStringLiteral(
                "---\n"
                "title: Good\n"
                "this line has no colon\n"
                "unknown-key: dropped\n"
                "---\n"
                "Body.\n"),
            QStringLiteral("fallback"), T::PromptTemplate::Scope::User);
        QCOMPARE(dirty.title, QStringLiteral("Good"));
        QCOMPARE(dirty.body, QStringLiteral("Body.\n"));
    }

    // --- 变量 ------------------------------------------------------------

    /// 提取：按出现顺序、同名去重、默认值取第一次的。
    void testVariableExtract()
    {
        const QList<T::PromptVariables::Variable> variables =
            T::PromptVariables::extract(
                QStringLiteral("{{task}} and {{task:second}} "
                               "and {{ other : with spaces }}"));
        QCOMPARE(variables.size(), 2);
        QCOMPARE(variables.at(0).name, QStringLiteral("task"));
        QVERIFY(variables.at(0).defaultValue.isEmpty());
        QCOMPARE(variables.at(1).name, QStringLiteral("other"));
        QCOMPARE(variables.at(1).defaultValue,
                 QStringLiteral("with spaces"));

        // 内置变量的判定（调用方据此把它们从表单里排除）。
        QVERIFY(T::PromptVariables::isBuiltIn(QStringLiteral("workspace")));
        QVERIFY(T::PromptVariables::isBuiltIn(QStringLiteral("date")));
        QVERIFY(!T::PromptVariables::isBuiltIn(QStringLiteral("task")));

        // 非变量名的 {{...}}（包含语法、以数字开头、中文、空名）不匹配。
        const QList<T::PromptVariables::Variable> none =
            T::PromptVariables::extract(
                QStringLiteral("{{> include}} {{1x}} {{中文}} {{}}"));
        QCOMPARE(none.size(), 0);
    }

    /// 求值：同名全替换、默认值兜底、未填原样保留、内置变量。
    void testVariableApply()
    {
        const QString body = QStringLiteral(
            "Task: {{task}}\nAgain: {{task}}\n"
            "With default: {{lang:rust}}\n"
            "Skipped: {{left}}\n");

        QVariantMap values;
        values.insert(QStringLiteral("task"),
                      QStringLiteral("Fix the tree"));
        QCOMPARE(T::PromptVariables::apply(body, values),
                 QStringLiteral("Task: Fix the tree\n"
                                "Again: Fix the tree\n"
                                "With default: rust\n"
                                "Skipped: {{left}}\n"));

        // 给了值优先于默认值。
        values.insert(QStringLiteral("lang"), QStringLiteral("cpp"));
        QVERIFY(T::PromptVariables::apply(body, values)
                    .contains(QStringLiteral("With default: cpp")));

        // 内置变量的求值：date 是今天（ISO），workspace 原样带入。
        QVERIFY(!T::PromptVariables::builtInValue(
            QStringLiteral("date"), QString()).isEmpty());
        QCOMPARE(T::PromptVariables::builtInValue(
                     QStringLiteral("workspace"),
                     QStringLiteral("C:/src/project")),
                 QStringLiteral("C:/src/project"));
        QVERIFY(T::PromptVariables::builtInValue(
            QStringLiteral("task"), QString()).isEmpty());
    }

    // --- 模板库 ------------------------------------------------------------

    /// 内置装载 + 用户覆盖 + 用户追加 + 检索。
    void testLibraryMergeAndFilter()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // 用户模板住在 <dataRoot>/prompt_templates/，不是数据根本身。
        const QString templatesDir =
            QDir(dir.path()).filePath(QStringLiteral("prompt_templates"));
        QVERIFY(QDir().mkpath(templatesDir));
        QVERIFY(writeTemplateFile(
            templatesDir, QStringLiteral("fix-a-bug.md"),
            QStringLiteral("---\ntitle: My Fix\n---\nMine {{x}}.\n")));
        QVERIFY(writeTemplateFile(
            templatesDir, QStringLiteral("extra.md"),
            QStringLiteral("---\ntitle: Extra\ncategory: Mine\n"
                           "tags: extra\n---\nExtra body.\n")));
        QVERIFY(writeTemplateFile(
            templatesDir, QStringLiteral("broken.md"),
            QStringLiteral("Body only, header-less but valid.")));

        T::PromptTemplateLibrary library(dir.path());
        const QList<T::PromptTemplate> all = library.templates();
        // 内置 10 条 + 用户追加 2 条（extra、broken）；fix-a-bug 被覆盖。
        QCOMPARE(all.size(), 12);
        const T::PromptTemplate overridden =
            library.byId(QStringLiteral("fix-a-bug"));
        QCOMPARE(overridden.title, QStringLiteral("My Fix"));
        QCOMPARE(overridden.scope, T::PromptTemplate::Scope::User);
        QCOMPARE(overridden.body, QStringLiteral("Mine {{x}}.\n"));
        QCOMPARE(library.byId(QStringLiteral("extra")).title,
                 QStringLiteral("Extra"));
        QCOMPARE(library.byId(QStringLiteral("broken")).title,
                 QStringLiteral("broken"));
        // 内置的其它条目还在（覆盖只顶那一个）。
        QCOMPARE(library.byId(QStringLiteral("add-a-feature")).scope,
                 T::PromptTemplate::Scope::Builtin);

        // 检索：标题命中、正文命中、标签过滤的与关系。
        QCOMPARE(library.filtered(QStringLiteral("extra"), QString()).size(),
                 1);
        QVERIFY(library.filtered(QStringLiteral("Acceptance"), QString())
                    .size() >= 1);
        QCOMPARE(library.filtered(QString(),
                                  QStringLiteral("extra")).size(), 1);
        QCOMPARE(library.filtered(QStringLiteral("extra"),
                                  QStringLiteral("bug")).size(), 0);
        // 空 + 空 = 全部。
        QCOMPARE(library.filtered(QString(), QString()).size(), 12);
    }

    /// 存 / 删用户模板（存完立即可见；删除后内置条目回归随包形态）。
    void testLibrarySaveRemove()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        T::PromptTemplateLibrary library(dir.path());
        QCOMPARE(library.templates().size(), 10);

        T::PromptTemplate mine;
        mine.id = QStringLiteral("mine");
        mine.title = QStringLiteral("Mine");
        mine.body = QStringLiteral("Body {{v}}.\n");
        mine.category = QStringLiteral("My templates");
        QVERIFY(library.save(mine).ok);
        QCOMPARE(library.templates().size(), 11);
        QCOMPARE(library.byId(QStringLiteral("mine")).body,
                 QStringLiteral("Body {{v}}.\n"));

        // 覆盖内置：同 id 存一份用户文件，内置条目被顶掉。
        T::PromptTemplate replacement;
        replacement.id = QStringLiteral("review-code");
        replacement.title = QStringLiteral("My Review");
        replacement.body = QStringLiteral("Changed.\n");
        QVERIFY(library.save(replacement).ok);
        QCOMPARE(library.byId(QStringLiteral("review-code")).title,
                 QStringLiteral("My Review"));
        // 删掉用户文件：内置形态回来。
        QVERIFY(library.remove(QStringLiteral("review-code")).ok);
        QCOMPARE(library.byId(QStringLiteral("review-code")).scope,
                 T::PromptTemplate::Scope::Builtin);
        // 删除纯用户条目：从列表消失。
        QVERIFY(library.remove(QStringLiteral("mine")).ok);
        QCOMPARE(library.templates().size(), 10);
        // 空标题/正文拒绝。
        T::PromptTemplate empty;
        empty.id = QStringLiteral("empty");
        QVERIFY(!library.save(empty).ok);
    }
};

AWB_TEST(TestPromptTemplates)

#include "tst_prompttemplates.moc"
