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
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLDEFINITION_H
