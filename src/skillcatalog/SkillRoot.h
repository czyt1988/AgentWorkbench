#ifndef AWB_SKILLS_SKILLROOT_H
#define AWB_SKILLS_SKILLROOT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace awb::skillcatalog {

// One scan root. `path` may contain wildcards
// (the ZCode plugin cache entry does) — the scanner expands them.
struct SkillRoot
{
    QString id;
    QString label;
    QString path;
    QString kind = QStringLiteral("custom"); // agents|claude|codex|plugin|project|custom
    bool enabled = true;
    bool recursive = true;
    QString dedupeScope; // plugin roots: marketplace scope for version dedup

    bool isValid() const { return !id.isEmpty() && !path.isEmpty(); }
};

// The default root list and conversion of
// `skills.roots` entries from settings.json.
class SkillRoots
{
public:
    // The platform defaults; `withProjectRoots` adds the cwd-based project
    // skills directories when a working directory is known.
    static QList<SkillRoot> defaults();

    // Parse settings.json `skills.roots` entries ({path, kind?, id?,
    // enabled?}); an empty array means "use the defaults".
    static QList<SkillRoot> fromJson(const QJsonArray &entries);
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLROOT_H
