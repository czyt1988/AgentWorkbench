#ifndef AWB_SKILLS_SKILLFRONTMATTER_H
#define AWB_SKILLS_SKILLFRONTMATTER_H

#include <QHash>
#include <QString>

namespace awb::skills {

// Parsed frontmatter of a SKILL.md (01-architecture.md §4.4).
struct SkillFrontmatter
{
    bool valid = false; // a leading `---` block was found and parsed
    QString name;       // falls back to the directory name at the call site
    QString description;
    // Every other scalar key: `allowed-tools`, `version`, flattened
    // `metadata.*` children, …
    QHash<QString, QString> extras;
};

// A very small YAML subset parser (specs/03 S6-T1): only the leading `---`
// block, `key: value` scalars, single/double quotes, `>`/`|` block scalars
// with indented continuation, BOM/CRLF tolerance. Nested mappings are
// flattened (`metadata.author` style keys); lists appear as part of the
// parent value. No anchors, no flow collections, no comments handling
// beyond ignoring whole-line `#`.
class SkillFrontmatterParser
{
public:
    // Returns an invalid result when the content has no frontmatter block.
    static SkillFrontmatter parse(const QByteArray &content);
};

} // namespace awb::skills

#endif // AWB_SKILLS_SKILLFRONTMATTER_H
