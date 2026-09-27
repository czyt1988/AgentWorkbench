#include "tools/ToolsStore.h"

#include "core/JsonStore.h"

#include <QJsonArray>
#include <QJsonObject>

namespace awb::tools {

/// 文件键位：workspaces（字符串数组，MRU 在前）、current（绝对路径）、draft。
/// 键名是磁盘格式的一部分，改键名等于丢用户数据。
namespace {
constexpr auto kWorkspacesKey = "workspaces";
constexpr auto kCurrentKey = "current";
constexpr auto kDraftKey = "draft";
} // namespace

ToolsStore::ToolsStore(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
}

QString ToolsStore::storeFilePath() const
{
    return m_dataRoot + QStringLiteral("/tools.json");
}

void ToolsStore::load()
{
    m_workspaces.clear();
    m_current.clear();
    m_draft.clear();

    const QJsonObject root = core::JsonStore::readFile(storeFilePath());
    const QJsonArray list = root.value(QLatin1String(kWorkspacesKey)).toArray();
    for (const QJsonValue &value : list) {
        // 只收字符串；坏元素直接跳过，不让单个坏项拖垮整个列表。
        if (value.isString()) {
            const QString path = value.toString();
            if (!path.isEmpty() && !m_workspaces.contains(path))
                m_workspaces.append(path);
        }
    }
    m_current = root.value(QLatin1String(kCurrentKey)).toString();
    m_draft = root.value(QLatin1String(kDraftKey)).toString();

    // 历史数据里 current 可能指向已删除的项；容忍并保留原值，
    // 是否可用由 ToolsFacade 决定（不存在的工作区不给文件树设根）。
    if (!m_workspaces.contains(m_current))
        m_current.clear();
}

bool ToolsStore::addWorkspace(const QString &path)
{
    // 已存在时这是一次 MRU 触碰：先移除再插回队首，统一两条路径。
    m_workspaces.removeAll(path);
    m_workspaces.prepend(path);
    // 超出上限从队尾淘汰——队尾是最久未使用的。
    while (m_workspaces.size() > kMaxWorkspaces)
        m_workspaces.removeLast();
    m_current = path;
    return save();
}

bool ToolsStore::removeWorkspace(const QString &path)
{
    m_workspaces.removeAll(path);
    if (m_current == path)
        m_current = m_workspaces.isEmpty() ? QString() : m_workspaces.constFirst();
    return save();
}

bool ToolsStore::setCurrentWorkspace(const QString &path)
{
    // 空串是合法目标（清空当前工作区）；不在列表中的路径不接受。
    if (!path.isEmpty() && !m_workspaces.contains(path))
        return false;
    m_workspaces.removeAll(path);
    if (!path.isEmpty())
        m_workspaces.prepend(path);
    m_current = path;
    return save();
}

bool ToolsStore::setDraft(const QString &text)
{
    if (m_draft == text)
        return true;
    m_draft = text;
    return save();
}

bool ToolsStore::save()
{
    QJsonObject root;
    QJsonArray list;
    for (const QString &path : m_workspaces)
        list.append(path);
    root[QLatin1String(kWorkspacesKey)] = list;
    root[QLatin1String(kCurrentKey)] = m_current;
    root[QLatin1String(kDraftKey)] = m_draft;
    return core::JsonStore::writeFile(storeFilePath(), root).ok;
}

} // namespace awb::tools
