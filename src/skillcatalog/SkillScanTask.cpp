#include "skillcatalog/SkillScanTask.h"

#include "core/EnvExpander.h"
#include "core/Logging.h"
#include "skillcatalog/SkillFrontmatter.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

#include <utility>

namespace awb::skillcatalog {

namespace {

using ScanStats = SkillScanTask::Stats;

/// plugin 去重的版本比较：0.5.1 > 0.4.2 > 0.3.0。
/// 按 '.' 分段，能转数字的按数字比，否则按字符串比。
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

/// 把可能带通配符的路径（plugin 缓存根就是）展开成具体目录。
/// 不含通配符的尾巴原样追加；含通配符的一层列出匹配的子目录再继续。
void expandPattern(const QString &pattern, QStringList &out)
{
    if (!pattern.contains(QLatin1Char('*'))) {
        out.append(pattern);
        return;
    }

    // 在第一个通配符段处切开：列出其父目录的匹配项，剩余尾巴（可能仍带
    // 通配符）递归处理。
    const QStringList segments = pattern.split(QLatin1Char('/'));
    int wildcardIndex = -1;
    for (int i = 0; i < segments.size(); ++i) {
        if (segments.at(i).contains(QLatin1Char('*'))) {
            wildcardIndex = i;
            break;
        }
    }
    if (wildcardIndex < 0) { // 上面已挡住，不可达
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

/// 根路径里第一个通配符之前的固定前缀（用于从缓存布局
/// "<plugin>/<version>" 里剥出 plugin 标识）。
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

/// 占位符变真实路径的唯一入口。settings 存 RAW 路径（"~"、"%PWD%"、
/// 通配符），这样回存时不会把某一台机器的 home/cwd 烧进 settings.json。
SkillRoot effectiveRoot(const SkillRoot &configured)
{
    SkillRoot root = configured;
    root.path.replace(QStringLiteral("%PWD%"),
                      QDir::toNativeSeparators(QDir::currentPath()));
    root.path = core::EnvExpander::expand(root.path);
    return root;
}

/// 一次扫描的累积状态：SkillScanTask::run 的整趟遍历都在它上面追加，
/// 让扫描逻辑可以从 SkillScanner 的成员状态里剥出来整体搬进 worker 线程。
struct ScanState
{
    QList<SkillDefinition> definitions;
};

void scanDirectory(const QString &dirPath, const SkillRoot &root, int depth,
                   ScanStats &stats, const QString &base, int maxDepth,
                   ScanState &state)
{
    const QDir dir(dirPath);
    const QString skillFile = dirPath + QStringLiteral("/SKILL.md");

    if (QFile::exists(skillFile)) {
        // 含 SKILL.md 的目录就是一个 skill —— 不再向下递归。
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

        // plugin 缓存布局：<base>/<marketplace>/<plugin>/<version>/skills/
        // <skill>。"skills" 段之前的最后一段是版本，其余是 plugin id
        // （marketplace/plugin）。
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

        state.definitions.append(skill);
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
                      maxDepth, state);
    }
}

void scanRoot(const SkillRoot &root, const SkillScanParams &params,
              ScanStats &stats, ScanState &state)
{
    // 通配符展开（%PWD%/~ 已在 effectiveRoot 处理）。
    QStringList concrete;
    expandPattern(root.path, concrete);
    // 不含通配符的路径：即使目录不存在也保留——scanRoot 会把它记成跳过。
    if (concrete.isEmpty())
        concrete.append(root.path);
    const QString base = fixedPrefix(root.path);

    int existing = 0;
    for (const QString &entry : concrete) {
        const QDir dir(entry);
        if (!dir.exists()) {
            qWarning().noquote() << QStringLiteral(
                "SkillScanner: root %1 does not exist; skipping it")
                .arg(entry);
            continue;
        }
        ++existing;
        scanDirectory(entry, root, 0, stats, base, params.maxDepth, state);
    }
    if (existing == 0) {
        ++stats.rootsSkipped;
        stats.skippedRoots.append(root.label.isEmpty() ? root.path
                                                       : root.label);
    } else {
        ++stats.rootsScanned;
    }
}

/// 同一 plugin 在缓存里有多个版本：只保留最高版本。
/// 非 plugin 根的 skill 不动；不同根里的同名 skill 也都保留。
void dedupePluginVersions(ScanState &state, ScanStats &stats)
{
    QHash<QString, int> bestIndex; // "<rootId>|<pluginId>|<skill name>"
    QList<SkillDefinition> kept;
    kept.reserve(state.definitions.size());

    for (const SkillDefinition &skill : std::as_const(state.definitions)) {
        if (skill.kind != QLatin1String("plugin")
            || skill.pluginId.isEmpty()) {
            kept.append(skill);
            continue;
        }
        // skill 名在 key 里：一个 plugin 会发多个 skill，只有同一个 skill
        // 才在缓存版本间竞争。
        const QString key = skill.rootId + QLatin1Char('|') + skill.pluginId
                            + QLatin1Char('|') + skill.name;
        const auto it = bestIndex.constFind(key);
        if (it == bestIndex.constEnd()) {
            bestIndex.insert(key, kept.size());
            kept.append(skill);
            continue;
        }
        // 同一 plugin skill：胜者留在原槽位；更低版本的同名条目丢弃
        // （同版本的重复也丢弃——先到者胜）。
        const SkillDefinition &current = kept.at(it.value());
        if (versionLess(current.pluginVersion, skill.pluginVersion)) {
            kept[it.value()] = skill;
            ++stats.duplicatesDropped;
        } else {
            ++stats.duplicatesDropped;
        }
    }
    state.definitions = kept;
}

} // namespace

SkillScanTask::Result SkillScanTask::run(const SkillScanParams &params)
{
    QElapsedTimer timer;
    timer.start();

    Stats stats;
    ScanState state;

    for (const SkillRoot &root : params.roots) {
        if (!root.enabled) {
            ++stats.rootsSkipped;
            continue;
        }
        if (root.kind == QLatin1String("plugin")
            && !params.includePluginCaches) {
            ++stats.rootsSkipped;
            continue;
        }
        // 每根单独计时：慢根（深目录树、plugin 缓存通配展开）一眼可辨。
        QElapsedTimer rootTimer;
        rootTimer.start();
        const int foundBefore = state.definitions.size();
        scanRoot(effectiveRoot(root), params, stats, state);
        AWB_PERF << QStringLiteral("skills: root '%1' took %2 ms, %3 skill(s)")
            .arg(root.id).arg(rootTimer.elapsed())
            .arg(state.definitions.size() - foundBefore);
    }

    dedupePluginVersions(state, stats);
    stats.skillCount = state.definitions.size();
    stats.elapsedMs = timer.elapsed();

    Result result;
    result.definitions = state.definitions;
    result.stats = stats;
    return result;
}

} // namespace awb::skillcatalog
