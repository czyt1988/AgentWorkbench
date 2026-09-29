#ifndef AWB_CORE_LEGACYIMPORT_H
#define AWB_CORE_LEGACYIMPORT_H

#include <QString>

namespace awb::core {

/// 0.4 之前 AgentLauncher 数据目录（~/.AgentLauncher）的一次性收编。
///
/// 旧目录只复制、不搬移不删除；导入至多跑一次——新数据根里一旦出现
/// log 目录以外的任何内容（Logging 先装，见实现）就不再动。
class LegacyImport
{
public:
    // 把旧目录收编进 newRoot（应用数据根）。返回 true 表示发生了导入，
    // outNotice 收到一条给用户看的一次性提示。每次启动调用都安全，
    // 之后的调用是空操作。
    static bool runOnce(const QString &newRoot, QString *outNotice = nullptr);

    // 同 runOnce，两个目录都由外部注入，供测试用临时目录
    static bool importOnce(const QString &newRoot, const QString &oldRoot,
                           QString *outNotice = nullptr);
};

} // namespace awb::core

#endif // AWB_CORE_LEGACYIMPORT_H
