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

/**
 * @brief plugin 去重用的版本比较
 *
 * 语义示例：0.5.1 > 0.4.2 > 0.3.0。按 '.' 分段逐段比，能转数字的
 * 按数字比，否则按字符串比；缺段按空串处理。
 *
 * @param a 版本串甲
 * @param b 版本串乙
 * @return a 严格小于 b 时返回 true
 */
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
            if (na != nb) {
                return na < nb;
            }
        } else if (pa != pb) {
            return pa < pb;
        }
    }
    return false;
}

/**
 * @brief 把可能带通配符的路径展开成具体目录
 *
 * plugin 缓存根就是典型输入（cache 下 marketplace/插件/版本/skills
 * 的多层通配布局）。不含通配符的路径原样追加；含通配符的在第一个
 * 通配符段处切开，列出其父目录下匹配的子目录，剩余尾巴（可能仍带
 * 通配符）递归展开。
 *
 * @param pattern 原始路径（占位符已由 effectiveRoot 展开）
 * @param out 展开结果就地追加；目录不存在或无匹配时可能一条都不加
 */
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
        if (!prefix.isEmpty()) {
            prefix += QLatin1Char('/');
        }
        prefix += segments.at(i);
    }
    const QString filter = segments.at(wildcardIndex);
    QString rest;
    for (int i = wildcardIndex + 1; i < segments.size(); ++i) {
        if (!rest.isEmpty()) {
            rest += QLatin1Char('/');
        }
        rest += segments.at(i);
    }

    const QDir dir(prefix.isEmpty() ? QStringLiteral(".") : prefix);
    if (!dir.exists()) {
        return;
    }
    const QFileInfoList entries = dir.entryInfoList(
        {filter}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (rest.isEmpty()) {
            out.append(info.absoluteFilePath());
        }
        else {
            expandPattern(info.absoluteFilePath() + QLatin1Char('/') + rest,
                          out);
        }
    }
}

/**
 * @brief 取根路径里第一个通配符之前的固定前缀
 *
 * 用于从缓存布局 `<marketplace>/<plugin>/<version>/skills` 里剥出
 * plugin 标识：扫描到的目录都在该前缀之下，相对前缀的路径段即
 * marketplace/plugin/version。
 *
 * @param path 原始根路径（可含通配符）
 * @return 通配符前的固定部分；没有通配符时就是整个路径
 */
QString fixedPrefix(const QString &path)
{
    const QStringList segments = path.split(QLatin1Char('/'));
    QString prefix;
    for (const QString &segment : segments) {
        if (segment.contains(QLatin1Char('*'))) {
            break;
        }
        if (!prefix.isEmpty()) {
            prefix += QLatin1Char('/');
        }
        prefix += segment;
    }
    return prefix;
}

/**
 * @brief 占位符变真实路径的唯一入口
 *
 * settings 存 RAW 路径（`~`、`%PWD%`、通配符），这样回存时不会把某台
 * 机器的 home/cwd 烧进 settings.json；本函数只用于扫描时的临时展开。
 * 通配符不在此时处理（留给 expandPattern）。
 *
 * @param configured 配置形态的根
 * @return path 已展开 `~` 与 `%PWD%` 的副本；其余字段原样
 */
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
    QList<SkillDefinition> definitions;  ///< 扫描到的全部 skill（去重前）
};

/**
 * @brief 深度优先扫描一个目录：找 SKILL.md、解析并登记 skill
 *
 * 一个含 SKILL.md 的目录就是一个 skill，不再向下递归；没有的目录按
 * maxDepth 上限继续下探（含隐藏目录）。plugin 根的条目同时从相对
 * base 的路径段里剥出 pluginId/pluginVersion。
 *
 * @param dirPath 当前目录的绝对路径
 * @param root 所属扫描根（id/label/kind 写进定义）
 * @param depth 当前递归深度（起始为 0）
 * @param stats 统计就地累积（本函数不写它，仅保持签名统一）
 * @param base plugin 根的固定前缀；非 plugin 根传空串
 * @param maxDepth 目录遍历深度上限
 * @param state 扫描累积状态，发现的 skill 就地追加
 */
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
                 it != frontmatter.extras.constEnd(); ++it) {
                skill.extras.insert(it.key(), it.value());
            }
        }

        const QFileInfo skillInfo(skillFile);
        skill.lastModified = skillInfo.lastModified();
        skill.sizeBytes = skillInfo.size();

        // plugin 缓存布局：<base>/<marketplace>/<plugin>/<version>/skills/
        // <skill>。"skills" 段之前的最后一段是版本，其余是 plugin id
        // （marketplace/plugin）。
        if (root.kind == QStringLiteral("plugin") && !base.isEmpty()
            && dirPath.startsWith(base)) {
            QString relative = dirPath.mid(base.length());
            while (relative.startsWith(QLatin1Char('/'))) {
                relative.remove(0, 1);
            }
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

    if (depth >= maxDepth) {
        return;
    }

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

/**
 * @brief 扫描单个根：展开通配符后逐个实体目录扫描
 *
 * 所有实体目录都不存在时整根记为跳过（skippedRoots 带 label）；
 * 只要有一个存在就记为已扫描。
 *
 * @param root 已展开占位符的根（见 effectiveRoot）
 * @param params 扫描参数（maxDepth 从这里取）
 * @param stats 统计就地累积
 * @param state 扫描累积状态
 */
void scanRoot(const SkillRoot &root, const SkillScanParams &params,
              ScanStats &stats, ScanState &state)
{
    // 通配符展开（%PWD%/~ 已在 effectiveRoot 处理）。
    QStringList concrete;
    expandPattern(root.path, concrete);
    // 不含通配符的路径：即使目录不存在也保留——scanRoot 会把它记成跳过。
    if (concrete.isEmpty()) {
        concrete.append(root.path);
    }
    const QString base = fixedPrefix(root.path);

    int existing = 0;
    for (const QString &entry : std::as_const(concrete)) {
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

/**
 * @brief 同一 plugin 的缓存多版本去重：每个 skill 只保留最高版本
 *
 * 非 plugin 根或没有 pluginId 的条目原样保留；不同根、不同 plugin、
 * 不同 skill 名之间互不竞争。同版本的重复也丢弃（先到者胜）。
 *
 * @param state 扫描累积状态，definitions 就地去重
 * @param stats duplicatesDropped 就地累积
 */
void dedupePluginVersions(ScanState &state, ScanStats &stats)
{
    QHash<QString, int> bestIndex; // "<rootId>|<pluginId>|<skill name>"
    QList<SkillDefinition> kept;
    kept.reserve(state.definitions.size());

    for (const SkillDefinition &skill : std::as_const(state.definitions)) {
        if (skill.kind != QStringLiteral("plugin")
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

/**
 * @brief 同步执行整次扫描
 *
 * 遍历启用且未被过滤的根（禁用或 plugin 根被排除的记跳过），扫描、
 * 去重、统计耗时后一次性返回。线程安全：只读 params、只写局部状态
 * 与返回值，不触碰任何 GUI 对象。
 *
 * @param params 扫描输入（根清单、深度上限、是否计入 plugin 缓存）
 * @return 定义列表 + 统计
 */
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
        if (root.kind == QStringLiteral("plugin")
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
