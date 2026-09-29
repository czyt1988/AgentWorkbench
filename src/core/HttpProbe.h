#ifndef AWB_CORE_HTTPPROBE_H
#define AWB_CORE_HTTPPROBE_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace awb::core {

// Asynchronous HTTP health probe.
//
// Semantics are fixed: ANY HTTP response (including 4xx/5xx) = reachable;
// connection refused or timeout = not reachable. No process sniffing —
// every tool is detected the same way.
class HttpProbe : public QObject
{
    Q_OBJECT

public:
    explicit HttpProbe(QObject *parent = nullptr);

    // Probe `url`; the result arrives as finished(url, reachable).
    // Concurrent probes are fine — each call is independent.
    void probe(const QString &url);

    // Give up after this many milliseconds (default 5000) and report the
    // URL as unreachable.
    void setTimeoutMs(int ms);
    int timeoutMs() const { return m_timeoutMs; }

    // TCP port of a web URL: the explicit port, the scheme default
    // (80/443) when absent, -1 when unparseable.
    static int portFromUrl(const QString &url);

Q_SIGNALS:
    void finished(const QString &url, bool reachable);

private:
    QNetworkAccessManager m_nam;
    int m_timeoutMs = 5000;
};

} // namespace awb::core

#endif // AWB_CORE_HTTPPROBE_H
