#include "core/Paths.h"

#include <QDir>
#include <QStandardPaths>

namespace awb::core {

QString Paths::s_testRoot;

void Paths::setDataRootForTesting(const QString &dir)
{
    s_testRoot = dir;
}

bool Paths::isDataRootOverridden()
{
    return !s_testRoot.isEmpty();
}

QString Paths::dataRoot()
{
    if (!s_testRoot.isEmpty())
        return s_testRoot;

    // QStandardPaths test mode only redirects the App* locations (see
    // QStandardPaths::setTestModeEnabled), never HomeLocation, so unit tests
    // would otherwise read and rewrite the developer's real config. Keep them
    // on the redirected location.
    if (QStandardPaths::isTestModeEnabled())
        return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
           + QStringLiteral("/.AgentWorkbench");
}

QString Paths::themesDir()
{
    return dataRoot() + QStringLiteral("/themes");
}

QString Paths::pluginsDir()
{
    return dataRoot() + QStringLiteral("/plugins");
}

QString Paths::logsDir()
{
    return dataRoot() + QStringLiteral("/log");
}

QString Paths::webProfilesDir()
{
    return dataRoot() + QStringLiteral("/webprofiles");
}

QString Paths::skillCacheFile()
{
    return dataRoot() + QStringLiteral("/skills_cache.json");
}

QString Paths::downloadsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

} // namespace awb::core
