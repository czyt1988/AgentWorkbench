#include "awbtest.h"

#include "agents/AgentUrls.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using awb::agents::AgentDefinition;
using awb::agents::AgentUrls;

class TestAgentUrls : public QObject
{
    Q_OBJECT

private slots:
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
};

#include "tst_agenturls.moc"
AWB_TEST(TestAgentUrls)
