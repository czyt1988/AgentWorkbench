#include "awbtest.h"

#include "core/HttpProbe.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using awb::core::HttpProbe;

class TestHttpProbe : public QObject
{
    Q_OBJECT

private slots:
    void testPortFromUrl()
    {
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("http://127.0.0.1:58627")),
                 58627);
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("https://example.com:8443/x")),
                 8443);
        // Scheme defaults when no port is given.
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("http://127.0.0.1")), 80);
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("https://example.com")), 443);
        // Unparseable / empty / non-http scheme without an explicit port.
        QCOMPARE(HttpProbe::portFromUrl(QString()), -1);
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("not a url")), -1);
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("ftp://example.com")), -1);
    }

    // Any HTTP response — even an error status — means the server is up.
    void testErrorStatusStillReachable()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, this, [this, &server]() {
            QTcpSocket *sock = server.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [sock]() {
                sock->readAll();
                sock->write("HTTP/1.1 404 Not Found\r\n"
                            "Content-Length: 0\r\n"
                            "\r\n");
                sock->flush();
                sock->disconnectFromHost();
            });
        });

        HttpProbe probe;
        probe.setTimeoutMs(3000);
        QSignalSpy spy(&probe, &HttpProbe::finished);
        const QString url =
            QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort());
        probe.probe(url);
        QVERIFY(spy.wait(5000));
        const QList<QVariant> row = spy.takeFirst();
        QCOMPARE(row.at(0).toString(), url);
        QCOMPARE(row.at(1).toBool(), true);
    }

    // Connection refused = not reachable.
    void testConnectionRefused()
    {
        quint16 port = 0;
        {
            // Bind once to learn a port nobody else holds, then release it.
            QTcpServer picker;
            QVERIFY(picker.listen(QHostAddress::LocalHost, 0));
            port = picker.serverPort();
        }

        HttpProbe probe;
        probe.setTimeoutMs(3000);
        QSignalSpy spy(&probe, &HttpProbe::finished);
        probe.probe(QStringLiteral("http://127.0.0.1:%1/").arg(port));
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.takeFirst().at(1).toBool(), false);
    }

    // A server that accepts but never answers hits the timeout and is
    // reported as unreachable (AGENTS.md: timeout = stopped).
    void testTimeout()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        // Accept the connection but never write a response.
        connect(&server, &QTcpServer::newConnection, this, [this, &server]() {
            QTcpSocket *sock = server.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [sock]() {
                sock->readAll(); // swallow the request, send nothing
            });
        });

        HttpProbe probe;
        probe.setTimeoutMs(300);
        QSignalSpy spy(&probe, &HttpProbe::finished);
        probe.probe(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()));
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.takeFirst().at(1).toBool(), false);
    }
};

#include "tst_httpprobe.moc"
AWB_TEST(TestHttpProbe)
