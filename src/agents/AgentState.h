#ifndef AWB_AGENTS_AGENTSTATE_H
#define AWB_AGENTS_AGENTSTATE_H

#include <QString>

namespace awb::agents {

// The runtime-only fields of an agent — they exist only in memory and are
// never written to agents.json (01-architecture.md §4.3).
struct AgentState
{
    bool running = false;
    bool launching = false;       // transient UI state, never persisted
    bool installed = false;       // runtime, detected via versionCommand
    QString version;              // runtime, parsed from versionCommand output
    bool installing = false;      // transient UI state, never persisted
    bool setupDone = false;       // recorded in agent_state.json
    bool setupping = false;       // transient UI state, never persisted
    bool checkingVersion = false; // version check in progress
    QString consoleOutput;        // live stdout/stderr of install/update/setup,
                                  // shown on the card so the user can see
                                  // progress
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTSTATE_H
