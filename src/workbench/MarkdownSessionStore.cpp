#include "workbench/MarkdownSessionStore.h"

#include "core/JsonStore.h"
#include "core/Logging.h"

#include <QJsonArray>
#include <QJsonObject>

namespace awb::workbench {

namespace {

// markdown_session.json 的键名。会话是「找回上次开着的」的全部语义：
// 换键名等于丢一次现场，与 tools.json 同一条纪律。
constexpr auto kTabsKey = "tabs";
constexpr auto kActiveIndexKey = "activeIndex";
constexpr auto kPresetKey = "preset";
constexpr auto kPromptLeftOpenKey = "promptLeftOpen";
constexpr auto kDocumentLeftOpenKey = "documentLeftOpen";
constexpr auto kDocumentRibbonKey = "documentRibbonCollapsed";
constexpr auto kRecentKey = "recentDocuments";

constexpr auto kTabPathKey = "path";
constexpr auto kTabModeKey = "mode";

/**
 * @brief 单个 tab 条目的读侧容错
 *
 * 路径不是字符串、模式不认识，都跳过该条——会话里坏一个 tab 不该拖垮
 * 整组恢复。
 */
bool tabFromJson(const QJsonObject &object, MarkdownSessionState::Tab *out)
{
    const QString path = object.value(QLatin1String(kTabPathKey)).toString();
    if (path.isEmpty()) {
        return false;
    }
    out->path = path;
    const QString mode = object.value(QLatin1String(kTabModeKey)).toString();
    out->mode = (mode == QLatin1String("preview")
                 || mode == QLatin1String("split")
                 || mode == QLatin1String("source"))
            ? mode : QStringLiteral("preview");
    return true;
}

} // namespace

/**
 * @brief 构造会话存储
 *
 * @param dataRoot 应用数据根（<dataRoot>/markdown_session.json）
 */
MarkdownSessionStore::MarkdownSessionStore(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
}

/**
 * @brief 会话文件的绝对路径
 */
QString MarkdownSessionStore::filePath() const
{
    return m_dataRoot + QStringLiteral("/markdown_session.json");
}

/**
 * @brief 读会话
 *
 * 文件缺失 = 首次运行，默认态。损坏（JsonStore 记警告并给空对象）或字段
 * 畸形 = 布局状态不值得抢救，整体作废重来；上限与类型在每一层都夹住，
 * 会话文件被手改出怪东西也不至于把窗口开成奇怪的样子。
 */
MarkdownSessionState MarkdownSessionStore::load() const
{
    MarkdownSessionState state;
    const QJsonObject root = core::JsonStore::readFile(filePath());
    if (root.isEmpty()) {
        return state;
    }

    const QJsonArray tabs = root.value(QLatin1String(kTabsKey)).toArray();
    for (const QJsonValue &value : tabs) {
        MarkdownSessionState::Tab tab;
        if (tabFromJson(value.toObject(), &tab)) {
            state.tabs.append(tab);
        }
    }

    const int active = root.value(QLatin1String(kActiveIndexKey)).toInt(-1);
    state.activeIndex = (active >= 0 && active < state.tabs.size())
            ? active : -1;

    const QString preset = root.value(QLatin1String(kPresetKey)).toString();
    state.preset = (preset == QLatin1String("document"))
            ? QStringLiteral("document") : QStringLiteral("prompt");
    state.promptLeftOpen =
        root.value(QLatin1String(kPromptLeftOpenKey)).toBool(true);
    state.documentLeftOpen =
        root.value(QLatin1String(kDocumentLeftOpenKey)).toBool(false);
    state.documentRibbonCollapsed =
        root.value(QLatin1String(kDocumentRibbonKey)).toBool(true);

    const QJsonArray recent =
        root.value(QLatin1String(kRecentKey)).toArray();
    for (const QJsonValue &value : recent) {
        if (value.isString() && !value.toString().isEmpty()) {
            state.recentDocuments.append(value.toString());
        }
        if (state.recentDocuments.size() >= kMaxRecentDocuments) {
            break;
        }
    }
    return state;
}

/**
 * @brief 原子写会话
 *
 * @param state 现场快照
 * @return 落盘是否成功
 */
bool MarkdownSessionStore::save(const MarkdownSessionState &state) const
{
    QJsonObject root;
    QJsonArray tabs;
    for (const MarkdownSessionState::Tab &tab : state.tabs) {
        QJsonObject entry;
        entry.insert(QLatin1String(kTabPathKey), tab.path);
        entry.insert(QLatin1String(kTabModeKey), tab.mode);
        tabs.append(entry);
    }
    root.insert(QLatin1String(kTabsKey), tabs);
    root.insert(QLatin1String(kActiveIndexKey), state.activeIndex);
    root.insert(QLatin1String(kPresetKey), state.preset);
    root.insert(QLatin1String(kPromptLeftOpenKey), state.promptLeftOpen);
    root.insert(QLatin1String(kDocumentLeftOpenKey), state.documentLeftOpen);
    root.insert(QLatin1String(kDocumentRibbonKey),
                state.documentRibbonCollapsed);
    root.insert(QLatin1String(kRecentKey),
                QJsonArray::fromStringList(state.recentDocuments));
    return core::JsonStore::writeFile(filePath(), root).ok;
}

/**
 * @brief 把一个路径触到 MRU 队首
 *
 * @param recent 既有队列（新在前）
 * @param path 这次打开的文档路径
 * @return 新队列：去重后队首是 path，长度不超过 kMaxRecentDocuments
 */
QStringList MarkdownSessionStore::touchRecent(const QStringList &recent,
                                              const QString &path)
{
    if (path.isEmpty()) {
        return recent;
    }
    QStringList next;
    next.reserve(recent.size() + 1);
    next.append(path);
    for (const QString &entry : recent) {
        if (entry != path) {
            next.append(entry);
        }
    }
    while (next.size() > kMaxRecentDocuments) {
        next.removeLast();
    }
    return next;
}

} // namespace awb::workbench
