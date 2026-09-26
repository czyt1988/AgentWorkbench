#ifndef AWB_AGENTS_AGENTURLS_H
#define AWB_AGENTS_AGENTURLS_H

#include "agents/AgentDefinition.h"

#include <QString>

namespace awb::agents {

// URL and token handling for agents. Shared by the embedded view and the
// external browser so both open exactly the same URL
// (01-architecture.md §4.3).
class AgentUrls
{
public:
    // The final URL to open: webUrl plus the bearer token as a #token=<value>
    // fragment when a tokenFile is configured. The fragment never reaches
    // the server or the access logs.
    static QString finalUrl(const AgentDefinition &definition);

    // The bearer token from the agent's tokenFile (env-expanded path,
    // trimmed). Empty when no file is configured, missing or blank.
    static QString tokenValue(const QString &tokenFile);
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTURLS_H
