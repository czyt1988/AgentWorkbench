#ifndef AWB_SKILLS_SKILLDEFINITION_H
#define AWB_SKILLS_SKILLDEFINITION_H

#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace awb::skillcatalog {

/// 扫描发现的一条 skill 记录：frontmatter 字段 + 定位信息 + 文件统计。
///
/// 它是 SkillModel 的数据源，也是 skills_cache.json 的存储单元；
/// name 为空时由扫描侧回退到目录名。
struct SkillDefinition
{
    QString name;           ///< frontmatter 的 name；为空时回退到目录名
    QString description;    ///< frontmatter 的 description
    QString skillFilePath;  ///< SKILL.md 的绝对路径
    QString dirPath;        ///< skill 所在目录
    QString rootId;         ///< 发现该 skill 的扫描根 id
    QString rootLabel;      ///< 扫描根的显示名
    QString kind;           ///< agents | claude | codex | plugin | project | custom
    QString pluginId;       ///< 插件 skill 的来源插件标识
    QString pluginVersion;  ///< 插件 skill 的来源插件版本
    QDateTime lastModified; ///< SKILL.md 的修改时刻
    qint64 sizeBytes = 0;   ///< SKILL.md 的字节数
    QVariantMap extras;     ///< 其余 frontmatter 标量（allowed-tools、version、metadata.* …）

    /// 全字段相等。SkillModel::setSkills 用它做「扫描结果与现状一致」的
    /// 短路判断（启动后台扫描 vs 缓存恢复的典型情形），一致时跳过
    /// modelReset，QML 不销毁重建整页卡片。
    friend bool operator==(const SkillDefinition &a, const SkillDefinition &b)
    {
        return a.name == b.name && a.description == b.description
               && a.skillFilePath == b.skillFilePath
               && a.dirPath == b.dirPath && a.rootId == b.rootId
               && a.rootLabel == b.rootLabel && a.kind == b.kind
               && a.pluginId == b.pluginId
               && a.pluginVersion == b.pluginVersion
               && a.lastModified == b.lastModified
               && a.sizeBytes == b.sizeBytes && a.extras == b.extras;
    }
    friend bool operator!=(const SkillDefinition &a, const SkillDefinition &b)
    {
        return !(a == b);
    }
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLDEFINITION_H
