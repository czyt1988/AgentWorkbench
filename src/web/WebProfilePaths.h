#ifndef AWB_WEB_WEBPROFILEPATHS_H
#define AWB_WEB_WEBPROFILEPATHS_H

#include <QString>

namespace awb::web {

/// 每个 agent 的持久 Web profile 路径，唯一的推导来源。
///
/// 「一个 agent 一个 profile」是硬要求：Chromium 按 host 索引 cookie 且忽略
/// 端口，共享 profile 会让 127.0.0.1:58627 与 127.0.0.1:4096 的本地服务会话
/// 互相串号（实测证据见 docs/research/webengine-embedding.md §3.1）。
class WebProfilePaths
{
public:
    // <dataRoot>/webprofiles/<agentId>——cookie 与 localStorage 的落盘目录
    static QString profileDir(const QString &agentId);

    // agent 对应 profile 的 WebEngine storageName（"awb-<agentId>"）
    static QString storageName(const QString &agentId);
};

} // namespace awb::web

#endif // AWB_WEB_WEBPROFILEPATHS_H
