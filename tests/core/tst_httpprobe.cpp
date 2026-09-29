#include "awbtest.h"

#include "core/HttpProbe.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using awb::core::HttpProbe;

/// 测 core::HttpProbe 的 URL 端口解析与健康检查判定：任何 HTTP 响应（含错误
/// 状态）算在线，连接被拒绝与超时算离线——用本地 QTcpServer 模拟，不依赖网络。
class TestHttpProbe : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testPortFromUrl()
    {
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("http://127.0.0.1:58627")),
                 58627);
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("https://example.com:8443/x")),
                 8443);
        // 未写端口时按 scheme 取默认端口。
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("http://127.0.0.1")), 80);
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("https://example.com")), 443);
        // 解析失败 / 空串 / 非 http scheme 且无显式端口。
        QCOMPARE(HttpProbe::portFromUrl(QString()), -1);
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("not a url")), -1);
        QCOMPARE(HttpProbe::portFromUrl(QStringLiteral("ftp://example.com")), -1);
    }

    // 任何 HTTP 响应——哪怕是错误状态码——都算服务在线。
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

    // 连接被拒绝 = 离线。
    void testConnectionRefused()
    {
        quint16 port = 0;
        {
            // 先绑定一次占住端口拿到端口号，再释放，保证探测时无人监听。
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

    // 接受连接却永不应答的服务会触发超时并报离线（AGENTS.md：超时 = 已停止）。
    void testTimeout()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        // 接受连接但不写任何响应。
        connect(&server, &QTcpServer::newConnection, this, [this, &server]() {
            QTcpSocket *sock = server.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [sock]() {
                sock->readAll(); // 吞掉请求，什么都不回
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
