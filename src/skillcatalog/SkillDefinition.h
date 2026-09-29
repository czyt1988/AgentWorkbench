#ifndef AWB_SKILLS_SKILLDEFINITION_H
#define AWB_SKILLS_SKILLDEFINITION_H

#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace awb::skillcatalog {

// One discovered skill.
struct SkillDefinition
{
    QString name;         // frontmatter name, falls back to the directory
    QString description;
    QString skillFilePath; // absolute path of SKILL.md
    QString dirPath;        // the skill's directory
    QString rootId;
    QString rootLabel;
    QString kind; // agents | claude | codex | plugin | project | custom
    QString pluginId;
    QString pluginVersion;
    QDateTime lastModified;
    qint64 sizeBytes = 0;
    // Remaining frontmatter scalars (allowed-tools, version, metadata.* …).
    QVariantMap extras;

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
