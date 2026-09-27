#include "awbtest.h"

#include "agents/AgentRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

using awb::agents::AgentDefinition;
using awb::agents::AgentRepository;

class TestAgentRepository : public QObject
{
    Q_OBJECT

private slots:
    void testSlugFromName()
    {
        QCOMPARE(AgentRepository::slugFromName(QStringLiteral("Kimi Code")),
                 QStringLiteral("kimi-code"));
        QCOMPARE(AgentRepository::slugFromName(QStringLiteral("My_Agent! 2")),
                 QStringLiteral("my-agent-2"));
        QCOMPARE(AgentRepository::slugFromName(QStringLiteral("  --Trim--  ")),
                 QStringLiteral("trim"));
        QCOMPARE(AgentRepository::slugFromName(QStringLiteral("???")),
                 QStringLiteral("agent"));
    }

    void testRemovedIdsRoundTrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository repo(tmp.path());
            repo.load(); // write the bundled defaults into the data root
            repo.setRemovedIds({"agent-one", "agent-two"});
            QVERIFY(repo.save());
        }
        AgentRepository reloaded(tmp.path());
        reloaded.load();
        QCOMPARE(reloaded.removedIds(),
                 QStringList({"agent-one", "agent-two"}));
    }

    // A fresh install writes the bundled default through unchanged, so
    // <dataRoot>/agents.json and config/default_agents.json stay diffable
    // while the shipped launcher list is being edited.
    void testFirstRunCopiesBundledDefaultVerbatim()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/agents.json")));

        AgentRepository repo(tmp.path());
        repo.load();

        QFile bundled(QStringLiteral(":/config/default_agents.json"));
        QVERIFY(bundled.open(QIODevice::ReadOnly));
        QFile onDisk(repo.configFilePath());
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        QCOMPARE(onDisk.readAll(), bundled.readAll());

        QCOMPARE(idsOf(repo.definitions()), AgentRepository::defaultAgentIds());
    }

    // Built-in agents are defined by the bundled default alone: an edit there
    // reaches an existing config file, while agents the user added themselves
    // keep their values.
    void testBuiltinAgentsFollowBundledDefault()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject stale;
        stale[QStringLiteral("id")] = QStringLiteral("kimi-code");
        stale[QStringLiteral("name")] = QStringLiteral("Stale Name");
        stale[QStringLiteral("command")] = QStringLiteral("stale-command");
        stale[QStringLiteral("webUrl")] = QStringLiteral("http://127.0.0.1:1");
        stale[QStringLiteral("color")] = QStringLiteral("#123456");
        QJsonObject mine;
        mine[QStringLiteral("id")] = QStringLiteral("my-agent");
        mine[QStringLiteral("name")] = QStringLiteral("My Agent");
        mine[QStringLiteral("command")] = QStringLiteral("myagent web");
        mine[QStringLiteral("webUrl")] = QStringLiteral("http://127.0.0.1:9999");
        mine[QStringLiteral("color")] = QStringLiteral("#00d4aa");
        QJsonObject root;
        root[QStringLiteral("agents")] = QJsonArray{stale, mine};
        writeConfig(tmp.path(), root);

        AgentRepository repo(tmp.path());
        repo.load();

        // Built-ins first, in the shipped order, then the user's own agent.
        const QStringList ids = idsOf(repo.definitions());
        QCOMPARE(ids, AgentRepository::defaultAgentIds()
                          + QStringList{QStringLiteral("my-agent")});

        const AgentDefinition shipped =
            bundledAgent(QStringLiteral("kimi-code"));
        int staleFields = 0;
        for (const AgentDefinition &a : repo.definitions()) {
            if (a.id == QStringLiteral("kimi-code")) {
                QCOMPARE(a.name, shipped.name);
                QCOMPARE(a.command, shipped.command);
                QCOMPARE(a.webUrl, shipped.webUrl);
                QCOMPARE(a.color, shipped.color);
                ++staleFields;
            } else if (a.id == QStringLiteral("my-agent")) {
                QCOMPARE(a.name, QStringLiteral("My Agent"));
                QCOMPARE(a.command, QStringLiteral("myagent web"));
                QCOMPARE(a.color, QStringLiteral("#00d4aa"));
                ++staleFields;
            }
        }
        QCOMPARE(staleFields, 2);

        // The refresh is on disk, not just in memory.
        AgentRepository reloaded(tmp.path());
        reloaded.load();
        QCOMPARE(idsOf(reloaded.definitions()), ids);
        QCOMPARE(reloaded.definitions()
                     .at(AgentRepository::defaultAgentIds().size()).name,
                 QStringLiteral("My Agent"));
    }

    void testDefaultAgentIds()
    {
        const QStringList ids = AgentRepository::defaultAgentIds();
        QVERIFY(ids.contains(QStringLiteral("kimi-code")));
        QVERIFY(ids.contains(QStringLiteral("opencode")));
    }

    // A built-in deleted in the Settings page stays deleted, even though
    // load() re-applies the shipped definition of every other built-in.
    void testDeletedBuiltinStaysDeleted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository repo(tmp.path());
            repo.load();

            // Same sequence AgentsFacade::removeAgent() produces: drop the
            // agent from the list and record its id.
            QList<AgentDefinition> remaining;
            for (const AgentDefinition &a : repo.definitions()) {
                if (a.id != QStringLiteral("kimi-code"))
                    remaining.append(a);
            }
            repo.setDefinitions(remaining);
            repo.setRemovedIds({QStringLiteral("kimi-code")});
            QVERIFY(repo.save());
        }
        AgentRepository reloaded(tmp.path());
        reloaded.load();
        QVERIFY(reloaded.removedIds().contains(QStringLiteral("kimi-code")));
        for (const AgentDefinition &a : reloaded.definitions())
            QVERIFY2(a.id != "kimi-code", "deleted built-in must not come back");
        // The other built-ins are all there.
        QCOMPARE(reloaded.definitions().size(),
                 AgentRepository::defaultAgentIds().size() - 1);
    }

    // 0.4.0: the root "title" field no longer drives the window title (it
    // moved to settings.json). Loading ignores the field, saving must not
    // write it back, and a leftover value is reported once a settings file
    // exists to move it to.
    void testTitleIsIgnored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject user;
        user[QStringLiteral("id")] = QStringLiteral("my-agent");
        user[QStringLiteral("name")] = QStringLiteral("My Agent");
        user[QStringLiteral("command")] = QStringLiteral("myagent web");
        QJsonObject root;
        root[QStringLiteral("title")] = QStringLiteral("Hand-edited Title");
        root[QStringLiteral("agents")] = QJsonArray{user};
        writeConfig(tmp.path(), root);

        // The deprecation hint fires only once a settings file exists.
        QFile settings(tmp.path() + QStringLiteral("/settings.json"));
        QVERIFY(settings.open(QIODevice::WriteOnly | QIODevice::Truncate));
        settings.write("{}");
        settings.close();

        s_capturedMessages.clear();
        const QtMessageHandler previous =
            qInstallMessageHandler(&TestAgentRepository::captureMessage);
        AgentRepository repo(tmp.path());
        repo.load();
        qInstallMessageHandler(previous);

        bool hinted = false;
        for (const QString &msg : std::as_const(s_capturedMessages)) {
            if (msg.contains(QStringLiteral("\"title\" field is ignored")))
                hinted = true;
        }
        QVERIFY2(hinted, "a legacy root title must be reported as ignored");

        // The title is dropped from the saved file.
        QVERIFY(repo.save());
        QFile onDisk(repo.configFilePath());
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        const QJsonObject saved =
            QJsonDocument::fromJson(onDisk.readAll()).object();
        QVERIFY(!saved.contains(QStringLiteral("title")));
        QVERIFY(saved.value(QStringLiteral("agents")).toArray().size() >= 1);
    }

private:
    // Message capture for asserting on log output (testTitleIsIgnored).
    static QStringList s_capturedMessages;
    static void captureMessage(QtMsgType, const QMessageLogContext &,
                               const QString &msg)
    {
        s_capturedMessages.append(msg);
    }

    // Write an agents.json into the given data root.
    static void writeConfig(const QString &dataRoot, const QJsonObject &root)
    {
        QDir().mkpath(dataRoot);
        QFile f(dataRoot + QStringLiteral("/agents.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
    }

    // The shipped definition of a built-in agent.
    static AgentDefinition bundledAgent(const QString &id)
    {
        for (const AgentDefinition &a : AgentRepository::loadDefaults()) {
            if (a.id == id)
                return a;
        }
        return {};
    }

    static QStringList idsOf(const QList<AgentDefinition> &agents)
    {
        QStringList ids;
        for (const AgentDefinition &a : agents)
            ids.append(a.id);
        return ids;
    }
};

QStringList TestAgentRepository::s_capturedMessages;

#include "tst_agentrepository.moc"
AWB_TEST(TestAgentRepository)
