#include "skillcatalog/SkillRoot.h"

namespace awb::skillcatalog {

/**
 * @brief 取平台默认的扫描根清单
 *
 * @return 默认根列表；路径一律保持原始占位形式（见函数尾的说明）
 */
QList<SkillRoot> SkillRoots::defaults()
{
    QList<SkillRoot> roots;

    SkillRoot agents;
    agents.id = QStringLiteral("agents");
    agents.label = QStringLiteral("Agents");
    agents.path = QStringLiteral("~/.agents/skills");
    agents.kind = QStringLiteral("agents");
    roots.append(agents);

    SkillRoot claude;
    claude.id = QStringLiteral("claude");
    claude.label = QStringLiteral("Claude");
    claude.path = QStringLiteral("~/.claude/skills");
    claude.kind = QStringLiteral("claude");
    roots.append(claude);

    SkillRoot codex;
    codex.id = QStringLiteral("codex");
    codex.label = QStringLiteral("Codex");
    codex.path = QStringLiteral("~/.codex/skills");
    codex.kind = QStringLiteral("codex");
    roots.append(codex);

    // ZCode 插件缓存：marketplace/插件/版本多级并存——通配符路径，
    // 由扫描器按 marketplace+plugin 去重。
    SkillRoot plugin;
    plugin.id = QStringLiteral("zcode-plugins");
    plugin.label = QStringLiteral("ZCode plugins");
    plugin.path =
        QStringLiteral("~/.zcode/cli/plugins/cache/*/*/*/skills");
    plugin.kind = QStringLiteral("plugin");
    plugin.dedupeScope = QStringLiteral("marketplace-plugin");
    roots.append(plugin);

    // 项目级 skill：只在存在「工作目录」概念时有意义。
    // v1 里作为可选项——目录存在才会扫到内容。
    SkillRoot projectAgents;
    projectAgents.id = QStringLiteral("project-agents");
    projectAgents.label = QStringLiteral("Project (.agents)");
    projectAgents.path = QStringLiteral("%PWD%/.agents/skills");
    projectAgents.kind = QStringLiteral("project");
    roots.append(projectAgents);

    SkillRoot projectClaude;
    projectClaude.id = QStringLiteral("project-claude");
    projectClaude.label = QStringLiteral("Project (.claude)");
    projectClaude.path = QStringLiteral("%PWD%/.claude/skills");
    projectClaude.kind = QStringLiteral("project");
    roots.append(projectClaude);

    // 路径在这里刻意保持 RAW：`~`、`%PWD%` 与通配符是占位符，到扫描时
    // 才展开（SkillScanner::effectiveRoot）。若在读取时展开，
    // setRootEnabled/addRoot 会把展开后的形式回写进 settings.json——
    // 把某台机器的 home 目录或 cwd 固化进去。
    return roots;
}

/**
 * @brief 解析 settings.json `skills.roots` 数组
 *
 * 非对象条目与 path 为空的条目直接跳过；kind 缺省 custom，id 缺省按
 * 「kind-序号」生成，enabled 缺省 true；kind 为 plugin 的条目补上
 * marketplace+plugin 的去重域。path 保持 RAW（见 defaults() 的说明）。
 *
 * @param entries settings.json 里的 `skills.roots` 数组
 * @return 解析出的根列表；空数组返回空列表（调用方按「用默认」处理）
 */
QList<SkillRoot> SkillRoots::fromJson(const QJsonArray &entries)
{
    QList<SkillRoot> roots;
    for (const QJsonValue &value : entries) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject o = value.toObject();
        SkillRoot root;
        // 刻意保持 RAW——展开推迟到扫描时，这样持久化的往返
        // 保留 `~`/`%PWD%`，而不是把绝对路径固化进去。
        root.path = o.value(QStringLiteral("path")).toString();
        if (root.path.isEmpty()) {
            continue;
        }
        root.label = o.value(QStringLiteral("label")).toString();
        root.kind = o.value(QStringLiteral("kind")).toString();
        if (root.kind.isEmpty()) {
            root.kind = QStringLiteral("custom");
        }
        root.id = o.value(QStringLiteral("id")).toString();
        if (root.id.isEmpty()) {
            root.id = root.kind + QLatin1Char('-')
                      + QString::number(roots.size());
        }
        root.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (root.kind == QStringLiteral("plugin")) {
            root.dedupeScope = QStringLiteral("marketplace-plugin");
        }
        roots.append(root);
    }
    return roots;
}

} // namespace awb::skillcatalog
