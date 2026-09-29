#ifndef AWB_THEME_THEMELOADER_H
#define AWB_THEME_THEMELOADER_H

#include "theme/ThemeFile.h"

#include <QJsonObject>
#include <QString>

namespace awb::theme {

/// 单个主题 JSON 的解析与校验。校验规则：
///   - 未知键        → 告警后忽略；
///   - 缺失令牌      → 取 baseline（同 variant 的内置主题）的值；
///   - 非法颜色/数值 → 告警后用 baseline 值兜底；
///   - id 缺失或与文件名不符 → 整个文件跳过（告警）。
class ThemeLoader
{
public:
    // 解析一个主题 JSON 对象；id 必须与传入的文件名一致，否则返回 false
    static bool parse(const QJsonObject &json, const QString &fileName,
                      const ThemeFile &baseline, ThemeFile &out);

    // 读磁盘或 :/ 资源里的一个主题文件并解析；任何失败都返回无效 ThemeFile
    static ThemeFile loadFile(const QString &path, const ThemeFile &baseline);
};

} // namespace awb::theme

#endif // AWB_THEME_THEMELOADER_H
