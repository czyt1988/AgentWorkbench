#include "awbtest.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentRepository.h"
#include "agentcatalog/AgentsFacade.h"
#include "core/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using awb::agentcatalog::AgentDefinition;
using awb::agentcatalog::AgentRepository;
using awb::agentcatalog::AgentsFacade;

/// 测 agentcatalog::AgentsFacade 的 0.3.0 `launcher` API（现挂在 `agents` 上）：
/// addAgent 的自动 id/重名后缀/显式重复 id、updateAgentFull、removeAgent 的
/// removed 记录、restoreDefaults 与 isDefaultAgent/configFilePath。
class TestAgentsFacade : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // 0.3.0 `launcher` API 的 CRUD 覆盖，现在挂在 `agents` 上
    // （testLauncherCrud）。
    void testLauncherCrud()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // 像首次启动一样，先把随包默认种进数据根。
        {
            AgentRepository seed(tmp.path());
            seed.load();
        }
        awb::core::Settings settings;
        AgentsFacade facade(&settings, tmp.path());

        const int baseCount = facade.agentModel()->rowCount();
        QVERIFY(baseCount > 0);

        // --- addAgent 自动分配 id ----------------------------------------
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
        // 自动分配调色板颜色，卡片才能正确渲染。
        QVERIFY(!facade.agentModel()
                     ->agent(QStringLiteral("my-agent"))
                     .value(QStringLiteral("color")).toString().isEmpty());

        // 落盘了吗？
        AgentRepository persisted(tmp.path());
        persisted.load();
        bool found = false;
        for (const AgentDefinition &a : persisted.definitions()) {
            found = found || a.id == "my-agent";
        }
        QVERIFY(found);

        // --- addAgent 重名 -> -2 后缀 ------------------------------------
        QVERIFY(facade.addAgent(fields));
        QCOMPARE(facade.agentModel()->rowCount(), baseCount + 2);
        QVERIFY(facade.agentModel()->indexOf(QStringLiteral("my-agent-2")) >= 0);

        // --- addAgent 显式重复 id -> false --------------------------------
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

        // --- removeAgent 删除内置 -> 记进 "removed" -----------------------
        QVERIFY(facade.removeAgent(QStringLiteral("kimi-code")));
        QVERIFY(facade.agentModel()->indexOf(QStringLiteral("kimi-code")) < 0);
        AgentRepository after(tmp.path());
        after.load();
        QVERIFY(after.removedIds().contains(QStringLiteral("kimi-code")));

        // --- restoreDefaults 把它带回来 ------------------------------------
        QVERIFY(facade.restoreDefaults());
        QVERIFY(facade.agentModel()->indexOf(QStringLiteral("kimi-code")) >= 0);
        AgentRepository after2(tmp.path());
        after2.load();
        QVERIFY(!after2.removedIds().contains(QStringLiteral("kimi-code")));

        // --- isDefaultAgent / configFilePath -------------------------------
        QVERIFY(facade.isDefaultAgent(QStringLiteral("opencode")));
        QVERIFY(!facade.isDefaultAgent(QStringLiteral("my-agent")));
        QVERIFY(facade.configFilePath().endsWith(QStringLiteral("agents.json")));
    }
};

#include "tst_agentsfacade.moc"
AWB_TEST(TestAgentsFacade)
