#ifndef AWB_CORE_ENVEXPANDER_H
#define AWB_CORE_ENVEXPANDER_H

#include <QString>

namespace awb::core {

// Path expansion shared by every module: %VAR% (Windows style) environment
// variables and a leading ~ for the home directory (01-architecture.md §4.1).
class EnvExpander
{
public:
    // Expand %VAR% occurrences and "~/". Unknown variables are left as-is.
    static QString expand(const QString &path);
};

} // namespace awb::core

#endif // AWB_CORE_ENVEXPANDER_H
