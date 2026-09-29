#ifndef AWB_SKILLS_SKILLFRONTMATTER_H
#define AWB_SKILLS_SKILLFRONTMATTER_H

#include <QHash>
#include <QString>

namespace awb::skillcatalog {

/// SKILL.md 开头 frontmatter 块的解析结果。
struct SkillFrontmatter
{
    bool valid = false;  ///< 文件头确有 `---` 块且被解析
    QString name;        ///< 为空时由调用方回退到目录名
    QString description;
    /// 其余标量键：`allowed-tools`、`version`、被展平的 `metadata.*` 子键等
    QHash<QString, QString> extras;
};

/// 极小的 YAML 子集解析器：只处理开头的 `---` 块、`key: value` 标量、
/// 单/双引号、带缩进续行的 `>`/`|` 块标量，容忍 BOM 与 CRLF。嵌套映射
/// 展平成 `metadata.author` 风格的键；列表以文本形式并入父键的值。
/// 不支持锚点、flow 集合；注释只按「整行 `#` 忽略」处理。
class SkillFrontmatterParser
{
public:
    // 内容没有 frontmatter 块时返回 valid = false 的结果
    static SkillFrontmatter parse(const QByteArray &content);
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLFRONTMATTER_H
