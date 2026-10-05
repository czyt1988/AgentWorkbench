#include "tools/ToolsStore.h"

#include "core/JsonStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <utility>

namespace awb::tools {

namespace {

/**
 * @brief tools.json 的键名
 *
 * workspaces（字符串数组，MRU 在前）、current（当前工作区绝对路径）、
 * draft（提示词草稿全文）、ribbon（Ribbon 界面偏好对象）。键名是磁盘
 * 格式的一部分，改键名等于丢用户数据。
 */
constexpr auto kWorkspacesKey = "workspaces";
constexpr auto kCurrentKey = "current";
constexpr auto kDraftKey = "draft";
constexpr auto kRibbonKey = "ribbon";
constexpr auto kFavoritesKey = "favorites";

// ribbon 段内的键名与样式档位的合法取值（单行 / 两行 / 三行）。
constexpr auto kRibbonCollapsedKey = "collapsed";
constexpr auto kRibbonStyleKey = "style";
constexpr auto kRibbonFollowThemeKey = "followAppTheme";
constexpr auto kRibbonLastTabKey = "lastTabIndex";

// favorites 数组项的键名。
constexpr auto kFavoriteTitleKey = "title";
constexpr auto kFavoriteBodyKey = "body";
constexpr auto kFavoriteTagsKey = "tags";
constexpr auto kFavoriteCreatedAtKey = "createdAt";

/**
 * @brief 从 JSON 对象还原 Ribbon 偏好，坏值一律落到默认
 *
 * 这是 ribbon 段的读侧容错：段缺失、类型不对、样式串不认识，都得到
 * 一份合法的默认值，不报错、不写回。
 */
RibbonSettings ribbonFromJson(const QJsonObject &object)
{
    RibbonSettings ribbon;
    ribbon.collapsed = object.value(QLatin1String(kRibbonCollapsedKey)).toBool(false);
    const QString style =
        object.value(QLatin1String(kRibbonStyleKey)).toString();
    if (style == QLatin1String("single") || style == QLatin1String("two-row")
        || style == QLatin1String("three-row")) {
        ribbon.style = style;
    }
    ribbon.followAppTheme =
        object.value(QLatin1String(kRibbonFollowThemeKey)).toBool(true);
    const int tab = object.value(QLatin1String(kRibbonLastTabKey)).toInt(0);
    ribbon.lastTabIndex = tab > 0 ? tab : 0;
    return ribbon;
}

/**
 * @brief 从 JSON 数组还原收藏夹（读侧容错）
 *
 * 坏条目（无标题、无正文、类型不对）跳过；标签只收字符串。上限在读侧
 * 就夹住——手改过的文件不该把列表撑到界面吃不下。
 */
QList<FavoriteEntry> favoritesFromJson(const QJsonArray &array)
{
    QList<FavoriteEntry> result;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        FavoriteEntry entry;
        entry.title =
            object.value(QLatin1String(kFavoriteTitleKey)).toString();
        entry.body =
            object.value(QLatin1String(kFavoriteBodyKey)).toString();
        if (entry.title.isEmpty() || entry.body.isEmpty()) {
            continue;
        }
        const QJsonArray tags =
            object.value(QLatin1String(kFavoriteTagsKey)).toArray();
        for (const QJsonValue &tag : tags) {
            if (tag.isString() && !tag.toString().isEmpty()) {
                entry.tags.append(tag.toString());
            }
        }
        entry.createdAt = static_cast<qint64>(
            object.value(QLatin1String(kFavoriteCreatedAtKey))
                .toDouble(0));
        result.append(entry);
        if (result.size() >= ToolsStore::kMaxFavorites) {
            break;
        }
    }
    return result;
}

/**
 * @brief 收藏条目 → JSON
 */
QJsonObject favoriteToJson(const FavoriteEntry &entry)
{
    QJsonObject object;
    object.insert(QLatin1String(kFavoriteTitleKey), entry.title);
    object.insert(QLatin1String(kFavoriteBodyKey), entry.body);
    object.insert(QLatin1String(kFavoriteTagsKey),
                  QJsonArray::fromStringList(entry.tags));
    object.insert(QLatin1String(kFavoriteCreatedAtKey),
                  double(entry.createdAt));
    return object;
}

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
 * current、空草稿；ribbon 段缺失时取默认值（见 ribbonFromJson）。
 */
void ToolsStore::load()
{
    m_workspaces.clear();
    m_current.clear();
    m_draft.clear();
    m_ribbon = RibbonSettings();

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
    m_ribbon = ribbonFromJson(
        root.value(QLatin1String(kRibbonKey)).toObject());
    m_favorites = favoritesFromJson(
        root.value(QLatin1String(kFavoritesKey)).toArray());

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
 * @brief 覆盖 Ribbon 界面偏好并立即写盘
 *
 * @param ribbon 新的偏好值
 * @return 落盘是否成功；内容没变时直接返回 true（不写盘）
 */
bool ToolsStore::setRibbon(const RibbonSettings &ribbon)
{
    if (m_ribbon.collapsed == ribbon.collapsed
        && m_ribbon.style == ribbon.style
        && m_ribbon.followAppTheme == ribbon.followAppTheme
        && m_ribbon.lastTabIndex == ribbon.lastTabIndex) {
        return true;
    }
    m_ribbon = ribbon;
    return save();
}

/**
 * @brief 新增一条收藏（新在队首；同标题同全文 = 更新时间戳的幂等重存）
 *
 * @param title 显示标题
 * @param body 全文
 * @param tags 标签（可空）
 * @return 落盘是否成功；超出上限时最旧（队尾）的一条被淘汰
 */
bool ToolsStore::addFavorite(const QString &title, const QString &body,
                             const QStringList &tags)
{
    if (title.isEmpty() || body.isEmpty()) {
        return true;
    }
    FavoriteEntry entry;
    entry.title = title;
    entry.body = body;
    entry.tags = tags;
    entry.createdAt = QDateTime::currentMSecsSinceEpoch();
    // 幂等：同标题同全文视为重存（时间戳刷新、挪到队首），不占新位。
    for (int i = 0; i < m_favorites.size(); ++i) {
        if (m_favorites.at(i).title == title
            && m_favorites.at(i).body == body) {
            m_favorites.removeAt(i);
            break;
        }
    }
    m_favorites.prepend(entry);
    while (m_favorites.size() > kMaxFavorites) {
        m_favorites.removeLast();
    }
    return save();
}

/**
 * @brief 按标题 + 全文精确匹配移除一条收藏
 *
 * @param title 标题
 * @param body 全文
 * @return 落盘是否成功（没有匹配项时同样返回 true——「没有了」就是目的）
 */
bool ToolsStore::removeFavorite(const QString &title, const QString &body)
{
    for (int i = 0; i < m_favorites.size(); ++i) {
        if (m_favorites.at(i).title == title
            && m_favorites.at(i).body == body) {
            m_favorites.removeAt(i);
            return save();
        }
    }
    return true;
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
    QJsonObject ribbon;
    ribbon[QLatin1String(kRibbonCollapsedKey)] = m_ribbon.collapsed;
    ribbon[QLatin1String(kRibbonStyleKey)] = m_ribbon.style;
    ribbon[QLatin1String(kRibbonFollowThemeKey)] = m_ribbon.followAppTheme;
    ribbon[QLatin1String(kRibbonLastTabKey)] = m_ribbon.lastTabIndex;
    root[QLatin1String(kRibbonKey)] = ribbon;
    QJsonArray favorites;
    for (const FavoriteEntry &entry : std::as_const(m_favorites)) {
        favorites.append(favoriteToJson(entry));
    }
    root[QLatin1String(kFavoritesKey)] = favorites;
    return core::JsonStore::writeFile(storeFilePath(), root).ok;
}

} // namespace awb::tools
