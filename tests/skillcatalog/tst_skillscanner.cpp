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

/// 测 skillcatalog 的扫描链路：多根扫描、插件多版本去重、缺失根不拖挂扫描、
/// JSON 缓存往返，以及 SkillModel 的过滤与排序。固定文本样本放在临时目录里
/// ——绝不碰本机真实的 skill 目录。
class TestSkillScanner : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        // 上一次运行留下的 testMaxDepth 文件否则会漏进来，
        // 把之后每个用例的深度都封顶。
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
        // 没有 SKILL.md 的目录不是 skill；它的子目录仍然是。
        writeSkill(tmp.path() + QStringLiteral("/roots/a/not-a-skill/deep/skill-three"),
                   "---\nname: three\n---\nbody");

        Settings settings;
        setRoots(&settings, { makeRoot("custom-a", tmp.path() + "/roots/a",
                                       "custom") });
        SkillScanner scanner(&settings);

        QVERIFY(scanAndWait(&scanner));
        QStringList names;
        for (const SkillDefinition &skill : scanner.definitions()) {
            names.append(skill.name);
        }
        QVERIFY(names.contains(QStringLiteral("one")));
        QVERIFY(names.contains(QStringLiteral("three")));

        // name 回退为目录名。
        QVERIFY(names.contains(QStringLiteral("skill-two")));
        QCOMPARE(scanner.lastStats().rootsScanned, 1);
        QCOMPARE(scanner.lastStats().skillCount, 3);
    }

    // 缓存里同一插件的多个版本：只有最高版活下来；无关插件不受影响。
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
            if (skill.pluginId == QStringLiteral("browser-use")) {
                ++browserUse;
                versions.append(skill.pluginVersion);
            }
        }
        QCOMPARE(browserUse, 1);
        QCOMPARE(versions, QStringList{QStringLiteral("0.5.1")});
        // 另一个插件安然无恙。
        bool docxFound = false;
        for (const SkillDefinition &skill : scanner.definitions()) {
            if (skill.pluginId == QStringLiteral("docx")) {
                docxFound = true;
            }
        }
        QVERIFY(docxFound);
    }

    // 一个插件带好几个 skill：去重键包含 skill 名，第二个 skill 不会被误判
    // 为第一个的版本重复（评审回归：2 个 skill 只活下来 1 个）。
    void testMultiSkillPluginKeepsAllSkills()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString cache = tmp.path() + QStringLiteral("/cache");
        writeSkill(cache + QStringLiteral("/bundle/0.5.1/skills/alpha"),
                   "---\nname: alpha\n---\nx");
        writeSkill(cache + QStringLiteral("/bundle/0.5.1/skills/beta"),
                   "---\nname: beta\n---\ny");
        // 同一 bundle 的旧拷贝：它的两个 skill 必须被丢弃。
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

        // 0.5.1 的两个 skill 都活下来；0.4.0 的两份拷贝被丢弃。
        QCOMPARE(scanner.lastStats().duplicatesDropped, 2);
        QStringList names;
        for (const SkillDefinition &skill : scanner.definitions()) {
            names.append(skill.name);
        }
        std::sort(names.begin(), names.end());
        QCOMPARE(names, QStringList({QStringLiteral("alpha"),
                                     QStringLiteral("beta")}));
    }

    // 缺失的根被跳过并告警——扫描仍然成功。
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

    // maxDepth 封顶遍历深度（默认 6；这里配成 1）。
    void testMaxDepth()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        writeSkill(tmp.path() + QStringLiteral("/r/shallow"),
                   "---\nname: shallow\n---\nx");
        writeSkill(tmp.path() + QStringLiteral("/r/a/b/deep"),
                   "---\nname: deep\n---\nx");

        // settings 文件必须在 Settings 实例读它之前就位——load() 在构造
        // 函数里就跑。
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
        for (const SkillDefinition &skill : scanner.definitions()) {
            names.append(skill.name);
        }
        QVERIFY(names.contains(QStringLiteral("shallow")));
        QVERIFY(!names.contains(QStringLiteral("deep")));
    }

    // 扫描在途中时再来一次 refresh() 被忽略（防抖）：两次紧挨着的 refresh()
    // 只等来恰好一次 scanFinished。
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
        scanner.refresh(); // 在途中——必须是 no-op
        QVERIFY(waitForScan(&scanner));
        QCOMPARE(finished.count(), 1);
        QVERIFY(!scanner.scanning());
    }

    // JSON 缓存往返：save() 之后 load() 拿回相同的定义（每个序列化字段），
    // 坏文件降级为无效快照而不是报错。
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

        // 损坏的缓存文件降级为无效快照（启动扫描会重建它）——绝不走报错路径。
        QFile f(SkillCache::filePath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("not json {");
        f.close();
        QVERIFY(!SkillCache::load().isValid());

        // 文件缺失是首次启动的路径，同样返回无效。
        QVERIFY(QFile::remove(SkillCache::filePath()));
        QVERIFY(!SkillCache::load().isValid());
    }

    // adoptResults（启动时的缓存恢复）走同一条 scanFinished 路径，但绝不
    // 能看起来像一次扫描：scanning 保持 false。
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

    // 过滤：搜索 + kind 分面 + 模型上的排序。
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
    /**
     * @brief 触发一次扫描并等它在 worker 线程上完成
     *
     * 泵事件循环；超时 5 s，测试不挂死。
     *
     * @param scanner 待驱动的扫描器
     * @param timeoutMs 最长等待毫秒数
     * @return 超时时间内扫描是否结束
     */
    static bool scanAndWait(SkillScanner *scanner, int timeoutMs = 5000)
    {
        scanner->refresh();
        return waitForScan(scanner, timeoutMs);
    }

    /**
     * @brief 等一个进行中的扫描完成（不触发）
     *
     * 防抖用例在两次 refresh() 之间依赖它只等不发。
     *
     * @param scanner 正在扫描的扫描器
     * @param timeoutMs 最长等待毫秒数
     * @return 超时时间内扫描是否结束；本来就没在扫时恒为 true
     */
    static bool waitForScan(SkillScanner *scanner, int timeoutMs = 5000)
    {
        if (!scanner->scanning()) {
            return true;
        }
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

    /**
     * @brief 在指定目录下造一个带 frontmatter 的 SKILL.md
     *
     * @param dir skill 目录（不存在会先创建）
     * @param front SKILL.md 的完整内容（末尾自动补一个换行）
     */
    static void writeSkill(const QString &dir, const QByteArray &front)
    {
        QDir().mkpath(dir);
        QFile f(dir + QStringLiteral("/SKILL.md"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(front);
        f.write("\n");
    }

    /**
     * @brief 构造一个最小可用的扫描根
     *
     * @param id 根 id（同时充当 label）
     * @param path 根路径（原样写入，不展开）
     * @param kind 根类型（custom 等）
     * @return 填好基本字段的 SkillRoot
     */
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

    /**
     * @brief 把扫描根列表写进 Settings
     *
     * @param settings 目标 Settings 实例
     * @param roots 要生效的根列表
     */
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

    /**
     * @brief 用给定根对象覆盖当前的 settings.json
     *
     * 用于「Settings 构造时就得读到文件内容」的用例（如 maxDepth）。
     *
     * @param json 完整的 settings.json 根对象
     */
    static void writeSettings(const QJsonObject &json)
    {
        QDir().mkpath(QFileInfo(Settings::settingsFilePath()).absolutePath());
        QFile f(Settings::settingsFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(json).toJson());
    }

    /**
     * @brief 构造一个仅含名字、类型与路径的 skill 定义
     *
     * @param name skill 名
     * @param kind 类型（custom 等）
     * @param dirName 目录名（拼进 /x/<dirName> 的路径）
     * @return 可塞进 SkillModel 的最小 SkillDefinition
     */
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
