#include "skills/SkillRoot.h"

#include "core/EnvExpander.h"

#include <QDir>

namespace awb::skills {

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
    // wildcard path, deduped by the scanner (02 §7.2).
    SkillRoot plugin;
    plugin.id = QStringLiteral("zcode-plugins");
    plugin.label = QStringLiteral("ZCode plugins");
    plugin.path =
        QStringLiteral("~/.zcode/cli/plugins/cache/*/*/*/skills");
    plugin.kind = QStringLiteral("plugin");
    plugin.dedupeScope = QStringLiteral("marketplace-plugin");
    roots.append(plugin);

    // Project skills: only when a working directory concept exists (02 §7.2
    // marks this as optional for v1 — included when the directories exist).
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

    for (SkillRoot &root : roots) {
        // Project roots anchor at the process working directory (there is
        // no cwd concept in the app model yet — 02 §7.2).
        root.path.replace(QStringLiteral("%PWD%"),
                           QDir::toNativeSeparators(QDir::currentPath()));
        root.path = core::EnvExpander::expand(root.path);
    }

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
        root.path = core::EnvExpander::expand(o.value(QStringLiteral("path")).toString());
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

} // namespace awb::skills
