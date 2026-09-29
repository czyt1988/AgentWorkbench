#include "web/WebProfilePaths.h"

#include "core/Paths.h"

#include <QDir>
#include <QRegularExpression>

namespace awb::web {

/**
 * @brief 取该 agent 的 profile 目录
 *
 * 返回前先确保目录存在（含父目录），调用方无需再建。
 *
 * @param agentId agent id；手工编辑的配置里可能混入目录分隔符等字符
 * @return <dataRoot>/webprofiles/<净化后的 agentId>，保证已存在于磁盘
 */
QString WebProfilePaths::profileDir(const QString &agentId)
{
    // agent id 约定是 slug（[a-z0-9-]），但仍然净化一次，保证手工编辑过的
    // 配置永远无法把路径拼到 profiles 目录之外。
    QString safe = agentId;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                 QStringLiteral("_"));
    const QString dir = core::Paths::webProfilesDir()
                        + QLatin1Char('/') + safe;
    QDir().mkpath(dir);
    return dir;
}

/**
 * @brief 取该 agent 对应 profile 的 WebEngine storageName
 *
 * @param agentId agent id，原样拼接
 * @return 形如 "awb-<agentId>" 的存储名
 */
QString WebProfilePaths::storageName(const QString &agentId)
{
    return QStringLiteral("awb-") + agentId;
}

} // namespace awb::web
