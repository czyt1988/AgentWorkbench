#ifndef AWB_CORE_LEGACYIMPORT_H
#define AWB_CORE_LEGACYIMPORT_H

#include <QString>

namespace awb::core {

// One-time adoption of the pre-0.4 AgentLauncher data directory
// (~/.AgentLauncher) into the new data root (~/.AgentWorkbench).
// Contract:
//
// The legacy directory is copied, never moved or deleted, and the import
// runs at most once: as soon as the new data root holds anything besides
// the log directory (Logging installs first, see), it is left alone.
class LegacyImport
{
public:
    // Adopt the legacy directory into `newRoot` (the app data root).
    // Returns true when files were imported; `outNotice` then receives a
    // user-facing message to show once in the UI. Safe to call on every
    // start — subsequent calls are no-ops.
    static bool runOnce(const QString &newRoot, QString *outNotice = nullptr);

    // Same, with both directories injected so tests can use temp dirs.
    static bool importOnce(const QString &newRoot, const QString &oldRoot,
                           QString *outNotice = nullptr);
};

} // namespace awb::core

#endif // AWB_CORE_LEGACYIMPORT_H
