#include "agents/AgentUrls.h"

#include "core/EnvExpander.h"

#include <QFile>
#include <QIODevice>

namespace awb::agents {

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
    QString url = definition.webUrl;
    if (url.isEmpty())
        return url;

    // If a token file is configured, append the token as a URL fragment
    // (#token=<value>) so the web UI can authenticate to mutation routes
    // (e.g. POST /workspaces). The fragment is never sent to the server,
    // keeping the token out of access logs and Referer headers. When the
    // webUrl already carries a fragment, join with '&' — a second '#' would
    // make everything after it part of the first fragment's text.
    const QString token = tokenValue(definition.tokenFile);
    if (!token.isEmpty())
        url += (url.contains(QLatin1Char('#')) ? QStringLiteral("&token=")
                                               : QStringLiteral("#token="))
               + token;
    return url;
}

} // namespace awb::agents
