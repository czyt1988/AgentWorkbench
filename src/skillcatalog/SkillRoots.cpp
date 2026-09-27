#include "skillcatalog/SkillRoot.h"

namespace awb::skillcatalog {

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

    // ZCode plugin caches: multiple marketplaces/plugins/versions at once —
    // wildcard path, deduped by the scanner.
    SkillRoot plugin;
    plugin.id = QStringLiteral("zcode-plugins");
    plugin.label = QStringLiteral("ZCode plugins");
    plugin.path =
        QStringLiteral("~/.zcode/cli/plugins/cache/*/*/*/skills");
    plugin.kind = QStringLiteral("plugin");
    plugin.dedupeScope = QStringLiteral("marketplace-plugin");
    roots.append(plugin);

    // Project skills: only when a working directory concept exists.
    // Optional for v1 — included when the directories exist.
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

    // Paths stay RAW here on purpose: `~`, `%PWD%` and wildcards are
    // placeholders expanded at SCAN time (SkillScanner::effectiveRoot).
    // Expanding at load would get the expanded form persisted back by
    // setRootEnabled/addRoot — baking one machine's home directory or cwd
    // into settings.json.
    return roots;
}

QList<SkillRoot> SkillRoots::fromJson(const QJsonArray &entries)
{
    QList<SkillRoot> roots;
    for (const QJsonValue &value : entries) {
        if (!value.isObject())
            continue;
        const QJsonObject o = value.toObject();
        SkillRoot root;
        // RAW on purpose — expansion happens at scan time so a persisted
        // round-trip keeps `~`/`%PWD%` instead of baking in absolutes.
        root.path = o.value(QStringLiteral("path")).toString();
        if (root.path.isEmpty())
            continue;
        root.label = o.value(QStringLiteral("label")).toString();
        root.kind = o.value(QStringLiteral("kind")).toString();
        if (root.kind.isEmpty())
            root.kind = QStringLiteral("custom");
        root.id = o.value(QStringLiteral("id")).toString();
        if (root.id.isEmpty())
            root.id = root.kind + QLatin1Char('-')
                      + QString::number(roots.size());
        root.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (root.kind == QLatin1String("plugin"))
            root.dedupeScope = QStringLiteral("marketplace-plugin");
        roots.append(root);
    }
    return roots;
}

} // namespace awb::skillcatalog
