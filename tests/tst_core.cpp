#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "AgentConfig.h"
#include "AgentLauncher.h"
#include "AgentModel.h"

// Unit tests for the config/model/launcher core. All config reads and
// writes are isolated from the user's real config via
// QStandardPaths test mode.
class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(AgentConfig::configFilePath());
    }

    void testSlugFromName()
    {
        QCOMPARE(AgentConfig::slugFromName(QStringLiteral("Kimi Code")),
                 QStringLiteral("kimi-code"));
        QCOMPARE(AgentConfig::slugFromName(QStringLiteral("My_Agent! 2")),
                 QStringLiteral("my-agent-2"));
        QCOMPARE(AgentConfig::slugFromName(QStringLiteral("  --Trim--  ")),
                 QStringLiteral("trim"));
        QCOMPARE(AgentConfig::slugFromName(QStringLiteral("???")),
                 QStringLiteral("agent"));
    }

    void testResolveIconPassthrough()
    {
        // Regression: file:// URLs must pass through unchanged, otherwise a
        // resolved local-file icon degrades to default.svg after save+reload.
        QCOMPARE(AgentConfig::resolveIcon(QStringLiteral("file:///C:/icons/a.svg")),
                 QStringLiteral("file:///C:/icons/a.svg"));
        QCOMPARE(AgentConfig::resolveIcon(QStringLiteral("")),
                 QStringLiteral("qrc:/icons/default.svg"));
        QCOMPARE(AgentConfig::resolveIcon(QStringLiteral("qrc:/icons/bot.svg")),
                 QStringLiteral("qrc:/icons/bot.svg"));

        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        QVERIFY(AgentConfig::resolveIcon(tmp.fileName())
                    .startsWith(QStringLiteral("file:///")));
    }

    void testRemovedIdsRoundTrip()
    {
        {
            AgentConfig cfg;
            cfg.load(); // seed the test-mode file from the bundled defaults
            cfg.setRemovedIds({"agent-one", "agent-two"});
            QVERIFY(cfg.save());
        }
        AgentConfig reloaded;
        reloaded.load();
        QCOMPARE(reloaded.removedIds(),
                 QStringList({"agent-one", "agent-two"}));
    }

    void testMigrateSkipsRemovedDefaults()
    {
        {
            AgentConfig cfg;
            cfg.load(); // seed defaults into the test-mode location

            // Simulate a Settings-page deletion: drop the agent from the
            // list AND record its id, then persist (same sequence
            // AgentLauncher::removeAgent() produces).
            QList<Agent> remaining;
            for (const Agent &a : cfg.agents()) {
                if (a.id != QStringLiteral("kimi-code"))
                    remaining.append(a);
            }
            cfg.setAgents(remaining);
            cfg.setRemovedIds({QStringLiteral("kimi-code")});
            QVERIFY(cfg.save());
        }
        AgentConfig reloaded;
        reloaded.load();
        QVERIFY(reloaded.removedIds().contains(QStringLiteral("kimi-code")));
        for (const Agent &a : reloaded.agents())
            QVERIFY2(a.id != "kimi-code", "removed default must not resurrect");
    }

    void testDefaultAgentIds()
    {
        const QStringList ids = AgentConfig::defaultAgentIds();
        QVERIFY(ids.contains(QStringLiteral("kimi-code")));
        QVERIFY(ids.contains(QStringLiteral("opencode")));
    }

    // Regression: the data directory must stay inside the test-mode sandbox.
    // Test mode does not redirect HomeLocation, so a data directory derived
    // from it made every test read and rewrite the real user config — which is
    // how test agents ended up in ~/.AgentLauncher/agents.json.
    void testUserDataDirStaysInTestSandbox()
    {
        const QString realDir =
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QStringLiteral("/.AgentLauncher");
        QVERIFY(!AgentConfig::userDataDir().startsWith(realDir));
        QVERIFY(AgentConfig::configFilePath().startsWith(AgentConfig::userDataDir()));
    }

    // Older builds kept agents.json / agent_state.json under AppConfigLocation;
    // those files must survive the move to the data directory.
    void testMigrateLegacyUserData()
    {
        QTemporaryDir legacyDir;
        QVERIFY(legacyDir.isValid());

        const QString legacyConfig = legacyDir.path() + QStringLiteral("/agents.json");
        QJsonObject agent;
        agent[QStringLiteral("id")] = QStringLiteral("legacy-agent");
        agent[QStringLiteral("name")] = QStringLiteral("Legacy Agent");
        agent[QStringLiteral("command")] = QStringLiteral("legacy.cmd");
        agent[QStringLiteral("webUrl")] = QStringLiteral("http://127.0.0.1:9");
        QJsonObject root;
        root[QStringLiteral("title")] = QStringLiteral("Legacy Title");
        root[QStringLiteral("agents")] = QJsonArray{agent};
        {
            QFile f(legacyConfig);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(root).toJson());
        }
        {
            QFile f(legacyDir.path() + QStringLiteral("/agent_state.json"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"legacy-agent\":{\"setupDone\":true}}");
        }

        AgentConfig::migrateLegacyUserData(legacyDir.path());

        // Both files were copied to the current location...
        QVERIFY(QFile::exists(AgentConfig::configFilePath()));
        QVERIFY(QFile::exists(AgentConfig::userDataDir()
                              + QStringLiteral("/agent_state.json")));
        // ...while the originals stay behind as a fallback for older builds.
        QVERIFY(QFile::exists(legacyConfig));

        AgentConfig migrated;
        migrated.load();
        QCOMPARE(migrated.title(), QStringLiteral("Legacy Title"));
        bool found = false;
        for (const Agent &a : migrated.agents())
            found = found || a.id == QStringLiteral("legacy-agent");
        QVERIFY(found);

        // A config already present in the new location always wins.
        {
            QFile f(AgentConfig::configFilePath());
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QJsonObject current;
            current[QStringLiteral("title")] = QStringLiteral("Current Title");
            current[QStringLiteral("agents")] = QJsonArray();
            f.write(QJsonDocument(current).toJson());
        }
        AgentConfig::migrateLegacyUserData(legacyDir.path());
        AgentConfig current;
        current.load();
        QCOMPARE(current.title(), QStringLiteral("Current Title"));
    }

    void testModelInsertRemove()
    {
        AgentModel model;
        Agent a;
        a.id = "x1";
        a.name = "X";
        model.setAgents({a});
        QCOMPARE(model.rowCount(), 1);

        Agent b;
        b.id = "x2";
        b.name = "Y";
        model.insertAgent(0, b);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.index(0, 0).data(AgentModel::IdRole).toString(),
                 QStringLiteral("x2"));
        QCOMPARE(model.index(1, 0).data(AgentModel::IdRole).toString(),
                 QStringLiteral("x1"));

        QVERIFY(model.removeAgentById("x1"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 0).data(AgentModel::IdRole).toString(),
                 QStringLiteral("x2"));
        QVERIFY(!model.removeAgentById("missing"));
        QCOMPARE(model.rowCount(), 1);
    }

    void testLauncherCrud()
    {
        AgentConfig cfg;
        cfg.load(); // seed test-mode config with the bundled defaults
        AgentModel model;
        model.setAgents(cfg.agents());
        AgentLauncher launcher(&model);
        launcher.setRemovedIds(cfg.removedIds());
        const int baseCount = model.rowCount();
        QVERIFY(baseCount > 0);

        // --- addAgent with auto id --------------------------------------
        QVariantMap fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("My Agent"));
        fields.insert(QStringLiteral("command"), QStringLiteral("myagent web"));
        fields.insert(QStringLiteral("webUrl"), QStringLiteral("http://127.0.0.1:9999"));
        QVERIFY(launcher.addAgent(fields));
        QCOMPARE(model.rowCount(), baseCount + 1);
        QCOMPARE(model.agent(QStringLiteral("my-agent"))
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("My Agent"));
        // Auto-assigned palette color so the card renders correctly.
        QVERIFY(!model.agent(QStringLiteral("my-agent"))
                     .value(QStringLiteral("color")).toString().isEmpty());

        // Persisted?
        AgentConfig persisted;
        persisted.load();
        bool found = false;
        for (const Agent &a : persisted.agents())
            found = found || a.id == "my-agent";
        QVERIFY(found);

        // --- addAgent duplicate name -> -2 suffix ------------------------
        QVERIFY(launcher.addAgent(fields));
        QCOMPARE(model.rowCount(), baseCount + 2);
        QVERIFY(model.indexOf(QStringLiteral("my-agent-2")) >= 0);

        // --- addAgent explicit duplicate id -> false ----------------------
        QVariantMap dup;
        dup.insert(QStringLiteral("id"), QStringLiteral("my-agent"));
        dup.insert(QStringLiteral("name"), QStringLiteral("X"));
        dup.insert(QStringLiteral("command"), QStringLiteral("x"));
        dup.insert(QStringLiteral("webUrl"), QStringLiteral("http://127.0.0.1:1"));
        QVERIFY(!launcher.addAgent(dup));

        // --- updateAgentFull ---------------------------------------------
        fields.insert(QStringLiteral("name"), QStringLiteral("Renamed"));
        QVERIFY(launcher.updateAgentFull(QStringLiteral("my-agent"), fields));
        QCOMPARE(model.agent(QStringLiteral("my-agent"))
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("Renamed"));

        // --- removeAgent of a built-in -> recorded in "removed" ----------
        QVERIFY(launcher.removeAgent(QStringLiteral("kimi-code")));
        QVERIFY(model.indexOf(QStringLiteral("kimi-code")) < 0);
        AgentConfig after;
        after.load();
        QVERIFY(after.removedIds().contains(QStringLiteral("kimi-code")));

        // --- restoreDefaults brings it back -------------------------------
        QVERIFY(launcher.restoreDefaults());
        QVERIFY(model.indexOf(QStringLiteral("kimi-code")) >= 0);
        AgentConfig after2;
        after2.load();
        QVERIFY(!after2.removedIds().contains(QStringLiteral("kimi-code")));

        // --- isDefaultAgent / configFilePath ------------------------------
        QVERIFY(launcher.isDefaultAgent(QStringLiteral("opencode")));
        QVERIFY(!launcher.isDefaultAgent(QStringLiteral("my-agent")));
        QVERIFY(launcher.configFilePath().endsWith(QStringLiteral("agents.json")));
    }
};

QTEST_MAIN(TestCore)
#include "tst_core.moc"
