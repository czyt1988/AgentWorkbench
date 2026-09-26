#ifndef AWB_WEB_WEBPROFILEPATHS_H
#define AWB_WEB_WEBPROFILEPATHS_H

#include <QString>

namespace awb::web {

// Per-agent persistent profile locations (01-architecture.md §4.5).
//
// One profile per agent is a HARD requirement: Chromium indexes cookies by
// host and IGNORES the port, so a shared profile would cross-contaminate
// 127.0.0.1:58627 with 127.0.0.1:4096 sessions (docs/research/
// webengine-embedding.md §3.1 has measured evidence).
class WebProfilePaths
{
public:
    // <dataRoot>/webprofiles/<agentId> — cookies + localStorage on disk.
    static QString profileDir(const QString &agentId);

    // WebEngine storageName for the agent ("awb-<agentId>").
    static QString storageName(const QString &agentId);
};

} // namespace awb::web

#endif // AWB_WEB_WEBPROFILEPATHS_H
