#include "awbtest.h"

#include "agentcatalog/AgentRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

using awb::agentcatalog::AgentDefinition;
using awb::agentcatalog::AgentRepository;

/// 测 agentcatalog::AgentRepository：名称转 slug、removedIds 的持久化、首次
/// 运行逐字节写入内置默认、内置 agent 跟随随包默认刷新而自建项保留、删除的
/// 内置保持删除，以及根级 title 字段的停用行为。
class TestAgentRepository : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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
            repo.load(); // 把随包默认写进数据根
            repo.setRemovedIds({"agent-one", "agent-two"});
            QVERIFY(repo.save());
        }
        AgentRepository reloaded(tmp.path());
        reloaded.load();
        QCOMPARE(reloaded.removedIds(),
                 QStringList({"agent-one", "agent-two"}));
    }

    // 全新安装把随包默认原样写穿：<dataRoot>/agents.json 与
    // config/default_agents.json 保持可 diff，方便随包列表还在编辑期时比对。
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

    // 内置 agent 只由随包默认定义：那边一改就能进到既有配置文件里，
    // 而用户自建的 agent 保留自己的值。
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

        // 内置在前、按随包顺序，然后才是用户自建的 agent。
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

        // 刷新落在磁盘上，不只是内存里。
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

    // 在设置页删除的内置 agent 保持删除，尽管 load() 会对其余内置重新应用
    // 随包定义。
    void testDeletedBuiltinStaysDeleted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository repo(tmp.path());
            repo.load();

            // 与 AgentsFacade::removeAgent() 产生的序列一致：先从列表剔除
            // 该 agent，再记录它的 id。
            QList<AgentDefinition> remaining;
            for (const AgentDefinition &a : repo.definitions()) {
                if (a.id != QStringLiteral("kimi-code")) {
                    remaining.append(a);
                }
            }
            repo.setDefinitions(remaining);
            repo.setRemovedIds({QStringLiteral("kimi-code")});
            QVERIFY(repo.save());
        }
        AgentRepository reloaded(tmp.path());
        reloaded.load();
        QVERIFY(reloaded.removedIds().contains(QStringLiteral("kimi-code")));
        for (const AgentDefinition &a : reloaded.definitions()) {
            QVERIFY2(a.id != "kimi-code", "deleted built-in must not come back");
        }
        // 其余内置一个不少。
        QCOMPARE(reloaded.definitions().size(),
                 AgentRepository::defaultAgentIds().size() - 1);
    }

    // 0.4.0：根级 "title" 字段不再驱动窗口标题（已挪到 settings.json）。
    // 加载忽略该字段、保存不得写回；残留值只在存在可迁移的 settings 文件时
    // 提示一次。
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

        // 停用提示只在 settings 文件已存在时触发。
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
            if (msg.contains(QStringLiteral("\"title\" field is ignored"))) {
                hinted = true;
            }
        }
        QVERIFY2(hinted, "a legacy root title must be reported as ignored");

        // title 从保存后的文件里消失。
        QVERIFY(repo.save());
        QFile onDisk(repo.configFilePath());
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        const QJsonObject saved =
            QJsonDocument::fromJson(onDisk.readAll()).object();
        QVERIFY(!saved.contains(QStringLiteral("title")));
        QVERIFY(saved.value(QStringLiteral("agents")).toArray().size() >= 1);
    }

private:
    /// 捕获到的 Qt 日志消息，供 testTitleIsIgnored 断言日志输出
    static QStringList s_capturedMessages;

    /**
     * @brief 安装给 qInstallMessageHandler 的消息处理器，把日志追加进
     *        s_capturedMessages
     *
     * @param type 消息级别（本测试不区分，忽略）
     * @param context 日志上下文（忽略）
     * @param msg 日志正文
     */
    static void captureMessage(QtMsgType, const QMessageLogContext &,
                               const QString &msg)
    {
        s_capturedMessages.append(msg);
    }

    /**
     * @brief 把给定的配置对象写进指定数据根下的 agents.json
     *
     * @param dataRoot 数据根目录（不存在会先创建）
     * @param root 完整的 agents.json 根对象
     */
    static void writeConfig(const QString &dataRoot, const QJsonObject &root)
    {
        QDir().mkpath(dataRoot);
        QFile f(dataRoot + QStringLiteral("/agents.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
    }

    /**
     * @brief 取某个内置 agent 的随包定义
     *
     * @param id agent id
     * @return 对应的随包定义；id 不在随包列表里时返回默认构造值
     */
    static AgentDefinition bundledAgent(const QString &id)
    {
        for (const AgentDefinition &a : AgentRepository::loadDefaults()) {
            if (a.id == id) {
                return a;
            }
        }
        return {};
    }

    /**
     * @brief 收集 agent 列表的 id 序列
     *
     * @param agents agent 定义列表
     * @return 按列表顺序排列的 id
     */
    static QStringList idsOf(const QList<AgentDefinition> &agents)
    {
        QStringList ids;
        for (const AgentDefinition &a : agents) {
            ids.append(a.id);
        }
        return ids;
    }
};

QStringList TestAgentRepository::s_capturedMessages;

#include "tst_agentrepository.moc"
AWB_TEST(TestAgentRepository)
