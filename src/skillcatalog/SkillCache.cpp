#include "skillcatalog/SkillCache.h"

#include "core/JsonStore.h"
#include "core/Paths.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace awb::skillcatalog {

namespace {

/**
 * @brief 缓存格式版本
 *
 * load() 只认这个值。改结构（增删字段、改语义）时递增并让旧文件按
 * 「过期」处理——旧缓存当没有用即可，启动扫描会立刻重建，所以不做
 * 任何跨版本迁移（与 settings 的无迁移原则一致）。
 */
constexpr int kFormatVersion = 1;

/**
 * @brief 把 SkillDefinition 序列化成 JSON 对象
 *
 * 字段与 SkillDefinition 一一对应；lastModified 用 ISO 字符串保住时区
 * 信息（msecs 数字会丢本地时区）。
 *
 * @param skill 待序列化的 skill 定义
 * @return 可放入 definitions 数组的 JSON 对象
 */
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

/**
 * @brief 把 JSON 对象还原成 SkillDefinition
 *
 * 缺字段按默认值；extras 只收字符串值（扫描产物本来全是字符串，
 * 非字符串值意味着文件被手工改过，丢弃）。
 *
 * @param o 缓存 definitions 数组里的单个对象
 * @return 还原后的 skill 定义
 */
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

/**
 * @brief 把 JSON 对象还原成扫描统计
 *
 * 缺字段按类型的默认值（int 为 0）；skippedRoots 数组逐项收进字符串列表。
 *
 * @param o 缓存根对象里的 stats 节点
 * @return 还原后的统计
 */
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

/**
 * @brief 把扫描统计序列化成 JSON 对象
 *
 * skippedRoots 为空时不写该字段，保持缓存文件精简。
 *
 * @param stats 待序列化的扫描统计
 * @return 可写进缓存根对象的 JSON 对象
 */
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

/**
 * @brief 取缓存文件路径
 *
 * @return core::Paths 给出的 <dataRoot>/skills_cache.json
 */
QString SkillCache::filePath()
{
    return core::Paths::skillCacheFile();
}

/**
 * @brief 读回缓存快照
 *
 * 文件缺失、损坏（JsonStore::readFile 返回空对象）或格式版本不认识都
 * 返回 invalid Snapshot，不发警告——「首次启动」与「缓存过期」都是正常
 * 路径，扫描随后自愈。小文件毫秒级，允许在 GUI 线程同步调用。
 *
 * @return 缓存内容；isValid() 为 false 表示没有可用缓存
 */
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

/**
 * @brief 原子写缓存（经 JsonStore 的 QSaveFile 落盘）
 *
 * 静态纯函数、线程安全：扫描 worker 在自己的线程里调用，GUI 线程
 * 不为此付出任何 IO 时间。
 *
 * @param definitions 本次扫描出的全部 skill 定义
 * @param stats 同一次扫描的统计
 * @return 写文件结果；失败时 error 带原因
 */
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
