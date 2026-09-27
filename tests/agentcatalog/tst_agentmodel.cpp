#include "awbtest.h"

#include "agentcatalog/AgentModel.h"

#include <QtTest>

using awb::agents::AgentDefinition;
using awb::agents::AgentModel;
using awb::agents::AgentState;

class TestAgentModel : public QObject
{
    Q_OBJECT

private slots:
    void testModelInsertRemove()
    {
        AgentModel model;
        AgentDefinition a;
        a.id = "x1";
        a.name = "X";
        model.setDefinitions({a});
        QCOMPARE(model.rowCount(), 1);

        AgentDefinition b;
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

    // Runtime state is keyed by id: swapping definitions must not blank a
    // running card, and dropping an agent must drop its state.
    void testStateSurvivesDefinitionSwap()
    {
        AgentModel model;
        AgentDefinition a;
        a.id = "x1";
        a.name = "X";
        model.setDefinitions({a});
        model.setRunning("x1", true);
        model.setVersion("x1", QStringLiteral("1.2.3"));
        QVERIFY(model.state("x1").running);

        // Replace the definition (rename) — state stays.
        a.name = "Renamed";
        QVERIFY(model.replaceDefinition(a));
        QVERIFY(model.state("x1").running);
        QCOMPARE(model.state("x1").version, QStringLiteral("1.2.3"));
        QCOMPARE(model.index(0, 0).data(AgentModel::NameRole).toString(),
                 QStringLiteral("Renamed"));
        QCOMPARE(model.index(0, 0).data(AgentModel::RunningRole).toBool(), true);

        // Remove — the state goes with it.
        QVERIFY(model.removeAgentById("x1"));
        QVERIFY(!model.state("x1").running);
    }

    // The role names must stay byte-compatible with 0.3.0 so the card QML
    // keeps working unchanged.
    void testRoleNamesUnchanged()
    {
        AgentModel model;
        const QHash<int, QByteArray> roles = model.roleNames();
        QCOMPARE(roles.value(AgentModel::IdRole), QByteArray("agentId"));
        QCOMPARE(roles.value(AgentModel::NameRole), QByteArray("name"));
        QCOMPARE(roles.value(AgentModel::RunningRole), QByteArray("running"));
        QCOMPARE(roles.value(AgentModel::ConsoleOutputRole),
                 QByteArray("consoleOutput"));
        QCOMPARE(roles.size(), 21);
    }
};

#include "tst_agentmodel.moc"
AWB_TEST(TestAgentModel)
