#ifndef AWB_CORE_JSONSTORE_H
#define AWB_CORE_JSONSTORE_H

#include "core/OpResult.h"

#include <QJsonObject>
#include <QString>

namespace awb::core {

/// 应用所有配置文件的 JSON 读写。
///
/// 保存经 QSaveFile 原子落盘、缩进统一；读取失败降级为空对象加一条日志，
/// 不让单个坏文件拖垮应用。
class JsonStore
{
public:
    // 读 path：文件不存在得到空对象且不告警；不可读或格式坏则记日志并同样返回空对象
    static QJsonObject readFile(const QString &path);

    // 把 object 以缩进 JSON 原子写入 path，父目录不存在时自动创建
    static OpResult writeFile(const QString &path, const QJsonObject &object);

    // 原子写入原始字节（用于必须与随包原始文件逐字节一致的场合）
    static OpResult writeBytes(const QString &path, const QByteArray &bytes);
};

} // namespace awb::core

#endif // AWB_CORE_JSONSTORE_H
