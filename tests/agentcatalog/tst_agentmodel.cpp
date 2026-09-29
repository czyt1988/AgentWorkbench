#include "awbtest.h"

#include "agentcatalog/AgentModel.h"

#include <QtTest>

using awb::agentcatalog::AgentDefinition;
using awb::agentcatalog::AgentModel;
using awb::agentcatalog::AgentState;

/// 测 agentcatalog::AgentModel：增删改对行列与 role 数据的影响、运行状态按
/// id 关联（换定义不丢状态、删 agent 连带删状态），以及 role 名与 0.3.0 的
/// 字节级兼容。
class TestAgentModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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

    // 运行状态按 id 关联：换定义不能把运行中的卡片清空，删除 agent 则
    // 连带删掉它的状态。
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

        // 替换定义（改名）——状态保留。
        a.name = "Renamed";
        QVERIFY(model.replaceDefinition(a));
        QVERIFY(model.state("x1").running);
        QCOMPARE(model.state("x1").version, QStringLiteral("1.2.3"));
        QCOMPARE(model.index(0, 0).data(AgentModel::NameRole).toString(),
                 QStringLiteral("Renamed"));
        QCOMPARE(model.index(0, 0).data(AgentModel::RunningRole).toBool(), true);

        // 删除——状态随之而去。
        QVERIFY(model.removeAgentById("x1"));
        QVERIFY(!model.state("x1").running);
    }

    // role 名必须与 0.3.0 保持字节级一致，卡片 QML 才能原样继续工作。
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
