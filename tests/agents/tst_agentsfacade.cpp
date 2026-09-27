#include "awbtest.h"

#include "agents/AgentModel.h"
#include "agents/AgentRepository.h"
#include "agents/AgentsFacade.h"
#include "core/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using awb::agents::AgentDefinition;
using awb::agents::AgentRepository;
using awb::agents::AgentsFacade;

class TestAgentsFacade : public QObject
{
    Q_OBJECT

private slots:
    // CRUD coverage of the 0.3.0 `launcher` API, now on `agents`
    // (testLauncherCrud).
    void testLauncherCrud()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Seed the data root with the bundled defaults, like a first start.
        {
            AgentRepository seed(tmp.path());
            seed.load();
        }
        awb::core::Settings settings;
        AgentsFacade facade(&settings, tmp.path());

        const int baseCount = facade.agentModel()->rowCount();
        QVERIFY(baseCount > 0);

        // --- addAgent with auto id --------------------------------------
        QVariantMap fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("My Agent"));
        fields.insert(QStringLiteral("command"), QStringLiteral("myagent web"));
        fields.insert(QStringLiteral("webUrl"),
                      QStringLiteral("http://127.0.0.1:9999"));
        QVERIFY(facade.addAgent(fields));
        QCOMPARE(facade.agentModel()->rowCount(), baseCount + 1);
        QCOMPARE(facade.agentModel()
                     ->agent(QStringLiteral("my-agent"))
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("My Agent"));
        // Auto-assigned palette color so the card renders correctly.
        QVERIFY(!facade.agentModel()
                     ->agent(QStringLiteral("my-agent"))
                     .value(QStringLiteral("color")).toString().isEmpty());

        // Persisted?
        AgentRepository persisted(tmp.path());
        persisted.load();
        bool found = false;
        for (const AgentDefinition &a : persisted.definitions())
            found = found || a.id == "my-agent";
        QVERIFY(found);

        // --- addAgent duplicate name -> -2 suffix ------------------------
        QVERIFY(facade.addAgent(fields));
        QCOMPARE(facade.agentModel()->rowCount(), baseCount + 2);
        QVERIFY(facade.agentModel()->indexOf(QStringLiteral("my-agent-2")) >= 0);

        // --- addAgent explicit duplicate id -> false ----------------------
        QVariantMap dup;
        dup.insert(QStringLiteral("id"), QStringLiteral("my-agent"));
        dup.insert(QStringLiteral("name"), QStringLiteral("X"));
        dup.insert(QStringLiteral("command"), QStringLiteral("x"));
        dup.insert(QStringLiteral("webUrl"),
                   QStringLiteral("http://127.0.0.1:1"));
        QVERIFY(!facade.addAgent(dup));

        // --- updateAgentFull ---------------------------------------------
        fields.insert(QStringLiteral("name"), QStringLiteral("Renamed"));
        QVERIFY(facade.updateAgentFull(QStringLiteral("my-agent"), fields));
        QCOMPARE(facade.agentModel()
                     ->agent(QStringLiteral("my-agent"))
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("Renamed"));

        // --- removeAgent of a built-in -> recorded in "removed" ----------
        QVERIFY(facade.removeAgent(QStringLiteral("kimi-code")));
        QVERIFY(facade.agentModel()->indexOf(QStringLiteral("kimi-code")) < 0);
        AgentRepository after(tmp.path());
        after.load();
        QVERIFY(after.removedIds().contains(QStringLiteral("kimi-code")));

        // --- restoreDefaults brings it back -------------------------------
        QVERIFY(facade.restoreDefaults());
        QVERIFY(facade.agentModel()->indexOf(QStringLiteral("kimi-code")) >= 0);
        AgentRepository after2(tmp.path());
        after2.load();
        QVERIFY(!after2.removedIds().contains(QStringLiteral("kimi-code")));

        // --- isDefaultAgent / configFilePath ------------------------------
        QVERIFY(facade.isDefaultAgent(QStringLiteral("opencode")));
        QVERIFY(!facade.isDefaultAgent(QStringLiteral("my-agent")));
        QVERIFY(facade.configFilePath().endsWith(QStringLiteral("agents.json")));
    }
};

#include "tst_agentsfacade.moc"
AWB_TEST(TestAgentsFacade)
