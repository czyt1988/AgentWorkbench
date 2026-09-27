#ifndef AWB_CORE_ICONRESOLVER_H
#define AWB_CORE_ICONRESOLVER_H

#include <QString>

namespace awb::core {

// Resolve an icon string from configuration into a displayable image URL.
// qrc:/, http(s):// and file:// pass through;
// an existing local file becomes a file:/// URL; anything else (including
// empty) falls back to `fallback`.
//
// The fallback is supplied by the caller — core must not hardcode an
// application resource path.
class IconResolver
{
public:
    static QString resolve(const QString &raw, const QString &fallback);
};

} // namespace awb::core

#endif // AWB_CORE_ICONRESOLVER_H
