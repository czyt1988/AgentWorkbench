#ifndef AWB_SKILLS_SKILLROOT_H
#define AWB_SKILLS_SKILLROOT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace awb::skillcatalog {

/// 一个扫描根。`path` 可以带通配符（ZCode 的插件缓存根就是）——展开
/// 由扫描器在扫描时完成，这里始终存原始形式。
struct SkillRoot
{
    QString id;      ///< 根的稳定标识（如 "agents"、"zcode-plugins"）
    QString label;   ///< 显示名
    QString path;    ///< 原始路径；可含 `~`、`%PWD%` 与通配符
    QString kind = QStringLiteral("custom"); ///< agents|claude|codex|plugin|project|custom
    bool enabled = true;   ///< 是否参与扫描
    bool recursive = true; ///< 是否递归扫子目录
    QString dedupeScope;   ///< 插件根的去重域：按 marketplace+plugin 跨版本去重

    /// id 与 path 都非空才算可用
    bool isValid() const { return !id.isEmpty() && !path.isEmpty(); }
};

/// 默认扫描根清单与 settings.json `skills.roots` 条目的转换。
class SkillRoots
{
public:
    // 平台默认根清单（~/.agents、~/.claude、~/.codex、ZCode 插件缓存、
    // %PWD% 下的项目目录）；路径一律保持原始占位形式
    static QList<SkillRoot> defaults();

    // 解析 settings.json `skills.roots` 条目（{path, kind?, id?, enabled?}）；
    // 空数组 = 用默认清单
    static QList<SkillRoot> fromJson(const QJsonArray &entries);
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLROOT_H
