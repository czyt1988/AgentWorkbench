#ifndef AWB_CORE_HTTPPROBE_H
#define AWB_CORE_HTTPPROBE_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace awb::core {

/// 异步 HTTP 健康探测。
///
/// 语义固定：任何 HTTP 响应（含 4xx/5xx）= 可达；连接被拒或超时 = 不可达。
/// 不做进程嗅探——所有工具用同一种方式判定运行状态。
class HttpProbe : public QObject
{
    Q_OBJECT

public:
    explicit HttpProbe(QObject *parent = nullptr);

    // 探测 url；结果经 finished(url, reachable) 异步送达。并发调用安全，
    // 每次探测彼此独立。
    void probe(const QString &url);

    // 超时毫秒数（默认 5000）；超时后放弃并把该 URL 报为不可达
    void setTimeoutMs(int ms);
    int timeoutMs() const { return m_timeoutMs; }

    // web URL 的 TCP 端口：显式端口，缺省时取 scheme 默认（80/443），解析失败为 -1
    static int portFromUrl(const QString &url);

Q_SIGNALS:
    /**
     * @brief 一次探测结束
     * @param url 被探测的 URL（与 probe() 的入参相同）
     * @param reachable true = 收到任何 HTTP 响应；false = 连接被拒或超时
     */
    void finished(const QString &url, bool reachable);

private:
    QNetworkAccessManager m_nam;  ///< 探测请求的发送通道
    int m_timeoutMs = 5000;       ///< 超时毫秒数
};

} // namespace awb::core

#endif // AWB_CORE_HTTPPROBE_H
