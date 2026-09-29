#include "awbtest.h"

#include "agentcatalog/AgentUrls.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using awb::agentcatalog::AgentDefinition;
using awb::agentcatalog::AgentUrls;

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

    // The bearer token rides as a #token=<value> fragment so the web UI can
    // authenticate to mutation routes — and the fragment never reaches the
    // server or the access logs.
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

        // A webUrl that already carries a fragment joins with '&' — a
        // second '#' would silently truncate the token (review regression).
        def.webUrl = QStringLiteral("http://127.0.0.1:4096/#/console");
        QCOMPARE(AgentUrls::finalUrl(def),
                 QStringLiteral("http://127.0.0.1:4096/#/console&token=s3cret-value"));
    }

    void testTokenValueEdgeCases()
    {
        // No file configured.
        QVERIFY(AgentUrls::tokenValue(QString()).isEmpty());

        // Missing file.
        QVERIFY(AgentUrls::tokenValue(
                     QStringLiteral("C:/no/such/token-file-awb")).isEmpty());

        // Trimming: stray newlines around the token are removed.
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

    // dsh's console line: the authenticated URL sits among other output and
    // must be found exactly as printed.
    void testSessionUrlFromDshStyleOutput()
    {
        const QString output = QStringLiteral(
            "dsh web: http://127.0.0.1:3080/?token=yJeivyv6PONWpBgMhH_GNB\n"
            "dsh web: opening the default browser; pass --no-open to disable\n");
        QCOMPARE(AgentUrls::sessionUrlFromOutput(
                     output, QStringLiteral("http://127.0.0.1:3080")),
                 QStringLiteral("http://127.0.0.1:3080/?token=yJeivyv6PONWpBgMhH_GNB"));
    }

    // Only URLs at the SAME server match: another port is a different
    // service (and a token-gated one would 401 anyway).
    void testSessionUrlIgnoresOtherServers()
    {
        const QString output = QStringLiteral(
            "listening on http://localhost:5173/\n"
            "docs at https://example.com/docs\n"
            "ready at http://127.0.0.1:4096/\n");
        QVERIFY(AgentUrls::sessionUrlFromOutput(
                     output, QStringLiteral("http://127.0.0.1:3080")).isEmpty());

        // No output, empty/unparseable webUrl: nothing, never a crash.
        QVERIFY(AgentUrls::sessionUrlFromOutput(
                     QString(), QStringLiteral("http://127.0.0.1:3080")).isEmpty());
        QVERIFY(AgentUrls::sessionUrlFromOutput(
                     QStringLiteral("see http://127.0.0.1:3080"), QString()).isEmpty());
        QVERIFY(AgentUrls::sessionUrlFromOutput(
                     QStringLiteral("see http://127.0.0.1:3080"),
                     QStringLiteral("not a url")).isEmpty());
    }

    // localhost and 127.0.0.1 spell the same loopback server; the
    // default-port rule covers URLs printed without a port.
    void testSessionUrlLoopbackEquivalence()
    {
        QCOMPARE(AgentUrls::sessionUrlFromOutput(
                     QStringLiteral("up at http://localhost:3080/x?token=abc"),
                     QStringLiteral("http://127.0.0.1:3080")),
                 QStringLiteral("http://localhost:3080/x?token=abc"));
    }

    // Sentence punctuation hugging the URL is trimmed, not swallowed into
    // the match.
    void testSessionUrlTrimsPunctuation()
    {
        QCOMPARE(AgentUrls::sessionUrlFromOutput(
                     QStringLiteral("ready (url: http://127.0.0.1:3080/?token=t.)"),
                     QStringLiteral("http://127.0.0.1:3080")),
                 QStringLiteral("http://127.0.0.1:3080/?token=t"));
    }

    // A base that already authenticates (dsh's ?token=… session URL, or a
    // fragment token) passes through unchanged — no double token, even when
    // a tokenFile exists and is readable.
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

        // The same readable token file still appends to a plain base.
        QCOMPARE(AgentUrls::finalUrl(QStringLiteral("http://127.0.0.1:3080"),
                                     tokenPath),
                 QStringLiteral("http://127.0.0.1:3080#token=file-token"));

        // Empty base stays empty.
        QVERIFY(AgentUrls::finalUrl(QString(), QString()).isEmpty());
    }
};

#include "tst_agenturls.moc"
AWB_TEST(TestAgentUrls)
