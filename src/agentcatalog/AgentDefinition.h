#ifndef AWB_AGENTS_AGENTDEFINITION_H
#define AWB_AGENTS_AGENTDEFINITION_H

#include <QString>

namespace awb::agentcatalog {

// The persisted fields of an agent — everything written to agents.json.
// Runtime state lives in AgentState and is never persisted here.
struct AgentDefinition
{
    QString id;
    QString name;
    QString command;
    QString webUrl;
    QString configDir;
    QString icon;
    QString color;
    QString cardColor; // optional card background color (non-running state)
    QString installCommand;
    QString updateCommand;
    QString versionCommand;
    QString setupCommand; // one-time setup command run before first launch
    QString tokenFile;    // path to a bearer token file (env-expanded); set as
                          // QWEN_SERVER_TOKEN on launch and appended as
                          // #token=<value> to the web URL when opening the
                          // browser
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTDEFINITION_H
