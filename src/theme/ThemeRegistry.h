#ifndef AWB_THEME_THEMEREGISTRY_H
#define AWB_THEME_THEMEREGISTRY_H

#include "theme/ThemeFile.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>

namespace awb::theme {

// Available themes: built-ins from :/themes/*.json plus user themes from
// <dataRoot>/themes/*.json, where a user file with the same id overrides
// the built-in. Watches the user directory and
// its files so a saved theme file reloads live.
class ThemeRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ThemeRegistry(QObject *parent = nullptr);

    // All themes in a stable order: built-ins first, then user themes.
    QList<ThemeFile> themes() const;

    // The theme with this id (user override applied); invalid when unknown.
    ThemeFile theme(const QString &id) const;

    // The built-in baseline theme of a variant ("dark" -> mocha-dark,
    // "light" -> latte-light). Invalid for an unknown variant.
    ThemeFile baseline(const QString &variant) const;

    // Rescan built-ins + the user directory (also re-arms the watchers).
    void refresh();

signals:
    // A theme file changed on disk (or was added/removed).
    void changed();

private:
    void scan();
    void armWatchers();

    // Built-in themes by id (the completeness schema for user themes).
    QHash<QString, ThemeFile> m_builtins;
    // Effective themes: builtins overridden/extended by user files.
    QHash<QString, ThemeFile> m_themes;
    // Where each effective theme came from (file path), for watching.
    QHash<QString, QString> m_sources;

    QFileSystemWatcher m_watcher;
};

} // namespace awb::theme

#endif // AWB_THEME_THEMEREGISTRY_H
