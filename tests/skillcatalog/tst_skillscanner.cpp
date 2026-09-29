#include "awbtest.h"

#include <QtTest>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

#include "core/Paths.h"
#include "core/Settings.h"
#include "skillcatalog/SkillCache.h"
#include "skillcatalog/SkillModel.h"
#include "skillcatalog/SkillScanTask.h"
#include "skillcatalog/SkillScanner.h"

using awb::core::Paths;
using awb::core::Settings;
using awb::skillcatalog::SkillCache;
using awb::skillcatalog::SkillDefinition;
using awb::skillcatalog::SkillModel;
using awb::skillcatalog::SkillRoot;
using awb::skillcatalog::SkillScanTask;
using awb::skillcatalog::SkillScanner;

// Covers multi-root scanning, plugin multi-version dedup, a
// missing root never failing the scan, and the JSON cache round trip.
// Uses fixed text samples in temp dirs — never the machine's real skill
// directories.
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
        QFile::remove(SkillCache::filePath());
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

        QVERIFY(scanAndWait(&scanner));
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
        QVERIFY(scanAndWait(&scanner));

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
        QVERIFY(scanAndWait(&scanner));

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
        QVERIFY(scanAndWait(&scanner));
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
        QVERIFY(scanAndWait(&scanner));
        QStringList names;
        for (const SkillDefinition &skill : scanner.definitions())
            names.append(skill.name);
        QVERIFY(names.contains(QStringLiteral("shallow")));
        QVERIFY(!names.contains(QStringLiteral("deep")));
    }

    // A refresh() while a scan is in flight is ignored (debounce): exactly
    // one scanFinished arrives for two immediate refresh() calls.
    void testRefreshWhileScanningIsIgnored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        writeSkill(tmp.path() + QStringLiteral("/r/skill-a"),
                   "---\nname: a\n---\nx");

        Settings settings;
        setRoots(&settings,
                 { makeRoot("r", tmp.path() + "/r", "custom") });
        SkillScanner scanner(&settings);

        QSignalSpy finished(&scanner, &SkillScanner::scanFinished);
        scanner.refresh();
        QVERIFY(scanner.scanning());
        scanner.refresh(); // in flight — must be a no-op
        QVERIFY(waitForScan(&scanner));
        QCOMPARE(finished.count(), 1);
        QVERIFY(!scanner.scanning());
    }

    // The JSON cache round trip: save() then load() returns the same
    // definitions (every serialized field), and a broken file degrades to
    // an invalid snapshot instead of failing.
    void testCacheRoundTrip()
    {
        QList<SkillDefinition> saved;
        SkillDefinition one;
        one.name = QStringLiteral("one");
        one.description = QStringLiteral("First skill");
        one.skillFilePath = QStringLiteral("/x/skill-one/SKILL.md");
        one.dirPath = QStringLiteral("/x/skill-one");
        one.rootId = QStringLiteral("custom-a");
        one.rootLabel = QStringLiteral("Custom A");
        one.kind = QStringLiteral("custom");
        one.lastModified = QDateTime(QDate(2026, 9, 29), QTime(12, 30, 45),
                                     Qt::UTC);
        one.sizeBytes = 1234;
        one.extras.insert(QStringLiteral("allowed-tools"), QStringLiteral(
            "Read, Write"));
        saved.append(one);
        SkillDefinition two;
        two.name = QStringLiteral("two");
        two.kind = QStringLiteral("plugin");
        two.pluginId = QStringLiteral("marketplace/plugin");
        two.pluginVersion = QStringLiteral("0.5.1");
        two.skillFilePath = QStringLiteral("/c/p/0.5.1/skills/two/SKILL.md");
        two.dirPath = QStringLiteral("/c/p/0.5.1/skills/two");
        saved.append(two);

        SkillScanTask::Stats stats;
        stats.skillCount = 2;
        stats.rootsScanned = 1;
        stats.elapsedMs = 42;
        stats.skippedRoots.append(QStringLiteral("gone"));

        QVERIFY(SkillCache::save(saved, stats).ok);
        const SkillCache::Snapshot snapshot = SkillCache::load();
        QVERIFY(snapshot.isValid());
        QCOMPARE(snapshot.definitions.size(), 2);

        const SkillDefinition &loadedOne = snapshot.definitions.at(0);
        QCOMPARE(loadedOne.name, one.name);
        QCOMPARE(loadedOne.description, one.description);
        QCOMPARE(loadedOne.skillFilePath, one.skillFilePath);
        QCOMPARE(loadedOne.dirPath, one.dirPath);
        QCOMPARE(loadedOne.rootId, one.rootId);
        QCOMPARE(loadedOne.rootLabel, one.rootLabel);
        QCOMPARE(loadedOne.kind, one.kind);
        QCOMPARE(loadedOne.lastModified, one.lastModified);
        QCOMPARE(loadedOne.sizeBytes, one.sizeBytes);
        QCOMPARE(loadedOne.extras.value(QStringLiteral("allowed-tools")),
                 QStringLiteral("Read, Write"));

        const SkillDefinition &loadedTwo = snapshot.definitions.at(1);
        QCOMPARE(loadedTwo.pluginId, two.pluginId);
        QCOMPARE(loadedTwo.pluginVersion, two.pluginVersion);

        QCOMPARE(snapshot.stats.skillCount, 2);
        QCOMPARE(snapshot.stats.elapsedMs, qint64(42));
        QCOMPARE(snapshot.stats.skippedRoots,
                 QStringList{QStringLiteral("gone")});

        // A corrupted cache file degrades to an invalid snapshot (the
        // startup scan rebuilds it) — never an error path.
        QFile f(SkillCache::filePath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("not json {");
        f.close();
        QVERIFY(!SkillCache::load().isValid());

        // A missing file is the first-start path, also invalid.
        QVERIFY(QFile::remove(SkillCache::filePath()));
        QVERIFY(!SkillCache::load().isValid());
    }

    // adoptResults (startup cache restore) lands through the same
    // scanFinished path but must NOT look like a scan: scanning stays false.
    void testAdoptResultsDoesNotFlipScanning()
    {
        Settings settings;
        SkillScanner scanner(&settings);

        QSignalSpy finished(&scanner, &SkillScanner::scanFinished);
        QSignalSpy started(&scanner, &SkillScanner::scanStarted);

        QList<SkillDefinition> defs;
        SkillDefinition def;
        def.name = QStringLiteral("cached");
        def.skillFilePath = QStringLiteral("/c/cached/SKILL.md");
        defs.append(def);
        SkillScanTask::Stats stats;
        stats.skillCount = 1;
        scanner.adoptResults(defs, stats);

        QCOMPARE(finished.count(), 1);
        QCOMPARE(started.count(), 0);
        QVERIFY(!scanner.scanning());
        QCOMPARE(scanner.definitions().size(), 1);
        QCOMPARE(scanner.lastStats().skillCount, 1);
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
    // 触发一次扫描并等它在 worker 线程上完成（泵事件循环；超时 5 s，
    // 测试不挂死）。
    static bool scanAndWait(SkillScanner *scanner, int timeoutMs = 5000)
    {
        scanner->refresh();
        return waitForScan(scanner, timeoutMs);
    }

    // 等一个进行中的扫描完成（不触发；防抖用例在两次 refresh() 之间
    // 依赖它只等不发）。
    static bool waitForScan(SkillScanner *scanner, int timeoutMs = 5000)
    {
        if (!scanner->scanning())
            return true;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(scanner, &SkillScanner::scanFinished, &loop,
                         &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop,
                         &QEventLoop::quit);
        timer.start(timeoutMs);
        loop.exec();
        return !scanner->scanning();
    }

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
