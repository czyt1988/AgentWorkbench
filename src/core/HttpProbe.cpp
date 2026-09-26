#include "core/HttpProbe.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace awb::core {

HttpProbe::HttpProbe(QObject *parent)
    : QObject(parent)
{
}

void HttpProbe::setTimeoutMs(int ms)
{
    m_timeoutMs = ms;
}

void HttpProbe::probe(const QString &url)
{
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(url)));

    // A server that accepts the connection but never answers must not pin
    // the agent in its previous state forever: abort after the timeout and
    // report unreachable (AGENTS.md: refused/timeout = stopped).
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->start(m_timeoutMs);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::finished, this, [this, url, reply]() {
        bool reachable = false;
        if (reply->error() == QNetworkReply::NoError) {
            reachable = true;
        } else {
            // Connection-refused/timeout => not running.
            // Got an HTTP error status (e.g. 401/404) => server is up.
            const int code =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reachable = (code > 0);
        }
        reply->deleteLater();
        emit finished(url, reachable);
    });
}

int HttpProbe::portFromUrl(const QString &url)
{
    if (url.isEmpty())
        return -1;
    const QUrl parsed(url);
    if (!parsed.isValid())
        return -1;
    const int port = parsed.port();
    if (port > 0)
        return port;
    // No explicit port: fall back to the scheme default.
    const QString scheme = parsed.scheme().toLower();
    if (scheme == QLatin1String("https"))
        return 443;
    if (scheme == QLatin1String("http"))
        return 80;
    return -1;
}

} // namespace awb::core
