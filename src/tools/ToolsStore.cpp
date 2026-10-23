#include "tools/ToolsStore.h"

#include "core/JsonStore.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <utility>

namespace awb::tools {

namespace {

/**
 * @brief tools.json 的键名
 *
 * workspaces（字符串数组，MRU 在前）、current（当前工作区绝对路径）、
 * draft（提示词草稿全文）、ribbon（Ribbon 界面偏好对象）、editor（编辑器
 * 偏好对象，M3）、checks（体检规则开关对象，M3）。键名是磁盘格式的一
 * 部分，改键名等于丢用户数据。
 */
constexpr auto kWorkspacesKey = "workspaces";
constexpr auto kCurrentKey = "current";
constexpr auto kDraftKey = "draft";
constexpr auto kRibbonKey = "ribbon";
constexpr auto kFavoritesKey = "favorites";
constexpr auto kEditorKey = "editor";
constexpr auto kChecksKey = "checks";
constexpr auto kDraftsKey = "drafts";
constexpr auto kActiveDraftIdKey = "activeDraftId";
constexpr auto kHistoryKey = "history";

// drafts 数组项的键名。
constexpr auto kDraftIdKey = "id";
constexpr auto kDraftTitleKey = "title";
constexpr auto kDraftBodyKey = "body";
constexpr auto kDraftWorkspaceKey = "workspace";
constexpr auto kDraftSavePathKey = "savePath";
constexpr auto kDraftCreatedAtKey = "createdAt";
constexpr auto kDraftUpdatedAtKey = "updatedAt";

// history 数组项的键名。
constexpr auto kSnapshotDraftIdKey = "draftId";
constexpr auto kSnapshotBodyKey = "body";
constexpr auto kSnapshotReasonKey = "reason";
constexpr auto kSnapshotCreatedAtKey = "createdAt";
constexpr auto kSnapshotCharCountKey = "charCount";

// tools_recovery.json 的键名。
constexpr auto kRecoveryDraftIdKey = "draftId";
constexpr auto kRecoveryBodyKey = "body";
constexpr auto kRecoveryUpdatedAtKey = "updatedAt";

// 旧 draft 键迁移为草稿时固定的 id（规划 07：稳定 id 让旧配置可预期）。
constexpr auto kLegacyDraftId = "default";

// ribbon 段内的键名与样式档位的合法取值（单行 / 两行 / 三行）。
constexpr auto kRibbonCollapsedKey = "collapsed";
constexpr auto kRibbonStyleKey = "style";
constexpr auto kRibbonFollowThemeKey = "followAppTheme";
constexpr auto kRibbonLastTabKey = "lastTabIndex";

// editor 段内的键名。
constexpr auto kEditorCaseKey = "findCaseSensitive";
constexpr auto kEditorWholeWordsKey = "findWholeWords";
constexpr auto kEditorContextWindowKey = "contextWindowTokens";

// checks 段内的键名（只存已关闭的规则 id）。
constexpr auto kChecksDisabledKey = "disabled";

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
    if (style == QLatin1String("single") || style == QLatin1String("two-row")) {
        ribbon.style = style;
    }
    // 三行档已退役（2026-10 精简为紧凑/宽松两档）：旧值落回单行。
    ribbon.followAppTheme =
        object.value(QLatin1String(kRibbonFollowThemeKey)).toBool(true);
    const int tab = object.value(QLatin1String(kRibbonLastTabKey)).toInt(0);
    ribbon.lastTabIndex = tab > 0 ? tab : 0;
    return ribbon;
}

/**
 * @brief 从 JSON 对象还原编辑器偏好，坏值一律落到默认
 *
 * 与 ribbon 段同一套读侧容错纪律。窗口基数夹在 [1000, 1,000,000]——
 * 手改文件改出 0 或负数没有意义，改出天文数字则预算条永远显示安全。
 */
EditorSettings editorFromJson(const QJsonObject &object)
{
    EditorSettings editor;
    editor.findCaseSensitive =
        object.value(QLatin1String(kEditorCaseKey)).toBool(false);
    editor.findWholeWords =
        object.value(QLatin1String(kEditorWholeWordsKey)).toBool(false);
    const int window =
        object.value(QLatin1String(kEditorContextWindowKey)).toInt(
            editor.contextWindowTokens);
    editor.contextWindowTokens = qBound(1000, window, 1000000);
    return editor;
}

/**
 * @brief 从 JSON 数组还原草稿集（读侧容错）
 *
 * 坏条目（无 id、无 body 键、类型不对）跳过；id 重复保留第一个。时间戳
 * 缺失给 0（显示侧按「未知时间」处理）。
 */
QList<DraftEntry> draftsFromJson(const QJsonArray &array)
{
    QList<DraftEntry> result;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        DraftEntry entry;
        entry.id = object.value(QLatin1String(kDraftIdKey)).toString();
        if (entry.id.isEmpty()) {
            continue;
        }
        entry.title =
            object.value(QLatin1String(kDraftTitleKey)).toString();
        entry.body =
            object.value(QLatin1String(kDraftBodyKey)).toString();
        entry.workspace =
            object.value(QLatin1String(kDraftWorkspaceKey)).toString();
        entry.savePath =
            object.value(QLatin1String(kDraftSavePathKey)).toString();
        entry.createdAt = static_cast<qint64>(
            object.value(QLatin1String(kDraftCreatedAtKey)).toDouble(0));
        entry.updatedAt = static_cast<qint64>(
            object.value(QLatin1String(kDraftUpdatedAtKey)).toDouble(0));
        bool duplicate = false;
        for (const DraftEntry &existing : result) {
            if (existing.id == entry.id) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            result.append(entry);
        }
    }
    return result;
}

/**
 * @brief 草稿条目 → JSON
 */
QJsonObject draftToJson(const DraftEntry &entry)
{
    QJsonObject object;
    object.insert(QLatin1String(kDraftIdKey), entry.id);
    object.insert(QLatin1String(kDraftTitleKey), entry.title);
    object.insert(QLatin1String(kDraftBodyKey), entry.body);
    object.insert(QLatin1String(kDraftWorkspaceKey), entry.workspace);
    object.insert(QLatin1String(kDraftSavePathKey), entry.savePath);
    object.insert(QLatin1String(kDraftCreatedAtKey),
                  double(entry.createdAt));
    object.insert(QLatin1String(kDraftUpdatedAtKey),
                  double(entry.updatedAt));
    return object;
}

/**
 * @brief 从 JSON 数组还原快照列表（读侧容错）
 *
 * 快照属于「哪条草稿」必须有效；reason 只收已知取值（磁盘格式），
 * 未知值落 "manual"（显示上它就是一版，不值得为一串不认识的原因红脸）。
 */
QList<DraftSnapshot> snapshotsFromJson(const QJsonArray &array)
{
    QList<DraftSnapshot> result;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        DraftSnapshot snapshot;
        snapshot.draftId =
            object.value(QLatin1String(kSnapshotDraftIdKey)).toString();
        if (snapshot.draftId.isEmpty()) {
            continue;
        }
        snapshot.body =
            object.value(QLatin1String(kSnapshotBodyKey)).toString();
        snapshot.reason =
            object.value(QLatin1String(kSnapshotReasonKey)).toString();
        snapshot.createdAt = static_cast<qint64>(
            object.value(QLatin1String(kSnapshotCreatedAtKey))
                .toDouble(0));
        snapshot.charCount =
            object.value(QLatin1String(kSnapshotCharCountKey)).toInt(
                snapshot.body.size());
        result.append(snapshot);
    }
    // 磁盘形态约定新在前；防御手改后的乱序（回滚按「最近」语义取第一条）。
    std::sort(result.begin(), result.end(),
              [](const DraftSnapshot &a, const DraftSnapshot &b) {
                  return a.createdAt > b.createdAt;
              });
    return result;
}

/**
 * @brief 快照 → JSON
 */
QJsonObject snapshotToJson(const DraftSnapshot &snapshot)
{
    QJsonObject object;
    object.insert(QLatin1String(kSnapshotDraftIdKey), snapshot.draftId);
    object.insert(QLatin1String(kSnapshotBodyKey), snapshot.body);
    object.insert(QLatin1String(kSnapshotReasonKey), snapshot.reason);
    object.insert(QLatin1String(kSnapshotCreatedAtKey),
                  double(snapshot.createdAt));
    object.insert(QLatin1String(kSnapshotCharCountKey),
                  snapshot.charCount);
    return object;
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
 * @brief 首行当标题（迁移与创建草稿共用的小助手）
 *
 * 去掉行首 markdown 记号（#、-、*、>、`）与首尾空白，截到 60 字符。
 * 空行给空串（调用方再兜底 "Untitled"）。
 */
QString firstLineTitle(const QString &body)
{
    const QString firstLine =
        body.section(QLatin1Char('\n'), 0, 0).trimmed();
    QString cleaned;
    for (const QChar &ch : firstLine) {
        if (ch == QLatin1Char('#') || ch == QLatin1Char('-')
            || ch == QLatin1Char('*') || ch == QLatin1Char('>')
            || ch == QLatin1Char('`')) {
            continue;
        }
        cleaned.append(ch);
    }
    cleaned = cleaned.trimmed();
    if (cleaned.size() > 60) {
        cleaned = cleaned.left(60).trimmed() + QStringLiteral("…");
    }
    return cleaned;
}

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
 *
 * **旧 draft 键的读侧容错（规划 07 §2.2，替代迁移代码）**：`drafts` 不
 * 存在或为空、而旧 `draft` 键非空时，把它读成一条 id 固定 "default" 的
 * 草稿（工作区绑定为 current）。此后 `draft` 键继续作为「当前草稿正文」
 * 的镜像被写侧同步——旧版本程序降级读时只认它，不丢内容。
 */
void ToolsStore::load()
{
    m_workspaces.clear();
    m_current.clear();
    m_draft.clear();
    m_ribbon = RibbonSettings();
    m_editor = EditorSettings();
    m_disabledChecks.clear();
    m_drafts.clear();
    m_activeDraftId.clear();
    m_snapshots.clear();

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
    m_editor = editorFromJson(
        root.value(QLatin1String(kEditorKey)).toObject());
    // checks 段读侧：只收「已关闭」的规则 id 字符串，去重保序。
    const QJsonArray disabled = root.value(QLatin1String(kChecksKey))
            .toObject().value(QLatin1String(kChecksDisabledKey)).toArray();
    for (const QJsonValue &value : disabled) {
        if (value.isString() && !value.toString().isEmpty()
            && !m_disabledChecks.contains(value.toString())) {
            m_disabledChecks.append(value.toString());
        }
    }
    m_favorites = favoritesFromJson(
        root.value(QLatin1String(kFavoritesKey)).toArray());
    m_drafts = draftsFromJson(
        root.value(QLatin1String(kDraftsKey)).toArray());
    m_activeDraftId =
        root.value(QLatin1String(kActiveDraftIdKey)).toString();
    m_snapshots = snapshotsFromJson(
        root.value(QLatin1String(kHistoryKey)).toArray());

    // 历史数据里 current 可能指向已删除的项；容忍并保留原值，
    // 是否可用由 ToolsFacade 决定（不存在的工作区不给文件树设根）。
    if (!m_workspaces.contains(m_current)) {
        m_current.clear();
    }

    // 旧 draft 键 → 一条默认草稿（读侧容错，只在 drafts 为空时发生）。
    // 空 body 不迁移：一条空草稿没有信息量，别在磁盘上立「default」的牌。
    if (m_drafts.isEmpty() && !m_draft.isEmpty()) {
        DraftEntry legacy;
        legacy.id = QLatin1String(kLegacyDraftId);
        legacy.title = firstLineTitle(m_draft);
        legacy.body = m_draft;
        legacy.workspace = m_current;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        legacy.createdAt = now;
        legacy.updatedAt = now;
        m_drafts.append(legacy);
        m_activeDraftId = legacy.id;
    }

    // activeDraftId 的不变量：指向真实存在的一项；否则回退第一项，
    // 一条都没有时空串（「无草稿」是合法态，界面给新建引导）。
    if (!m_activeDraftId.isEmpty()) {
        bool found = false;
        for (const DraftEntry &entry : std::as_const(m_drafts)) {
            if (entry.id == m_activeDraftId) {
                found = true;
                break;
            }
        }
        if (!found) {
            m_activeDraftId.clear();
        }
    }
    if (m_activeDraftId.isEmpty() && !m_drafts.isEmpty()) {
        m_activeDraftId = m_drafts.constFirst().id;
    }
    // 草稿存在而 active 为空的情况上面已回退；镜像键此时对不上号也没
    // 关系——写侧（setDraftBody/syncDraftMirror）会把它对齐。
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
 * @brief 覆盖草稿（**当前草稿**的镜像写侧）并立即写盘
 *
 * M4 起 `draft` 键是当前草稿的镜像：写它 = 写当前草稿的 body。没有
 * 草稿时静默建一条 "default"（与 load() 的旧键容错对偶——理论上走不到
 * 这里，UI 总是先建草稿；防的是测试与手写调用）。
 *
 * @param text 草稿全文
 * @return 落盘是否成功；内容没变时直接返回 true（不写盘）
 */
bool ToolsStore::setDraft(const QString &text)
{
    if (m_activeDraftId.isEmpty()) {
        DraftEntry fallback;
        fallback.id = QLatin1String(kLegacyDraftId);
        fallback.title = firstLineTitle(text);
        fallback.body = text;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        fallback.createdAt = now;
        fallback.updatedAt = now;
        m_drafts.append(fallback);
        m_activeDraftId = fallback.id;
        // 新建的 fallback body 已经等于 text：setDraftBody 会视作
        // 「没变化」短路，所以这里必须自己落一次盘（草稿的诞生本身
        // 就是一次结构写盘）。
        syncDraftMirror();
        return save();
    }
    return setDraftBody(m_activeDraftId, text);
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

// --- 草稿集（M4） ---------------------------------------------------------

/**
 * @brief 按 id 取一条草稿
 */
DraftEntry ToolsStore::draftById(const QString &id) const
{
    for (const DraftEntry &entry : m_drafts) {
        if (entry.id == id) {
            return entry;
        }
    }
    return DraftEntry();
}

/**
 * @brief 生成一个未占用的草稿 id
 */
QString ToolsStore::allocateDraftId() const
{
    for (int seq = m_docSeq;; ++seq) {
        const QString candidate =
                QStringLiteral("d%1").arg(seq + 1);
        if (draftById(candidate).id.isEmpty()) {
            return candidate;
        }
    }
}

/**
 * @brief 新建一条草稿并设为当前
 *
 * 标题空时给 "Untitled N"（N 取首个未占用的编号，1 起）。超过上限
 * **拒绝**而不是静默淘汰：草稿是用户资产，砍谁的轮不到程序决定。
 */
QString ToolsStore::createDraft(const QString &title, const QString &body,
                                const QString &workspace, QString *rejected)
{
    if (m_drafts.size() >= kMaxDrafts) {
        if (rejected) {
            *rejected = QStringLiteral("draft-limit");
        }
        return QString();
    }
    DraftEntry entry;
    entry.id = allocateDraftId();
    if (title.trimmed().isEmpty()) {
        // Untitled N：N 与 id 序号无关——用户看到的是「第几条未命名」，
        // 从 1 数到第一个未占用的名字。
        for (int n = 1;; ++n) {
            const QString candidate =
                    QStringLiteral("Untitled %1").arg(n);
            bool taken = false;
            for (const DraftEntry &existing : std::as_const(m_drafts)) {
                if (existing.title == candidate) {
                    taken = true;
                    break;
                }
            }
            if (!taken) {
                entry.title = candidate;
                break;
            }
        }
    }
    else {
        entry.title = title.trimmed();
    }
    entry.body = body;
    entry.workspace = workspace;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    entry.createdAt = now;
    entry.updatedAt = now;
    m_drafts.append(entry);
    m_activeDraftId = entry.id;
    syncDraftMirror();
    const bool saved = save();
    if (!saved && rejected) {
        // 落盘失败不是「拒绝」，但调用方需要知道；用与上限不同的哨兵。
        *rejected = QStringLiteral("write-failed");
    }
    return saved ? entry.id : entry.id;
}

/**
 * @brief 整条覆盖（标题/工作区绑定）并刷新 updatedAt
 */
bool ToolsStore::updateDraft(const QString &id, const QString &title,
                             const QString &workspace)
{
    for (DraftEntry &entry : m_drafts) {
        if (entry.id == id) {
            entry.title = title.trimmed();
            entry.workspace = workspace;
            entry.updatedAt = QDateTime::currentMSecsSinceEpoch();
            if (entry.id == m_activeDraftId) {
                syncDraftMirror();
            }
            return save();
        }
    }
    return false;
}

/**
 * @brief 草稿正文回写（编辑防抖的落点）
 *
 * 只动 body 与 updatedAt；不变的内容不写盘。
 */
bool ToolsStore::setDraftBody(const QString &id, const QString &body)
{
    for (DraftEntry &entry : m_drafts) {
        if (entry.id == id) {
            if (entry.body == body) {
                return true;
            }
            entry.body = body;
            entry.updatedAt = QDateTime::currentMSecsSinceEpoch();
            if (entry.id == m_activeDraftId) {
                syncDraftMirror();
            }
            return save();
        }
    }
    return false;
}

/**
 * @brief 重命名（只改标题）
 */
bool ToolsStore::renameDraft(const QString &id, const QString &title)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    for (DraftEntry &entry : m_drafts) {
        if (entry.id == id) {
            entry.title = trimmed;
            entry.updatedAt = QDateTime::currentMSecsSinceEpoch();
            if (entry.id == m_activeDraftId) {
                syncDraftMirror();
            }
            return save();
        }
    }
    return false;
}

/**
 * @brief 记下草稿上次保存到的 Markdown 文件
 *
 * 只影响「是否已保存过」与下次存盘的默认落点，不动正文与标题。路径原样
 * 存储（调用方已归一为绝对路径）。
 */
bool ToolsStore::setDraftSavePath(const QString &id, const QString &path)
{
    for (DraftEntry &entry : m_drafts) {
        if (entry.id == id) {
            if (entry.savePath == path) {
                return true;
            }
            entry.savePath = path;
            entry.updatedAt = QDateTime::currentMSecsSinceEpoch();
            return save();
        }
    }
    return false;
}

/**
 * @brief 删除一条草稿（连同快照）
 *
 * 删的是当前草稿时顺延队首（空则无草稿）。恢复副本若恰好指向它，
 * 直接作废（下次写恢复副本会覆盖）。
 */
bool ToolsStore::removeDraft(const QString &id)
{
    for (int i = 0; i < m_drafts.size(); ++i) {
        if (m_drafts.at(i).id != id) {
            continue;
        }
        m_drafts.removeAt(i);
        for (int j = m_snapshots.size() - 1; j >= 0; --j) {
            if (m_snapshots.at(j).draftId == id) {
                m_snapshots.removeAt(j);
            }
        }
        if (m_activeDraftId == id) {
            m_activeDraftId = m_drafts.isEmpty()
                    ? QString() : m_drafts.constFirst().id;
        }
        syncDraftMirror();
        return save();
    }
    return false;
}

/**
 * @brief 设当前草稿（并同步 `draft` 镜像键）
 */
bool ToolsStore::setActiveDraft(const QString &id)
{
    if (draftById(id).id.isEmpty()) {
        return false;
    }
    if (m_activeDraftId == id) {
        return true;
    }
    m_activeDraftId = id;
    syncDraftMirror();
    return save();
}

// --- 历史快照（M4） -------------------------------------------------------

/**
 * @brief 某条草稿的快照（新在前）
 */
QList<DraftSnapshot> ToolsStore::snapshots(const QString &draftId) const
{
    QList<DraftSnapshot> result;
    for (const DraftSnapshot &snapshot : m_snapshots) {
        if (snapshot.draftId == draftId) {
            result.append(snapshot);
        }
    }
    return result;
}

/**
 * @brief 存一版快照（新在队首；上限淘汰最旧）
 *
 * 与上一版内容相同则跳过（连点两次「存一版」不该产生两条一样的记录）。
 */
bool ToolsStore::addSnapshot(const QString &draftId, const QString &body,
                             const QString &reason)
{
    if (draftById(draftId).id.isEmpty()) {
        return false;
    }
    // 上一版一样：跳过。
    for (const DraftSnapshot &snapshot : m_snapshots) {
        if (snapshot.draftId == draftId
            && snapshot.body == body) {
            return true;
        }
    }
    DraftSnapshot snapshot;
    snapshot.draftId = draftId;
    snapshot.body = body;
    snapshot.reason = reason;
    snapshot.createdAt = QDateTime::currentMSecsSinceEpoch();
    snapshot.charCount = body.size();
    m_snapshots.prepend(snapshot);
    trimSnapshots();
    return save();
}

/**
 * @brief 删除一版快照
 */
bool ToolsStore::removeSnapshot(const QString &draftId, qint64 createdAt)
{
    for (int i = 0; i < m_snapshots.size(); ++i) {
        if (m_snapshots.at(i).draftId == draftId
            && m_snapshots.at(i).createdAt == createdAt) {
            m_snapshots.removeAt(i);
            return save();
        }
    }
    return false;
}

// --- 崩溃恢复副本（M4，D6） -----------------------------------------------

/**
 * @brief 取恢复副本文件的路径
 */
QString ToolsStore::recoveryFilePath() const
{
    return m_dataRoot + QStringLiteral("/tools_recovery.json");
}

/**
 * @brief 读恢复副本
 *
 * 文件不存在（上次正常退出，或从未编辑过）得到 present=false；损坏按
 * 不存在处理——恢复副本救的是「大概丢了」，救不回就当作本来没有。
 */
ToolsStore::RecoveryInfo ToolsStore::recovery() const
{
    RecoveryInfo info;
    const QJsonObject root =
        core::JsonStore::readFile(recoveryFilePath());
    if (root.isEmpty()) {
        return info;
    }
    info.present = true;
    info.draftId =
        root.value(QLatin1String(kRecoveryDraftIdKey)).toString();
    info.body = root.value(QLatin1String(kRecoveryBodyKey)).toString();
    info.updatedAt = static_cast<qint64>(
        root.value(QLatin1String(kRecoveryUpdatedAtKey)).toDouble(0));
    return info;
}

/**
 * @brief 落一份恢复副本
 *
 * draftId 为空 = 删除副本（清空草稿的场合）。JsonStore 的原子写让它
 * 在编辑期间频繁落盘也是安全的。
 */
bool ToolsStore::writeRecovery(const QString &draftId,
                               const QString &body)
{
    if (draftId.isEmpty()) {
        QFile file(recoveryFilePath());
        return !file.exists() || file.remove();
    }
    QJsonObject root;
    root.insert(QLatin1String(kRecoveryDraftIdKey), draftId);
    root.insert(QLatin1String(kRecoveryBodyKey), body);
    root.insert(QLatin1String(kRecoveryUpdatedAtKey),
                double(QDateTime::currentMSecsSinceEpoch()));
    return core::JsonStore::writeFile(recoveryFilePath(), root).ok;
}

/**
 * @brief 删除恢复副本（正常退出时调用）
 */
bool ToolsStore::clearRecovery()
{
    QFile file(recoveryFilePath());
    return !file.exists() || file.remove();
}

/**
 * @brief `draft` 镜像键的同步
 *
 * 旧版程序降级读 tools.json 时只认 `draft` 键；它必须始终等于**当前
 * 草稿**的正文（无草稿时空串）。写侧的三个入口（setDraftBody /
 * setActiveDraft / removeDraft）都路过这里。
 */
void ToolsStore::syncDraftMirror()
{
    const DraftEntry active = draftById(m_activeDraftId);
    m_draft = active.id.isEmpty() ? QString() : active.body;
}

/**
 * @brief 快照列表按上限裁剪
 */
void ToolsStore::trimSnapshots()
{
    // m_snapshots 全局新在前。每草稿只保留**最新** kMaxSnapshotsPerDraft
    // 条：从头（新）往下数，超出上限的（更旧的）整段丢弃。草稿删除时它
    // 的快照整段拿掉（removeDraft），孤儿快照也在下面顺手清。
    for (const DraftEntry &entry : std::as_const(m_drafts)) {
        int seen = 0;
        for (int i = 0; i < m_snapshots.size(); ++i) {
            if (m_snapshots.at(i).draftId != entry.id) {
                continue;
            }
            ++seen;
            if (seen > kMaxSnapshotsPerDraft) {
                m_snapshots.removeAt(i);
                --i;   // 移除后回退一格，继续扫同一位
            }
        }
    }
    // 孤儿快照（草稿已删而快照残留——手改文件可造出这状态）顺手清掉。
    for (int i = m_snapshots.size() - 1; i >= 0; --i) {
        if (draftById(m_snapshots.at(i).draftId).id.isEmpty()) {
            m_snapshots.removeAt(i);
        }
    }
}

/**
 * @brief 覆盖编辑器偏好并立即写盘
 * @param editor 新的偏好值
 * @return 落盘是否成功；内容没变时直接返回 true（不写盘）
 */
bool ToolsStore::setEditor(const EditorSettings &editor)
{
    if (m_editor.findCaseSensitive == editor.findCaseSensitive
        && m_editor.findWholeWords == editor.findWholeWords
        && m_editor.contextWindowTokens == editor.contextWindowTokens) {
        return true;
    }
    m_editor = editor;
    return save();
}

/**
 * @brief 整组覆盖体检规则的关闭集合并立即写盘
 * @param ids 已关闭的规则 id 列表（其余视为开启）
 * @return 落盘是否成功；集合没变时直接返回 true（不写盘）
 */
bool ToolsStore::setDisabledCheckIds(const QStringList &ids)
{
    if (m_disabledChecks == ids) {
        return true;
    }
    m_disabledChecks = ids;
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
    QJsonObject ribbon;
    ribbon[QLatin1String(kRibbonCollapsedKey)] = m_ribbon.collapsed;
    ribbon[QLatin1String(kRibbonStyleKey)] = m_ribbon.style;
    ribbon[QLatin1String(kRibbonFollowThemeKey)] = m_ribbon.followAppTheme;
    ribbon[QLatin1String(kRibbonLastTabKey)] = m_ribbon.lastTabIndex;
    root[QLatin1String(kRibbonKey)] = ribbon;
    QJsonObject editor;
    editor[QLatin1String(kEditorCaseKey)] = m_editor.findCaseSensitive;
    editor[QLatin1String(kEditorWholeWordsKey)] = m_editor.findWholeWords;
    editor[QLatin1String(kEditorContextWindowKey)] =
        m_editor.contextWindowTokens;
    root[QLatin1String(kEditorKey)] = editor;
    QJsonObject checks;
    checks[QLatin1String(kChecksDisabledKey)] =
        QJsonArray::fromStringList(m_disabledChecks);
    root[QLatin1String(kChecksKey)] = checks;
    QJsonArray favorites;
    for (const FavoriteEntry &entry : std::as_const(m_favorites)) {
        favorites.append(favoriteToJson(entry));
    }
    root[QLatin1String(kFavoritesKey)] = favorites;
    QJsonArray drafts;
    for (const DraftEntry &entry : std::as_const(m_drafts)) {
        drafts.append(draftToJson(entry));
    }
    root[QLatin1String(kDraftsKey)] = drafts;
    root[QLatin1String(kActiveDraftIdKey)] = m_activeDraftId;
    QJsonArray history;
    for (const DraftSnapshot &snapshot : std::as_const(m_snapshots)) {
        history.append(snapshotToJson(snapshot));
    }
    root[QLatin1String(kHistoryKey)] = history;
    return core::JsonStore::writeFile(storeFilePath(), root).ok;
}

} // namespace awb::tools
