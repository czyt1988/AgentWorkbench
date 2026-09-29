#include "awbtest.h"

#include "agentcatalog/AgentUrls.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using awb::agentcatalog::AgentDefinition;
using awb::agentcatalog::AgentUrls;

/// 测 agentcatalog::AgentUrls 的 URL 组装：#token= 片段追加（含已有 fragment
/// 的拼接规则）、token 文件读取与修剪、从启动输出提取会话 URL（同服务器匹配、
/// 回环等价、标点修剪）以及已带 token 的 base 原样透传。
class TestAgentUrls : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testFinalUrlWithoutToken()
    {
        AgentDefinition def;
        def.webUrl = QStringLiteral("http://127.0.0.1:58627");
        QCOMPARE(AgentUrls::finalUrl(def), def.webUrl);

        def.webUrl.clear();
        QVERIFY(AgentUrls::finalUrl(def).isEmpty());
    }

    // bearer token 以 #token=<value> 片段搭车，web UI 据此向变更类路由鉴权
    // ——片段不会到达服务器，也不进访问日志。
    void testFinalUrlAppendsTokenFragment()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString tokenPath = tmp.path() + QStringLiteral("/token");
        QFile f(tokenPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("s3cret-value\n");
        f.close();

        AgentDefinition def;
        def.webUrl = QStringLiteral("http://127.0.0.1:4096");
        def.tokenFile = tokenPath;
        QCOMPARE(AgentUrls::finalUrl(def),
                 QStringLiteral("http://127.0.0.1:4096#token=s3cret-value"));

        // webUrl 已带 fragment 时用 '&' 接续——再来一个 '#' 会静默截断
        // token（评审发现的回归）。
        def.webUrl = QStringLiteral("http://127.0.0.1:4096/#/console");
        QCOMPARE(AgentUrls::finalUrl(def),
                 QStringLiteral("http://127.0.0.1:4096/#/console&token=s3cret-value"));
    }

    void testTokenValueEdgeCases()
    {
        // 未配置文件。
        QVERIFY(AgentUrls::tokenValue(QString()).isEmpty());

        // 文件不存在。
        QVERIFY(AgentUrls::tokenValue(
                    QStringLiteral("C:/no/such/token-file-awb")).isEmpty());

        // 修剪：token 前后误带的换行被去掉。
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString tokenPath = tmp.path() + QStringLiteral("/token");
        QFile f(tokenPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("  padded-token  \n");
        f.close();
        QCOMPARE(AgentUrls::tokenValue(tokenPath), QStringLiteral("padded-token"));
    }

    // --- sessionUrlFromOutput / finalUrl(base, tokenFile) ------------------

    // dsh 的控制台行：带鉴权的 URL 混在其它输出中间，必须按打印原样找到。
    void testSessionUrlFromDshStyleOutput()
    {
        const QString output = QStringLiteral(
            "dsh web: http://127.0.0.1:3080/?token=yJeivyv6PONWpBgMhH_GNB\n"
            "dsh web: opening the default browser; pass --no-open to disable\n");
        QCOMPARE(AgentUrls::sessionUrlFromOutput(
                     output, QStringLiteral("http://127.0.0.1:3080")),
                 QStringLiteral("http://127.0.0.1:3080/?token=yJeivyv6PONWpBgMhH_GNB"));
    }

    // 只有指向同一服务器的 URL 才匹配：换个端口就是另一个服务（而且带 token
    // 门禁的那个本来就会 401）。
    void testSessionUrlIgnoresOtherServers()
    {
        const QString output = QStringLiteral(
            "listening on http://localhost:5173/\n"
            "docs at https://example.com/docs\n"
            "ready at http://127.0.0.1:4096/\n");
        QVERIFY(AgentUrls::sessionUrlFromOutput(
                     output, QStringLiteral("http://127.0.0.1:3080")).isEmpty());

        // 无输出、webUrl 为空或解析不了：返回空，绝不崩溃。
        QVERIFY(AgentUrls::sessionUrlFromOutput(
                     QString(), QStringLiteral("http://127.0.0.1:3080")).isEmpty());
        QVERIFY(AgentUrls::sessionUrlFromOutput(
                     QStringLiteral("see http://127.0.0.1:3080"), QString()).isEmpty());
        QVERIFY(AgentUrls::sessionUrlFromOutput(
                     QStringLiteral("see http://127.0.0.1:3080"),
                     QStringLiteral("not a url")).isEmpty());
    }

    // localhost 与 127.0.0.1 拼的是同一个回环服务器；默认端口规则覆盖
    // 打印时不带端口的 URL。
    void testSessionUrlLoopbackEquivalence()
    {
        QCOMPARE(AgentUrls::sessionUrlFromOutput(
                     QStringLiteral("up at http://localhost:3080/x?token=abc"),
                     QStringLiteral("http://127.0.0.1:3080")),
                 QStringLiteral("http://localhost:3080/x?token=abc"));
    }

    // 贴着 URL 的句子标点要被修剪掉，而不是吞进匹配结果。
    void testSessionUrlTrimsPunctuation()
    {
        QCOMPARE(AgentUrls::sessionUrlFromOutput(
                     QStringLiteral("ready (url: http://127.0.0.1:3080/?token=t.)"),
                     QStringLiteral("http://127.0.0.1:3080")),
                 QStringLiteral("http://127.0.0.1:3080/?token=t"));
    }

    // 已自带鉴权的 base（dsh 的 ?token=… 会话 URL，或 fragment 形式的
    // token）原样透传——不叠加第二个 token，即使配置了可读的 tokenFile。
    void testFinalUrlBaseWithOwnTokenUnchanged()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString tokenPath = tmp.path() + QStringLiteral("/token");
        QFile f(tokenPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("file-token\n");
        f.close();

        const QString sessionUrl =
            QStringLiteral("http://127.0.0.1:3080/?token=yJei_vy");
        QCOMPARE(AgentUrls::finalUrl(sessionUrl, tokenPath), sessionUrl);
        QCOMPARE(AgentUrls::finalUrl(
                     QStringLiteral("http://127.0.0.1:3080/#token=abc"), tokenPath),
                 QStringLiteral("http://127.0.0.1:3080/#token=abc"));

        // 同一个可读的 token 文件对普通 base 照常追加。
        QCOMPARE(AgentUrls::finalUrl(QStringLiteral("http://127.0.0.1:3080"),
                                     tokenPath),
                 QStringLiteral("http://127.0.0.1:3080#token=file-token"));

        // 空 base 保持为空。
        QVERIFY(AgentUrls::finalUrl(QString(), QString()).isEmpty());
    }
};

#include "tst_agenturls.moc"
AWB_TEST(TestAgentUrls)
