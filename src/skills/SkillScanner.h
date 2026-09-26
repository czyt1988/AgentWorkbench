#ifndef AWB_SKILLS_SKILLSCANNER_H
#define AWB_SKILLS_SKILLSCANNER_H

#include "skills/SkillDefinition.h"
#include "skills/SkillRoot.h"

#include <QHash>
#include <QObject>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::skills {

// Walks the configured roots and finds skills: any directory containing a
// SKILL.md counts as one skill and is not descended into further
// (01-architecture.md §4.4).
//
// Rules (specs/03 S6-T3):
//  - a root that is missing or unreadable is skipped with a warning — one
//    bad root never fails the scan;
//  - the plugin cache holds several versions of the same plugin; only the
//    highest version of the same marketplace+plugin survives;
//  - refresh() returns immediately; the (still synchronous) result arrives
//    through scanFinished(Stats) — the async-shaped interface means the
//    scan can move to a worker thread later without touching callers.
class SkillScanner : public QObject
{
    Q_OBJECT

public:
    struct Stats
    {
        int skillCount = 0;
        int rootsScanned = 0;
        int rootsSkipped = 0;   // missing/unreadable/filtered out
        int duplicatesDropped = 0;
        qint64 elapsedMs = 0;
        QStringList skippedRoots; // labels, for the page's warning line
    };

    explicit SkillScanner(core::Settings *settings,
                          QObject *parent = nullptr);

    // Roots as configured (settings override or defaults).
    QList<SkillRoot> roots() const;
    // Persist an enabled/disabled flag for one root (into skills.roots).
    void setRootEnabled(const QString &id, bool enabled);

    // Kick a scan. Returns immediately (currently synchronous inside).
    Q_INVOKABLE void refresh();

    // The definitions produced by the last scan.
    const QList<SkillDefinition> &definitions() const { return m_definitions; }

    // Stats of the last scan (emptyStats until the first refresh()).
    Stats lastStats() const { return m_lastStats; }

signals:
    void scanFinished();
    void scanStarted();

private:
    SkillRoot effectiveRoot(const SkillRoot &configured) const;
    QList<SkillRoot> expandWildcards(const SkillRoot &root) const;
    void scanRoot(const SkillRoot &root, Stats &stats);
    void scanDirectory(const QString &dirPath, const SkillRoot &root,
                       int depth, Stats &stats, const QString &base,
                       int maxDepth);
    void dedupePluginVersions(Stats &stats);

    core::Settings *m_settings;
    QList<SkillRoot> m_roots;
    QList<SkillDefinition> m_definitions;
    Stats m_lastStats;
    int m_nextLocalId = 1;
};

} // namespace awb::skills

#endif // AWB_SKILLS_SKILLSCANNER_H
