#include <QtTest>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "AgentConfig.h"
#include "AgentLauncher.h"
#include "AgentModel.h"
#include "Logger.h"

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
            cfg.load(); // write the bundled defaults into the test-mode file
            cfg.setRemovedIds({"agent-one", "agent-two"});
            QVERIFY(cfg.save());
        }
        AgentConfig reloaded;
        reloaded.load();
        QCOMPARE(reloaded.removedIds(),
                 QStringList({"agent-one", "agent-two"}));
    }

    // A fresh install writes the bundled default through unchanged, so
    // ~/.AgentLauncher/agents.json and config/default_agents.json stay
    // diffable while the shipped launcher list is being edited.
    void testFirstRunCopiesBundledDefaultVerbatim()
    {
        QVERIFY(!QFile::exists(AgentConfig::configFilePath()));
        AgentConfig cfg;
        cfg.load();

        QFile bundled(QStringLiteral(":/config/default_agents.json"));
        QVERIFY(bundled.open(QIODevice::ReadOnly));
        QFile onDisk(AgentConfig::configFilePath());
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        QCOMPARE(onDisk.readAll(), bundled.readAll());

        QCOMPARE(idsOf(cfg.agents()), AgentConfig::defaultAgentIds());
    }

    // Built-in agents are defined by the bundled default alone: an edit there
    // reaches an existing config file, while agents the user added themselves
    // keep their values.
    void testBuiltinAgentsFollowBundledDefault()
    {
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
        writeConfig(root);

        AgentConfig cfg;
        cfg.load();

        // Built-ins first, in the shipped order, then the user's own agent.
        const QStringList ids = idsOf(cfg.agents());
        QCOMPARE(ids, AgentConfig::defaultAgentIds()
                          + QStringList{QStringLiteral("my-agent")});

        const Agent shipped = bundledAgent(QStringLiteral("kimi-code"));
        int staleFields = 0;
        for (const Agent &a : cfg.agents()) {
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
        AgentConfig reloaded;
        reloaded.load();
        QCOMPARE(idsOf(reloaded.agents()), ids);
        QCOMPARE(reloaded.agents().at(AgentConfig::defaultAgentIds().size()).name,
                 QStringLiteral("My Agent"));
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

    // A built-in deleted in the Settings page stays deleted, even though load()
    // re-applies the shipped definition of every other built-in.
    void testDeletedBuiltinStaysDeleted()
    {
        {
            AgentConfig cfg;
            cfg.load();

            // Same sequence AgentLauncher::removeAgent() produces: drop the
            // agent from the list and record its id.
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
            QVERIFY2(a.id != "kimi-code", "deleted built-in must not come back");
        // The other built-ins are all there.
        QCOMPARE(reloaded.agents().size(), AgentConfig::defaultAgentIds().size() - 1);
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

    // A command line is quoted only where it has to be, so the log shows the
    // real thing and it can still be pasted back into cmd.exe.
    void testFormatCommandLine()
    {
        QCOMPARE(Logger::formatCommandLine(QStringLiteral("qwen"),
                                           {QStringLiteral("serve")}),
                 QStringLiteral("qwen serve"));
        QCOMPARE(Logger::formatCommandLine(
                     QStringLiteral("cmd"),
                     {QStringLiteral("/c"),
                      QStringLiteral("C:/Program Files/qwen.cmd"),
                      QStringLiteral("serve")}),
                 QStringLiteral("cmd /c \"C:/Program Files/qwen.cmd\" serve"));
        // An empty argument stays visible instead of collapsing into nothing.
        QCOMPARE(Logger::formatCommandLine(QStringLiteral("x"), {QString()}),
                 QStringLiteral("x \"\""));
    }

    void testClampOutput()
    {
        const QString text(100, QLatin1Char('a'));
        QCOMPARE(Logger::clampOutput(text, 200), text);

        const QString clamped = Logger::clampOutput(text, 10);
        QVERIFY(clamped.startsWith(QStringLiteral("aaaaaaaaaa")));
        QVERIFY(clamped.contains(QStringLiteral("90")));
    }

    // The log rotates at the size limit and keeps at most that many files, so
    // a chatty install can never fill the disk.
    void testLogRotation()
    {
        QCOMPARE(Logger::DEFAULT_MAX_FILES, 3);
        QCOMPARE(Logger::DEFAULT_MAX_FILE_SIZE, qint64(5 * 1024 * 1024));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Tiny files so rotation happens without writing megabytes; one line
        // is already bigger than the limit, so every write rotates.
        Logger::install(dir.path(), 128, 3);
        for (int i = 0; i < 20; ++i)
            qInfo().noquote() << QStringLiteral("rotation line %1").arg(i);
        Logger::uninstall(); // hand the message handler back to QTest

        const QDir logDir(dir.path());
        const QStringList files = logDir.entryList(
            {QStringLiteral("agentlauncher.log*")}, QDir::Files, QDir::Name);
        QCOMPARE(files, QStringList({QStringLiteral("agentlauncher.log"),
                                     QStringLiteral("agentlauncher.log.1"),
                                     QStringLiteral("agentlauncher.log.2")}));

        QString logged;
        for (const QString &name : files) {
            QFile f(logDir.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            logged += QString::fromUtf8(f.readAll());
        }
        // The newest line survived (the last write may have rotated it into
        // .1 already), and the oldest ones were dropped for good.
        QVERIFY(logged.contains(QStringLiteral("rotation line 19")));
        QVERIFY(!logged.contains(QStringLiteral("rotation line 0")));
    }

#ifdef Q_OS_WIN
    // End-to-end check of the command log: launching an install really runs
    // `cmd /c <installCommand>`, and the log then carries the command line,
    // the exit code and the command's own output.
    void testInstallCommandIsLogged()
    {
        QTemporaryDir logDir;
        QVERIFY(logDir.isValid());

        AgentConfig cfg;
        cfg.load();
        AgentModel model;
        model.setAgents(cfg.agents());
        AgentLauncher launcher(&model);

        // The install command is a cmd builtin: no tooling or network needed,
        // and it prints something to capture.
        QVariantMap fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("Log Probe"));
        fields.insert(QStringLiteral("command"), QStringLiteral("logprobe serve"));
        fields.insert(QStringLiteral("webUrl"), QStringLiteral("http://127.0.0.1:9"));
        fields.insert(QStringLiteral("installCommand"),
                      QStringLiteral("echo install-finished"));
        QVERIFY(launcher.addAgent(fields));
        const QString id = QStringLiteral("log-probe");

        QSignalSpy finished(&launcher, &AgentLauncher::installFinished);
        Logger::install(logDir.path());
        launcher.install(id);
        QVERIFY(finished.wait(15000));
        Logger::uninstall();

        QFile log(logDir.filePath(QStringLiteral("agentlauncher.log")));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(log.readAll());
        QVERIFY2(text.contains(QStringLiteral("[cmd] install \"%1\": "
                                              "running: cmd /c echo install-finished")
                                   .arg(id)),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("done, exit=0")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("install-finished")), qPrintable(text));
    }
#endif

private:
    // Write an agents.json into the test-mode data directory.
    static void writeConfig(const QJsonObject &root)
    {
        QDir().mkpath(AgentConfig::userDataDir());
        QFile f(AgentConfig::configFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
    }

    // The shipped definition of a built-in agent.
    static Agent bundledAgent(const QString &id)
    {
        for (const Agent &a : AgentConfig::loadDefaults()) {
            if (a.id == id)
                return a;
        }
        return {};
    }

    static QStringList idsOf(const QList<Agent> &agents)
    {
        QStringList ids;
        for (const Agent &a : agents)
            ids.append(a.id);
        return ids;
    }
};

QTEST_MAIN(TestCore)
#include "tst_core.moc"
