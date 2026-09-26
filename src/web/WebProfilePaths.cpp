#include "web/WebProfilePaths.h"

#include "core/Paths.h"

#include <QDir>
#include <QRegularExpression>

namespace awb::web {

QString WebProfilePaths::profileDir(const QString &agentId)
{
    // Agent ids are slugs ([a-z0-9-]), but sanitize anyway so a hand-edited
    // config can never escape the profiles directory.
    QString safe = agentId;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                 QStringLiteral("_"));
    const QString dir = core::Paths::webProfilesDir()
                        + QLatin1Char('/') + safe;
    QDir().mkpath(dir);
    return dir;
}

QString WebProfilePaths::storageName(const QString &agentId)
{
    return QStringLiteral("awb-") + agentId;
}

} // namespace awb::web
