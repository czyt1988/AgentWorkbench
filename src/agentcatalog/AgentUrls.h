#ifndef AWB_AGENTS_AGENTURLS_H
#define AWB_AGENTS_AGENTURLS_H

#include "agentcatalog/AgentDefinition.h"

#include <QString>

namespace awb::agentcatalog {

// URL and token handling for agents. Shared by the embedded view and the
// external browser so both open exactly the same URL
class AgentUrls
{
public:
    // The final URL to open: webUrl plus the bearer token as a #token=<value>
    // fragment when a tokenFile is configured. The fragment never reaches
    // the server or the access logs.
    static QString finalUrl(const AgentDefinition &definition);

    // Same merge for an arbitrary base — the session URL captured from the
    // agent's own output instead of the configured webUrl. A base that
    // already carries a token= (query or fragment) passes through unchanged.
    static QString finalUrl(const QString &baseUrl, const QString &tokenFile);

    // The bearer token from the agent's tokenFile (env-expanded path,
    // trimmed). Empty when no file is configured, missing or blank.
    static QString tokenValue(const QString &tokenFile);

    // The first URL in the agent's process output that points at the same
    // server as `webUrl` (host-equal, or both loopback spellings of it,
    // same effective port). Token-gated harnesses such as dsh print a
    // per-process authenticated URL to stdout; this finds it. Empty when
    // nothing matches.
    static QString sessionUrlFromOutput(const QString &output,
                                        const QString &webUrl);
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTURLS_H
