#include "skillcatalog/SkillCache.h"

#include "core/JsonStore.h"
#include "core/Paths.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace awb::skillcatalog {

namespace {

// 缓存格式版本：load() 只认这个值。改结构（增删字段、改语义）时递增并
// 让旧文件按「过期」处理——旧缓存当没有用即可，启动扫描会立刻重建，
// 所以不做任何跨版本迁移（与 settings 的无迁移原则一致）。
constexpr int kFormatVersion = 1;

/// SkillDefinition → JSON 对象。字段与 SkillDefinition 一一对应；
/// lastModified 用 ISO 字符串保住时区信息（msecs 数字会丢本地时区）。
QJsonObject toJson(const SkillDefinition &skill)
{
    QJsonObject o;
    o[QStringLiteral("name")] = skill.name;
    o[QStringLiteral("description")] = skill.description;
    o[QStringLiteral("skillFilePath")] = skill.skillFilePath;
    o[QStringLiteral("dirPath")] = skill.dirPath;
    o[QStringLiteral("rootId")] = skill.rootId;
    o[QStringLiteral("rootLabel")] = skill.rootLabel;
    o[QStringLiteral("kind")] = skill.kind;
    o[QStringLiteral("pluginId")] = skill.pluginId;
    o[QStringLiteral("pluginVersion")] = skill.pluginVersion;
    if (skill.lastModified.isValid()) {
        o[QStringLiteral("lastModified")] =
            skill.lastModified.toString(Qt::ISODateWithMs);
    }
    o[QStringLiteral("sizeBytes")] = static_cast<double>(skill.sizeBytes);
    if (!skill.extras.isEmpty()) {
        o[QStringLiteral("extras")] = QJsonObject::fromVariantMap(skill.extras);
    }
    return o;
}

/// JSON 对象 → SkillDefinition。缺字段按默认值；extras 只收字符串值
/// （扫描产物本来全是字符串，非字符串值意味着文件被手工改过，丢弃）。
SkillDefinition fromJson(const QJsonObject &o)
{
    SkillDefinition skill;
    skill.name = o.value(QStringLiteral("name")).toString();
    skill.description = o.value(QStringLiteral("description")).toString();
    skill.skillFilePath = o.value(QStringLiteral("skillFilePath")).toString();
    skill.dirPath = o.value(QStringLiteral("dirPath")).toString();
    skill.rootId = o.value(QStringLiteral("rootId")).toString();
    skill.rootLabel = o.value(QStringLiteral("rootLabel")).toString();
    skill.kind = o.value(QStringLiteral("kind")).toString();
    skill.pluginId = o.value(QStringLiteral("pluginId")).toString();
    skill.pluginVersion =
        o.value(QStringLiteral("pluginVersion")).toString();
    const QString modified =
        o.value(QStringLiteral("lastModified")).toString();
    if (!modified.isEmpty()) {
        skill.lastModified =
            QDateTime::fromString(modified, Qt::ISODateWithMs);
    }
    skill.sizeBytes = static_cast<qint64>(
        o.value(QStringLiteral("sizeBytes")).toDouble());
    const QJsonObject extras = o.value(QStringLiteral("extras")).toObject();
    for (auto it = extras.begin(); it != extras.end(); ++it) {
        if (it.value().isString()) {
            skill.extras.insert(it.key(), it.value().toString());
        }
    }
    return skill;
}

SkillScanTask::Stats statsFromJson(const QJsonObject &o)
{
    SkillScanTask::Stats stats;
    stats.skillCount = o.value(QStringLiteral("skillCount")).toInt();
    stats.rootsScanned = o.value(QStringLiteral("rootsScanned")).toInt();
    stats.rootsSkipped = o.value(QStringLiteral("rootsSkipped")).toInt();
    stats.duplicatesDropped =
        o.value(QStringLiteral("duplicatesDropped")).toInt();
    stats.elapsedMs = static_cast<qint64>(
        o.value(QStringLiteral("elapsedMs")).toDouble());
    const QJsonArray skipped = o.value(QStringLiteral("skippedRoots")).toArray();
    for (const QJsonValue &value : skipped) {
        stats.skippedRoots.append(value.toString());
    }
    return stats;
}

QJsonObject toJson(const SkillScanTask::Stats &stats)
{
    QJsonObject o;
    o[QStringLiteral("skillCount")] = stats.skillCount;
    o[QStringLiteral("rootsScanned")] = stats.rootsScanned;
    o[QStringLiteral("rootsSkipped")] = stats.rootsSkipped;
    o[QStringLiteral("duplicatesDropped")] = stats.duplicatesDropped;
    o[QStringLiteral("elapsedMs")] = static_cast<double>(stats.elapsedMs);
    if (!stats.skippedRoots.isEmpty()) {
        QJsonArray skipped;
        for (const QString &label : stats.skippedRoots) {
            skipped.append(label);
        }
        o[QStringLiteral("skippedRoots")] = skipped;
    }
    return o;
}

} // namespace

QString SkillCache::filePath()
{
    return core::Paths::skillCacheFile();
}

SkillCache::Snapshot SkillCache::load()
{
    const QJsonObject root = core::JsonStore::readFile(filePath());
    Snapshot snapshot;
    if (root.isEmpty()) {
        return snapshot;
    }

    if (root.value(QStringLiteral("version")).toInt()
            != kFormatVersion) {
        // 过期格式按「没有缓存」处理：启动扫描马上重建，不告警。
        return snapshot;
    }
    snapshot.cachedAt = QDateTime::fromString(
        root.value(QStringLiteral("cachedAt")).toString(),
        Qt::ISODateWithMs);
    if (!snapshot.cachedAt.isValid()) {
        return snapshot;
    }

    snapshot.stats = statsFromJson(
        root.value(QStringLiteral("stats")).toObject());
    const QJsonArray definitions =
        root.value(QStringLiteral("definitions")).toArray();
    for (const QJsonValue &value : definitions) {
        if (value.isObject()) {
            snapshot.definitions.append(fromJson(value.toObject()));
        }
    }
    return snapshot;
}

core::OpResult SkillCache::save(const QList<SkillDefinition> &definitions,
                                const SkillScanTask::Stats &stats)
{
    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("cachedAt")] =
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    root[QStringLiteral("stats")] = toJson(stats);
    QJsonArray array;
    for (const SkillDefinition &skill : definitions) {
        array.append(toJson(skill));
    }
    root[QStringLiteral("definitions")] = array;
    return core::JsonStore::writeFile(filePath(), root);
}

} // namespace awb::skillcatalog
