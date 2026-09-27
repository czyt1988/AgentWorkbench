#include "skills/SkillScanner.h"

#include "core/EnvExpander.h"
#include "core/JsonStore.h"
#include "core/Settings.h"
#include "skills/SkillFrontmatter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

#include <utility>

namespace awb::skills {

namespace {

// Version comparison for the plugin dedup: 0.5.1 > 0.4.2 > 0.3.0.
// Splits on '.', compares numerically where possible, lexically otherwise.
bool versionLess(const QString &a, const QString &b)
{
    const QStringList partsA = a.split(QLatin1Char('.'));
    const QStringList partsB = b.split(QLatin1Char('.'));
    const int count = qMax(partsA.size(), partsB.size());
    for (int i = 0; i < count; ++i) {
        const QString pa = i < partsA.size() ? partsA.at(i) : QString();
        const QString pb = i < partsB.size() ? partsB.at(i) : QString();
        bool numA = false;
        bool numB = false;
        const int na = pa.toInt(&numA);
        const int nb = pb.toInt(&numB);
        if (numA && numB) {
            if (na != nb)
                return na < nb;
        } else if (pa != pb) {
            return pa < pb;
        }
    }
    return false;
}

// Expand a path that may contain wildcards (the plugin cache entry does)
// into concrete directories. Non-wildcard tails are appended verbatim; a
// wildcard level lists the matching subdirectories.
void expandPattern(const QString &pattern, QStringList &out)
{
    if (!pattern.contains(QLatin1Char('*'))) {
        out.append(pattern);
        return;
    }

    // Split at the first wildcard segment; list its parent's matches and
    // continue with the remaining (possibly wildcard) tail.
    const QStringList segments = pattern.split(QLatin1Char('/'));
    int wildcardIndex = -1;
    for (int i = 0; i < segments.size(); ++i) {
        if (segments.at(i).contains(QLatin1Char('*'))) {
            wildcardIndex = i;
            break;
        }
    }
    if (wildcardIndex < 0) { // unreachable, guarded above
        out.append(pattern);
        return;
    }

    QString prefix;
    for (int i = 0; i < wildcardIndex; ++i) {
        if (!prefix.isEmpty())
            prefix += QLatin1Char('/');
        prefix += segments.at(i);
    }
    const QString filter = segments.at(wildcardIndex);
    QString rest;
    for (int i = wildcardIndex + 1; i < segments.size(); ++i) {
        if (!rest.isEmpty())
            rest += QLatin1Char('/');
        rest += segments.at(i);
    }

    const QDir dir(prefix.isEmpty() ? QStringLiteral(".") : prefix);
    if (!dir.exists())
        return;
    const QFileInfoList entries = dir.entryInfoList(
        {filter}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (rest.isEmpty())
            out.append(info.absoluteFilePath());
        else
            expandPattern(info.absoluteFilePath() + QLatin1Char('/') + rest,
                          out);
    }
}

// Fixed prefix before the first wildcard of a root path (used to derive
// "<plugin>/<version>" from the cache layout).
QString fixedPrefix(const QString &path)
{
    const QStringList segments = path.split(QLatin1Char('/'));
    QString prefix;
    for (const QString &segment : segments) {
        if (segment.contains(QLatin1Char('*')))
            break;
        if (!prefix.isEmpty())
            prefix += QLatin1Char('/');
        prefix += segment;
    }
    return prefix;
}

} // namespace

SkillScanner::SkillScanner(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QList<SkillRoot> SkillScanner::roots() const
{
    return m_roots.isEmpty() ? SkillRoots::defaults() : m_roots;
}

void SkillScanner::setRootEnabled(const QString &id, bool enabled)
{
    // Persist the full effective list so toggles survive a restart even
    // when the user never customized the roots (a non-empty
    // skills.roots completely replaces the defaults).
    const QList<SkillRoot> current =
        m_roots.isEmpty() ? SkillRoots::defaults() : m_roots;
    QJsonArray array;
    for (SkillRoot root : current) {
        if (root.id == id)
            root.enabled = enabled;
        QJsonObject o;
        o[QStringLiteral("id")] = root.id;
        o[QStringLiteral("label")] = root.label;
        o[QStringLiteral("path")] = root.path;
        o[QStringLiteral("kind")] = root.kind;
        o[QStringLiteral("enabled")] = root.enabled;
        array.append(o);
    }
    m_settings->setSkillRoots(array);
    m_settings->save();
    m_roots = SkillRoots::fromJson(array);
}

void SkillScanner::refresh()
{
    emit scanStarted();
    QElapsedTimer timer;
    timer.start();

    Stats stats;
    m_definitions.clear();

    const auto &options = m_settings->skillsOptions();
    const QList<SkillRoot> configured = options.roots.isEmpty()
        ? SkillRoots::defaults()
        : SkillRoots::fromJson(options.roots);
    m_roots = configured;

    for (const SkillRoot &root : configured) {
        if (!root.enabled) {
            ++stats.rootsSkipped;
            continue;
        }
        if (root.kind == QLatin1String("plugin")
            && !options.includePluginCaches) {
            ++stats.rootsSkipped;
            continue;
        }
        scanRoot(effectiveRoot(root), stats);
    }

    dedupePluginVersions(stats);
    stats.skillCount = m_definitions.size();
    stats.elapsedMs = timer.elapsed();
    m_lastStats = stats;
    qInfo().noquote() << QStringLiteral(
        "SkillScanner: found %1 skill(s) in %2 root(s), %3 skipped, "
        "%4 duplicate(s) dropped (%5 ms)")
        .arg(stats.skillCount)
        .arg(stats.rootsScanned)
        .arg(stats.rootsSkipped)
        .arg(stats.duplicatesDropped)
        .arg(stats.elapsedMs);
    emit scanFinished();
}

SkillRoot SkillScanner::effectiveRoot(const SkillRoot &configured) const
{
    // The one place placeholders become real paths: settings store paths
    // RAW ("~", "%PWD%", wildcards) so persisting them back never bakes in
    // an expansion. Wildcards are expanded further downstream
    // by expandWildcards().
    SkillRoot root = configured;
    root.path.replace(QStringLiteral("%PWD%"),
                       QDir::toNativeSeparators(QDir::currentPath()));
    root.path = core::EnvExpander::expand(root.path);
    return root;
}

QList<SkillRoot> SkillScanner::expandWildcards(const SkillRoot &root) const
{
    QStringList concrete;
    expandPattern(root.path, concrete);
    // A pattern without wildcards: keep it even when missing — scanRoot
    // reports it as skipped.
    if (concrete.isEmpty())
        concrete.append(root.path);
    QList<SkillRoot> result;
    for (const QString &path : concrete) {
        SkillRoot copy = root;
        copy.path = path;
        result.append(copy);
    }
    return result;
}

void SkillScanner::scanRoot(const SkillRoot &root, Stats &stats)
{
    const QList<SkillRoot> concrete = expandWildcards(root);
    const QString base = fixedPrefix(root.path);

    int existing = 0;
    for (const SkillRoot &entry : concrete) {
        const QDir dir(entry.path);
        if (!dir.exists()) {
            qWarning().noquote() << QStringLiteral(
                "SkillScanner: root %1 does not exist; skipping it")
                .arg(entry.path);
            continue;
        }
        ++existing;
        const int maxDepth = m_settings->skillsOptions().maxDepth;
        scanDirectory(entry.path, entry, 0, stats, base, maxDepth);
    }
    if (existing == 0) {
        ++stats.rootsSkipped;
        stats.skippedRoots.append(root.label.isEmpty() ? root.path
                                                       : root.label);
    } else {
        ++stats.rootsScanned;
    }
}

void SkillScanner::scanDirectory(const QString &dirPath, const SkillRoot &root,
                                 int depth, Stats &stats, const QString &base,
                                 int maxDepth)
{
    const QDir dir(dirPath);
    const QString skillFile = dirPath + QStringLiteral("/SKILL.md");

    if (QFile::exists(skillFile)) {
        // A directory with SKILL.md is a skill — do not descend further.
        QFile file(skillFile);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning().noquote() << QStringLiteral(
                "SkillScanner: cannot read %1: %2").arg(skillFile,
                                                        file.errorString());
            return;
        }
        const SkillFrontmatter frontmatter =
            SkillFrontmatterParser::parse(file.readAll());
        file.close();

        SkillDefinition skill;
        skill.name = frontmatter.valid && !frontmatter.name.isEmpty()
            ? frontmatter.name : dir.dirName();
        skill.description = frontmatter.description;
        skill.skillFilePath = skillFile;
        skill.dirPath = dirPath;
        skill.rootId = root.id;
        skill.rootLabel = root.label;
        skill.kind = root.kind;
        if (frontmatter.valid) {
            for (auto it = frontmatter.extras.constBegin();
                 it != frontmatter.extras.constEnd(); ++it)
                skill.extras.insert(it.key(), it.value());
        }

        const QFileInfo skillInfo(skillFile);
        skill.lastModified = skillInfo.lastModified();
        skill.sizeBytes = skillInfo.size();

        // Plugin cache layout: <base>/<marketplace>/<plugin>/<version>/
        // skills/<skill> (row 4). Everything before the "skills"
        // segment identifies the source; the last of those segments is the
        // version, the rest the plugin id (marketplace/plugin).
        if (root.kind == QLatin1String("plugin") && !base.isEmpty()
            && dirPath.startsWith(base)) {
            QString relative = dirPath.mid(base.length());
            while (relative.startsWith(QLatin1Char('/')))
                relative.remove(0, 1);
            const QStringList segments = relative.split(QLatin1Char('/'));
            const int skillsIndex = segments.indexOf(QStringLiteral("skills"));
            if (skillsIndex >= 2) {
                skill.pluginVersion = segments.at(skillsIndex - 1);
                skill.pluginId =
                    segments.mid(0, skillsIndex - 1).join(QLatin1Char('/'));
            } else if (skillsIndex == 1) {
                skill.pluginId = segments.at(0);
            }
        }

        m_definitions.append(skill);
        return;
    }

    if (depth >= maxDepth)
        return;

    const QFileInfoList children = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QFileInfo &child : children) {
        if (!child.isReadable()) {
            qWarning().noquote() << QStringLiteral(
                "SkillScanner: cannot read %1; continuing").arg(
                child.absoluteFilePath());
            continue;
        }
        scanDirectory(child.absoluteFilePath(), root, depth + 1, stats, base,
                      maxDepth);
    }
}

// Same plugin, several versions in the cache: keep the highest only.
// Skills from non-plugin roots are untouched, and
// same-named skills from different roots all stay (dedup rule).
void SkillScanner::dedupePluginVersions(Stats &stats)
{
    QHash<QString, int> bestIndex; // "<rootId>|<pluginId>|<skill name>"
    QList<SkillDefinition> kept;
    kept.reserve(m_definitions.size());

    for (const SkillDefinition &skill : std::as_const(m_definitions)) {
        if (skill.kind != QLatin1String("plugin")
            || skill.pluginId.isEmpty()) {
            kept.append(skill);
            continue;
        }
        // The skill name is part of the key: one plugin ships several
        // skills, and only the SAME skill competes across cached versions.
        const QString key = skill.rootId + QLatin1Char('|') + skill.pluginId
                            + QLatin1Char('|') + skill.name;
        const auto it = bestIndex.constFind(key);
        if (it == bestIndex.constEnd()) {
            bestIndex.insert(key, kept.size());
            kept.append(skill);
            continue;
        }
        // Same plugin skill: the winner stays at its slot; every later
        // duplicate of a LOWER version is dropped (any duplicate of the
        // same version is dropped too — the first one wins).
        const SkillDefinition &current = kept.at(it.value());
        if (versionLess(current.pluginVersion, skill.pluginVersion)) {
            kept[it.value()] = skill;
            ++stats.duplicatesDropped;
        } else {
            ++stats.duplicatesDropped;
        }
    }
    m_definitions = kept;
}

} // namespace awb::skills
