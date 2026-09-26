#ifndef AWB_THEME_THEMELOADER_H
#define AWB_THEME_THEMELOADER_H

#include "theme/ThemeFile.h"

#include <QJsonObject>
#include <QString>

namespace awb::theme {

// Parse + validate one theme JSON file (01-architecture.md §4.2):
//   - unknown keys            -> WARN and ignore
//   - missing token           -> take the value from `baseline` (the
//                                built-in theme of the SAME variant)
//   - invalid color           -> WARN and use the baseline value
//   - `id` missing or != file name -> skip the whole file (WARN)
class ThemeLoader
{
public:
    // Parse a theme JSON object. `fileName` is the bare file name without
    // the .json suffix (it must equal the theme's `id`). `baseline` supplies
    // fallback values; pass an invalid ThemeFile for the built-ins
    // themselves (they are complete by construction). Returns false when
    // the file must be skipped; `out` is then invalid.
    static bool parse(const QJsonObject &json, const QString &fileName,
                      const ThemeFile &baseline, ThemeFile &out);

    // Read + parse a file from disk or a :/ resource. On any failure the
    // returned ThemeFile is invalid.
    static ThemeFile loadFile(const QString &path, const ThemeFile &baseline);
};

} // namespace awb::theme

#endif // AWB_THEME_THEMELOADER_H
