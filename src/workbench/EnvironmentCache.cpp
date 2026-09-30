#include "workbench/EnvironmentCache.h"

#include "core/JsonStore.h"
#include "core/Paths.h"

#include <QJsonObject>

namespace awb::workbench {

namespace {

/**
 * @brief 缓存格式版本
 *
 * load() 只认这个值。改结构（增删字段、改语义）时递增，让旧文件按
 * 「过期」处理——旧缓存当没有用即可，随后的探测会立刻重建，所以不做
 * 任何跨版本迁移（与 settings 的无迁移原则一致）。
 */
constexpr int kFormatVersion = 1;

/**
 * @brief 把一个运行时的结论序列化成 JSON 对象
 *
 * @param state 运行时结论
 * @return 可放入根对象的 JSON 对象
 */
QJsonObject runtimeToJson(const RuntimeState &state)
{
    QJsonObject o;
    o[QStringLiteral("known")] = state.known;
    o[QStringLiteral("installed")] = state.installed;
    o[QStringLiteral("version")] = state.version;
    o[QStringLiteral("path")] = state.path;
    return o;
}

/**
 * @brief 把 JSON 对象还原成一个运行时的结论
 *
 * 缺字段按「没结论」处理；未安装时不收 version/path（文件被手工改过时
 * 不至于把陈旧版本当有效结论摆出来）。
 *
 * @param o 缓存里的运行时对象
 * @return 还原出的结论
 */
RuntimeState runtimeFromJson(const QJsonObject &o)
{
    RuntimeState state;
    state.known = o.value(QStringLiteral("known")).toBool(false);
    state.installed = o.value(QStringLiteral("installed")).toBool(false);
    if (state.installed) {
        state.version = o.value(QStringLiteral("version")).toString();
        state.path = o.value(QStringLiteral("path")).toString();
    }
    return state;
}

} // namespace

/**
 * @brief 取缓存文件路径
 *
 * @return <dataRoot>/environment_cache.json
 */
QString EnvironmentCache::filePath()
{
    return core::Paths::environmentCacheFile();
}

/**
 * @brief 读回缓存快照
 *
 * 文件缺失、损坏（JsonStore::readFile 返回空对象）、格式版本不认识或
 * 两个运行时都缺，都返回 invalid Snapshot 且不告警。
 *
 * @return 缓存内容；isValid() 为 false 表示没有可用缓存
 */
EnvironmentCache::Snapshot EnvironmentCache::load()
{
    Snapshot snapshot;
    const QJsonObject root = core::JsonStore::readFile(filePath());
    if (root.isEmpty()) {
        return snapshot;
    }
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        // 过期格式按「没有缓存」处理：随后的探测马上重建，不告警。
        return snapshot;
    }

    const QJsonObject python = root.value(QStringLiteral("python")).toObject();
    const QJsonObject node = root.value(QStringLiteral("node")).toObject();
    if (python.isEmpty() && node.isEmpty()) {
        return snapshot;
    }

    snapshot.python = runtimeFromJson(python);
    snapshot.node = runtimeFromJson(node);
    snapshot.cachedAt = QDateTime::fromString(
        root.value(QStringLiteral("cachedAt")).toString(), Qt::ISODateWithMs);
    return snapshot;
}

/**
 * @brief 写缓存
 *
 * @param python Python 的当前结论
 * @param node Node.js 的当前结论
 * @return 写入结果；父目录由 JsonStore 自动创建
 */
core::OpResult EnvironmentCache::save(const RuntimeState &python,
                                      const RuntimeState &node)
{
    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("cachedAt")] =
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    root[QStringLiteral("python")] = runtimeToJson(python);
    root[QStringLiteral("node")] = runtimeToJson(node);
    return core::JsonStore::writeFile(filePath(), root);
}

} // namespace awb::workbench
