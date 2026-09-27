#include "agentcatalog/AgentUrls.h"

#include "core/EnvExpander.h"

#include <QFile>
#include <QIODevice>
#include <QRegularExpression>
#include <QUrl>

namespace awb::agentcatalog {

namespace {

bool isLoopbackHost(const QString &host)
{
    const QString h = host.toLower();
    return h == QLatin1String("127.0.0.1") || h == QLatin1String("localhost")
           || h == QLatin1String("::1") || h.startsWith(QLatin1String("127."));
}

// Same server: equal hosts (either spelling of loopback counts) and the
// same effective port (scheme default when absent).
bool sameServer(const QUrl &a, const QUrl &b)
{
    if (a.host().isEmpty() || b.host().isEmpty())
        return false;
    const bool sameHost =
            a.host().compare(b.host(), Qt::CaseInsensitive) == 0
            || (isLoopbackHost(a.host()) && isLoopbackHost(b.host()));
    if (!sameHost)
        return false;
    const auto effectivePort = [](const QUrl &u) {
        const int port = u.port(-1);
        if (port != -1)
            return port;
        return u.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
                ? 443 : 80;
    };
    return effectivePort(a) == effectivePort(b);
}

} // namespace

QString AgentUrls::tokenValue(const QString &tokenFile)
{
    if (tokenFile.isEmpty())
        return {};
    QFile f(core::EnvExpander::expand(tokenFile));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

QString AgentUrls::finalUrl(const AgentDefinition &definition)
{
    return finalUrl(definition.webUrl, definition.tokenFile);
}

QString AgentUrls::finalUrl(const QString &baseUrl, const QString &tokenFile)
{
    if (baseUrl.isEmpty())
        return baseUrl;

    // If a token applies, append it as a URL fragment (#token=<value>) so
    // the web UI can authenticate to mutation routes (e.g. POST /workspaces).
    // The fragment is never sent to the server, keeping the token out of
    // access logs and Referer headers. When the webUrl already carries a
    // fragment, join with '&' — a second '#' would make everything after it
    // part of the first fragment's text. A base that already authenticates
    // (dsh's ?token=… session URL) must not get a second token.
    const QString token = tokenValue(tokenFile);
    if (token.isEmpty() || baseUrl.contains(QLatin1String("token=")))
        return baseUrl;
    return baseUrl
           + (baseUrl.contains(QLatin1Char('#')) ? QStringLiteral("&token=")
                                                 : QStringLiteral("#token="))
           + token;
}

QString AgentUrls::sessionUrlFromOutput(const QString &output,
                                        const QString &webUrl)
{
    const QUrl web(webUrl);
    if (output.isEmpty() || !web.isValid() || web.host().isEmpty())
        return {};

    static const QRegularExpression re(
            QStringLiteral("https?://[^\\s\"'<>]+"),
            QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(output);
    while (it.hasNext()) {
        QString candidate = it.next().captured(0);
        // Console lines hug the URL with sentence punctuation.
        while (candidate.endsWith(QLatin1Char('.'))
               || candidate.endsWith(QLatin1Char(','))
               || candidate.endsWith(QLatin1Char(';'))
               || candidate.endsWith(QLatin1Char(')'))) {
            candidate.chop(1);
        }
        const QUrl parsed(candidate);
        if (parsed.isValid() && sameServer(parsed, web))
            return candidate;
    }
    return {};
}

} // namespace awb::agentcatalog
