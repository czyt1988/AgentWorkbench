#include "core/IconResolver.h"

#include "core/EnvExpander.h"

#include <QFileInfo>
#include <QUrl>

namespace awb::core {

QString IconResolver::resolve(const QString &raw, const QString &fallback)
{
    if (raw.isEmpty())
        return fallback;

    // Built-in resources, remote URLs and file URLs are used as-is.
    if (raw.startsWith(QStringLiteral("qrc:/"))
        || raw.startsWith(QStringLiteral("http://"))
        || raw.startsWith(QStringLiteral("https://"))
        || raw.startsWith(QStringLiteral("file://")))
        return raw;

    // Treat anything else as a local file path. Expand environment variables
    // and ~ so users can write e.g. "%USERPROFILE%/icons/my-agent.svg".
    const QString expanded = EnvExpander::expand(raw);
    const QFileInfo fi(expanded);
    if (fi.exists())
        return QUrl::fromLocalFile(fi.absoluteFilePath()).toString();

    // File not found — fall back rather than showing nothing.
    return fallback;
}

} // namespace awb::core
