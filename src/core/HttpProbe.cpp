#include "core/HttpProbe.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace awb::core {

/**
 * @brief 构造健康探测器
 *
 * @param parent QObject 父项
 */
HttpProbe::HttpProbe(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 设置探测超时
 *
 * @param ms 超时毫秒数；到时仍未应答则中止请求并报为不可达
 */
void HttpProbe::setTimeoutMs(int ms)
{
    m_timeoutMs = ms;
}

/**
 * @brief 异步探测一个 URL 是否可达
 *
 * 接受了连接却永不应答的服务器不能把 agent 永远钉在上一个状态：
 * 超时后中止请求并报为不可达（AGENTS.md：连接被拒/超时 = 已停止）。
 * 超时定时器挂在 reply 上，reply 销毁时一并销毁，不留孤儿定时器。
 *
 * @param url 完整的 http(s) URL
 */
void HttpProbe::probe(const QString &url)
{
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(url)));

    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->start(m_timeoutMs);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::finished, this, [this, url, reply]() {
        bool reachable = false;
        if (reply->error() == QNetworkReply::NoError) {
            reachable = true;
        } else {
            // 连接被拒/超时 => 未运行；拿到 HTTP 错误状态（如 401/404）
            // => 服务器在跑。
            const int code =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reachable = (code > 0);
        }
        reply->deleteLater();
        Q_EMIT finished(url, reachable);
    });
}

/**
 * @brief 取 web URL 的 TCP 端口
 *
 * @param url 完整 URL，可为空
 * @return 显式端口；无显式端口时取 scheme 默认（https=443、http=80）；
 *         空串、解析失败或未知 scheme 返回 -1
 */
int HttpProbe::portFromUrl(const QString &url)
{
    if (url.isEmpty()) {
        return -1;
    }
    const QUrl parsed(url);
    if (!parsed.isValid()) {
        return -1;
    }
    const int port = parsed.port();
    if (port > 0) {
        return port;
    }
    // 无显式端口：回退到 scheme 默认值。
    const QString scheme = parsed.scheme().toLower();
    if (scheme == QStringLiteral("https")) {
        return 443;
    }
    if (scheme == QStringLiteral("http")) {
        return 80;
    }
    return -1;
}

} // namespace awb::core
