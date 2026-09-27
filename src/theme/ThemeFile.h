#ifndef AWB_THEME_THEMEFILE_H
#define AWB_THEME_THEMEFILE_H

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

namespace awb::theme {

// One parsed theme JSON file. Token names are the contract the QML pages
// rely on; values may differ per theme.
struct ThemeFile
{
    QString id;      // empty = invalid / unusable file
    QString name;
    QString variant; // "dark" | "light"

    QHash<QString, QColor> colors;
    QHash<QString, double> metrics;
    QHash<QString, QString> fonts;   // keys: family, monoFamily
    QStringList agentPalette;        // #rrggbb strings, auto-assignment

    bool isValid() const { return !id.isEmpty(); }
};

} // namespace awb::theme

#endif // AWB_THEME_THEMEFILE_H
