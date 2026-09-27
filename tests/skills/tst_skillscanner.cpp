#include "awbtest.h"

#include <QtTest>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

#include "core/Settings.h"
#include "skills/SkillModel.h"
#include "skills/SkillScanner.h"

using awb::core::Settings;
using awb::skills::SkillDefinition;
using awb::skills::SkillModel;
using awb::skills::SkillRoot;
using awb::skills::SkillScanner;

// Covers multi-root scanning, plugin multi-version dedup, a
// missing root never fails the scan. Uses fixed text samples in temp dirs —
// never the machine's real skill directories.
class TestSkillScanner : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        // A previous run's testMaxDepth file would otherwise leak in and
        // cap the depth of every later test.
        QFile::remove(Settings::settingsFilePath());
    }

    void testScanFindsSkillDirectories()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        writeSkill(tmp.path() + QStringLiteral("/roots/a/skill-one"),
                   "---\nname: one\ndescription: First skill\n---\nbody");
        writeSkill(tmp.path() + QStringLiteral("/roots/a/nested/skill-two"),
                   "---\ndescription: Second skill\n---\nbody");
        // A directory without SKILL.md is not a skill; its child still is.
        writeSkill(tmp.path() + QStringLiteral("/roots/a/not-a-skill/deep/skill-three"),
                   "---\nname: three\n---\nbody");

        Settings settings;
        setRoots(&settings, { makeRoot("custom-a", tmp.path() + "/roots/a",
                                       "custom") });
        SkillScanner scanner(&settings);

        QSignalSpy finished(&scanner, &SkillScanner::scanFinished);
        scanner.refresh();
        QCOMPARE(finished.count(), 1);

        QStringList names;
        for (const SkillDefinition &skill : scanner.definitions())
            names.append(skill.name);
        QVERIFY(names.contains(QStringLiteral("one")));
        QVERIFY(names.contains(QStringLiteral("three")));

        // name falls back to the directory name.
        QVERIFY(names.contains(QStringLiteral("skill-two")));
        QCOMPARE(scanner.lastStats().rootsScanned, 1);
        QCOMPARE(scanner.lastStats().skillCount, 3);
    }

    // Multiple versions of the same plugin in the cache: only the highest
    // survives; unrelated plugins are untouched.
    void testPluginVersionDedup()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString cache = tmp.path() + QStringLiteral("/cache");
        writeSkill(cache + QStringLiteral("/browser-use/0.2.1/skills/use"),
                   "---\nname: use\n---\nx");
        writeSkill(cache + QStringLiteral("/browser-use/0.5.1/skills/use"),
                   "---\nname: use\n---\nx");
        writeSkill(cache + QStringLiteral("/browser-use/0.4.2/skills/use"),
                   "---\nname: use\n---\nx");
        writeSkill(cache + QStringLiteral("/docx/1.0.0/skills/docx"),
                   "---\nname: docx\n---\nx");

        Settings settings;
        SkillRoot root;
        root.id = QStringLiteral("plugins");
        root.label = QStringLiteral("Plugins");
        root.path = cache + QStringLiteral("/*/*/skills");
        root.kind = QStringLiteral("plugin");
        root.dedupeScope = QStringLiteral("marketplace-plugin");
        setRoots(&settings, { root });

        SkillScanner scanner(&settings);
        scanner.refresh();

        QCOMPARE(scanner.lastStats().duplicatesDropped, 2);
        QStringList versions;
        int browserUse = 0;
        for (const SkillDefinition &skill : scanner.definitions()) {
            if (skill.pluginId == QLatin1String("browser-use")) {
                ++browserUse;
                versions.append(skill.pluginVersion);
            }
        }
        QCOMPARE(browserUse, 1);
        QCOMPARE(versions, QStringList{QStringLiteral("0.5.1")});
        // The other plugin survives.
        bool docxFound = false;
        for (const SkillDefinition &skill : scanner.definitions()) {
            if (skill.pluginId == QLatin1String("docx"))
                docxFound = true;
        }
        QVERIFY(docxFound);
    }

    // One plugin ships SEVERAL skills: the dedup key includes the skill
    // name, so a second skill is not mistaken for a version-duplicate of
    // the first (review regression: only 1 of 2 skills survived).
    void testMultiSkillPluginKeepsAllSkills()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString cache = tmp.path() + QStringLiteral("/cache");
        writeSkill(cache + QStringLiteral("/bundle/0.5.1/skills/alpha"),
                   "---\nname: alpha\n---\nx");
        writeSkill(cache + QStringLiteral("/bundle/0.5.1/skills/beta"),
                   "---\nname: beta\n---\ny");
        // Older copy of the same bundle: its two skills must drop.
        writeSkill(cache + QStringLiteral("/bundle/0.4.0/skills/alpha"),
                   "---\nname: alpha\n---\nx");
        writeSkill(cache + QStringLiteral("/bundle/0.4.0/skills/beta"),
                   "---\nname: beta\n---\ny");

        Settings settings;
        SkillRoot root;
        root.id = QStringLiteral("plugins");
        root.label = QStringLiteral("Plugins");
        root.path = cache + QStringLiteral("/*/*/skills");
        root.kind = QStringLiteral("plugin");
        root.dedupeScope = QStringLiteral("marketplace-plugin");
        setRoots(&settings, { root });

        SkillScanner scanner(&settings);
        scanner.refresh();

        // Both 0.5.1 skills survive; the two 0.4.0 copies drop.
        QCOMPARE(scanner.lastStats().duplicatesDropped, 2);
        QStringList names;
        for (const SkillDefinition &skill : scanner.definitions())
            names.append(skill.name);
        std::sort(names.begin(), names.end());
        QCOMPARE(names, QStringList({QStringLiteral("alpha"),
                                     QStringLiteral("beta")}));
    }

    // A missing root is skipped with a warning — the scan still succeeds.
    void testMissingRootIsSkipped()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        writeSkill(tmp.path() + QStringLiteral("/ok/skill-x"),
                   "---\nname: x\n---\ny");

        Settings settings;
        setRoots(&settings, {
            makeRoot("missing", tmp.path() + "/does-not-exist", "custom"),
            makeRoot("ok", tmp.path() + "/ok", "custom"),
        });
        SkillScanner scanner(&settings);
        QSignalSpy finished(&scanner, &SkillScanner::scanFinished);
        scanner.refresh();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(scanner.definitions().size(), 1);
        QCOMPARE(scanner.lastStats().rootsSkipped, 1);
        QVERIFY(scanner.lastStats().skippedRoots.contains(QStringLiteral("missing")));
    }

    // maxDepth caps the walk (default 6; configured here to 1).
    void testMaxDepth()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        writeSkill(tmp.path() + QStringLiteral("/r/shallow"),
                   "---\nname: shallow\n---\nx");
        writeSkill(tmp.path() + QStringLiteral("/r/a/b/deep"),
                   "---\nname: deep\n---\nx");

        // The settings file must exist BEFORE the Settings instance reads
        // it — load() runs in the constructor.
        QJsonObject skills;
        skills[QStringLiteral("maxDepth")] = 1;
        QJsonObject json;
        json[QStringLiteral("skills")] = skills;
        writeSettings(json);

        Settings settings;
        QJsonArray roots;
        QJsonObject root;
        root[QStringLiteral("id")] = QStringLiteral("r");
        root[QStringLiteral("path")] = tmp.path() + "/r";
        root[QStringLiteral("kind")] = QStringLiteral("custom");
        roots.append(root);
        settings.setSkillRoots(roots);

        SkillScanner scanner(&settings);
        scanner.refresh();
        QStringList names;
        for (const SkillDefinition &skill : scanner.definitions())
            names.append(skill.name);
        QVERIFY(names.contains(QStringLiteral("shallow")));
        QVERIFY(!names.contains(QStringLiteral("deep")));
    }

    // Filtering: search + kind facets + sort on the model.
    void testModelFilterAndSort()
    {
        QList<SkillDefinition> skills;
        skills.append(makeSkill("beta tool", "custom", "zeta-dir"));
        skills.append(makeSkill("alpha tool", "agents", "alpha-dir"));
        skills.append(makeSkill("gamma helper", "claude", "gamma-dir"));

        SkillModel model;
        model.setSkills(skills);
        QCOMPARE(model.rowCount(), 3);

        model.setSearchText(QStringLiteral("tool"));
        QCOMPARE(model.rowCount(), 2);

        model.setSearchText(QString());
        model.setActiveKinds({ QStringLiteral("claude") });
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 0).data(SkillModel::KindRole).toString(),
                 QStringLiteral("claude"));

        model.setActiveKinds(QStringList());
        model.setSortMode(QStringLiteral("name"));
        QCOMPARE(model.index(0, 0).data(SkillModel::NameRole).toString(),
                 QStringLiteral("alpha tool"));
    }

private:
    static void writeSkill(const QString &dir, const QByteArray &front)
    {
        QDir().mkpath(dir);
        QFile f(dir + QStringLiteral("/SKILL.md"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(front);
        f.write("\n");
    }

    static SkillRoot makeRoot(const QString &id, const QString &path,
                              const QString &kind)
    {
        SkillRoot root;
        root.id = id;
        root.label = id;
        root.path = path;
        root.kind = kind;
        return root;
    }

    static void setRoots(Settings *settings, const QList<SkillRoot> &roots)
    {
        QJsonArray array;
        for (const SkillRoot &root : roots) {
            QJsonObject o;
            o[QStringLiteral("id")] = root.id;
            o[QStringLiteral("label")] = root.label;
            o[QStringLiteral("path")] = root.path;
            o[QStringLiteral("kind")] = root.kind;
            o[QStringLiteral("enabled")] = root.enabled;
            array.append(o);
        }
        settings->setSkillRoots(array);
    }

    static void writeSettings(const QJsonObject &json)
    {
        QDir().mkpath(QFileInfo(Settings::settingsFilePath()).absolutePath());
        QFile f(Settings::settingsFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(json).toJson());
    }

    static SkillDefinition makeSkill(const QString &name, const QString &kind,
                                     const QString &dirName)
    {
        SkillDefinition skill;
        skill.name = name;
        skill.kind = kind;
        skill.dirPath = QStringLiteral("/x/") + dirName;
        skill.skillFilePath = skill.dirPath + QStringLiteral("/SKILL.md");
        return skill;
    }
};

#include "tst_skillscanner.moc"
AWB_TEST(TestSkillScanner)
