#ifndef AWB_CORE_PATHS_H
#define AWB_CORE_PATHS_H

#include <QString>

namespace awb::core {

// The single source of truth for where the application keeps user data.
// Everything else asks Paths — no other module
// may derive the data root itself.
//
// Non-ASCII user profiles (C:\Users\陈宗衍\…) are handled by using
// QFile/QDir exclusively; never pass these paths to narrow-char APIs.
class Paths
{
public:
    // <dataRoot> — defaults to ~/.AgentWorkbench.
    // Priority: setDataRootForTesting() > QStandardPaths test mode >
    // home directory.
    static QString dataRoot();

    // Point dataRoot() at a specific directory (tests inject a
    // QTemporaryDir here). Pass an empty string to return to the default.
    static void setDataRootForTesting(const QString &dir);

    static QString themesDir();      // <dataRoot>/themes
    static QString pluginsDir();     // <dataRoot>/plugins
    static QString logsDir();        // <dataRoot>/log
    static QString webProfilesDir(); // <dataRoot>/webprofiles
    static QString downloadsDir();   // ~/Downloads (configurable later)

    // True once dataRoot() was overridden by setDataRootForTesting().
    static bool isDataRootOverridden();

private:
    static QString s_testRoot;
};

} // namespace awb::core

#endif // AWB_CORE_PATHS_H
