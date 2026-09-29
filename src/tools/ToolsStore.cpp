#include "tools/ToolsStore.h"

#include "core/JsonStore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <utility>

namespace awb::tools {

namespace {

/**
 * @brief tools.json 的键名
 *
 * workspaces（字符串数组，MRU 在前）、current（当前工作区绝对路径）、
 * draft（提示词草稿全文）。键名是磁盘格式的一部分，改键名等于丢用户数据。
 */
constexpr auto kWorkspacesKey = "workspaces";
constexpr auto kCurrentKey = "current";
constexpr auto kDraftKey = "draft";
} // namespace

/**
 * @brief 构造工作区记忆存储
 *
 * 只记数据根；磁盘状态要等 load()。
 *
 * @param dataRoot 应用数据根（<dataRoot>/tools.json）
 */
ToolsStore::ToolsStore(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
}

/**
 * @brief 取存储文件的绝对路径
 *
 * @return <dataRoot>/tools.json
 */
QString ToolsStore::storeFilePath() const
{
    return m_dataRoot + QStringLiteral("/tools.json");
}

/**
 * @brief 读取 tools.json 到成员
 *
 * 文件缺失或损坏（JsonStore 记警告并给空对象）一律得到空列表、空
 * current、空草稿。
 */
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
            if (!path.isEmpty() && !m_workspaces.contains(path)) {
                m_workspaces.append(path);
            }
        }
    }
    m_current = root.value(QLatin1String(kCurrentKey)).toString();
    m_draft = root.value(QLatin1String(kDraftKey)).toString();

    // 历史数据里 current 可能指向已删除的项；容忍并保留原值，
    // 是否可用由 ToolsFacade 决定（不存在的工作区不给文件树设根）。
    if (!m_workspaces.contains(m_current)) {
        m_current.clear();
    }
}

/**
 * @brief 新增工作区（或把已有项移到队首）并设为当前
 *
 * @param path 工作区的绝对路径
 * @return 落盘是否成功（内存状态无论成败都已更新）
 */
bool ToolsStore::addWorkspace(const QString &path)
{
    // 已存在时这是一次 MRU 触碰：先移除再插回队首，统一两条路径。
    m_workspaces.removeAll(path);
    m_workspaces.prepend(path);
    // 超出上限从队尾淘汰——队尾是最久未使用的。
    while (m_workspaces.size() > kMaxWorkspaces) {
        m_workspaces.removeLast();
    }
    m_current = path;
    return save();
}

/**
 * @brief 移除一个工作区
 *
 * 被移除的恰是当前工作区时，current 顺延为队首剩余项（列表空则为空串）。
 *
 * @param path 要移除的工作区路径；不在列表中时相当于只落盘一次
 * @return 落盘是否成功
 */
bool ToolsStore::removeWorkspace(const QString &path)
{
    m_workspaces.removeAll(path);
    if (m_current == path) {
        m_current = m_workspaces.isEmpty() ? QString() : m_workspaces.constFirst();
    }
    return save();
}

/**
 * @brief 切换当前工作区并把该项移到队首（MRU 触碰）
 *
 * @param path 列表内的工作区路径；空串表示清空当前工作区（合法目标）
 * @return path 非空且不在列表中时返回 false（拒绝且不落盘），否则返回落盘是否成功
 */
bool ToolsStore::setCurrentWorkspace(const QString &path)
{
    // 空串是合法目标（清空当前工作区）；不在列表中的路径不接受。
    if (!path.isEmpty() && !m_workspaces.contains(path)) {
        return false;
    }
    m_workspaces.removeAll(path);
    if (!path.isEmpty()) {
        m_workspaces.prepend(path);
    }
    m_current = path;
    return save();
}

/**
 * @brief 覆盖草稿并立即写盘
 *
 * @param text 草稿全文
 * @return 落盘是否成功；内容没变时直接返回 true（不写盘）
 */
bool ToolsStore::setDraft(const QString &text)
{
    if (m_draft == text) {
        return true;
    }
    m_draft = text;
    return save();
}

/**
 * @brief 把当前全部状态写回 tools.json
 *
 * 经 core::JsonStore 原子写（临时文件 + 替换）。
 *
 * @return 落盘是否成功
 */
bool ToolsStore::save()
{
    QJsonObject root;
    QJsonArray list;
    for (const QString &path : std::as_const(m_workspaces)) {
        list.append(path);
    }
    root[QLatin1String(kWorkspacesKey)] = list;
    root[QLatin1String(kCurrentKey)] = m_current;
    root[QLatin1String(kDraftKey)] = m_draft;
    return core::JsonStore::writeFile(storeFilePath(), root).ok;
}

} // namespace awb::tools
