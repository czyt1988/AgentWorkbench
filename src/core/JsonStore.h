#ifndef AWB_CORE_JSONSTORE_H
#define AWB_CORE_JSONSTORE_H

#include "core/OpResult.h"

#include <QJsonObject>
#include <QString>

namespace awb::core {

// JSON file I/O for every configuration file the application writes.
// Atomic saves via QSaveFile, consistent
// indentation, and read failures that degrade to an empty object plus a
// log line instead of taking the app down.
class JsonStore
{
public:
    // Read `path`. A missing file yields an empty object without a warning;
    // an unreadable or malformed file logs and also yields an empty object.
    static QJsonObject readFile(const QString &path);

    // Atomically write `object` as indented JSON. Creates parent
    // directories as needed.
    static OpResult writeFile(const QString &path, const QJsonObject &object);

    // Atomically write raw bytes (for files that must stay byte-identical
    // to a bundled original).
    static OpResult writeBytes(const QString &path, const QByteArray &bytes);
};

} // namespace awb::core

#endif // AWB_CORE_JSONSTORE_H
