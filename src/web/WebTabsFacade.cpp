#include "web/WebTabsFacade.h"

#include "core/Paths.h"
#include "core/Settings.h"
#include "web/WebSurfaceRegistry.h"
#include "web/WebTab.h"
#include "web/WebTabsModel.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QStringList>
#include <QUrl>
#include <utility>

namespace awb::web {

namespace {

/**
 * @brief 生成 URL 的可展示形式（抹掉 bearer token）
 *
 * token 绝不能进日志或用户可见的 toast——凡第三方可能读到的地方都必须
 * 先过这里。两种拼写都处理：`#token=…` 片段（qwen 的门禁）与 `?token=…`
 * 查询项（dsh 的会话 URL）。其余片段与查询项原样保留。
 *
 * @param url 原始 URL（可含 token）
 * @return 脱敏后的 URL 文本，可直接写入日志或 toast
 */
QString redactedUrl(const QUrl &url)
{
    QString text = url.toString(QUrl::RemoveFragment);

    // 去掉 token= 查询项，查询串的其余部分保留。
    const int queryStart = text.indexOf(QLatin1Char('?'));
    if (queryStart >= 0) {
        QStringList kept;
        for (const QString &part : text.mid(queryStart + 1).split(QLatin1Char('&'))) {
            if (part.startsWith(QStringLiteral("token="))) {
                continue;
            }
            kept.append(part);
        }
        text = text.left(queryStart);
        if (!kept.isEmpty()) {
            text += QLatin1Char('?') + kept.join(QLatin1Char('&'));
        }
    }

    const QString fragment = url.fragment();
    if (fragment.isEmpty()) {
        return text;
    }

    // 只丢 token= 这一段；片段里的正常文本保留。
    QStringList parts;
    bool droppedToken = false;
    for (const QString &part : fragment.split(QLatin1Char('&'))) {
        if (part.startsWith(QStringLiteral("token="))) {
            droppedToken = true;
            continue;
        }
        parts.append(part);
    }
    if (!droppedToken) {
        text += QLatin1Char('#') + fragment;
    }
    else if (!parts.isEmpty()) {
        text += QLatin1Char('#') + parts.join(QLatin1Char('&'));
    }
    return text;
}
} // namespace

/**
 * @brief 构造 Web 门面
 *
 * 创建标签模型与表面注册表（均为本类子对象），并接通 activeTabChanged /
 * activeStateChanged / tabCountChanged 的转发与设置变化监听。
 *
 * @param settings 设置对象（webOptions 的来源），须已就绪
 * @param parent QObject 父项
 */
WebTabsFacade::WebTabsFacade(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_tabs(new WebTabsModel(this))
    , m_registry(new WebSurfaceRegistry(this))
{
    wireActiveTracking();

    // tabCount 靠模型的结构变化驱动——QML 绑定依赖不了 rowCount()，
    // 它没有 NOTIFY。
    connect(m_tabs, &QAbstractItemModel::rowsInserted, this,
            &WebTabsFacade::tabCountChanged);
    connect(m_tabs, &QAbstractItemModel::rowsRemoved, this,
            &WebTabsFacade::tabCountChanged);

    connect(settings, &core::Settings::valueChanged, this,
            [this](const QString &key) {
                if (key == QStringLiteral("web.freezeInactiveTabs")
                    || key == QStringLiteral("web.downloadDir")
                    || key == QStringLiteral("web.homeUrl")) {
                    Q_EMIT policyChanged();
                }
                // 上限调低必须立即生效，而不是等下一次开标签。
                else if (key == QStringLiteral("web.maxLiveTabs")) {
                    applyMemoryPolicy();
                }
            });
}

/**
 * @brief 取暴露给 QML 的标签模型
 *
 * @return 模型的 QAbstractItemModel 接口
 */
QAbstractItemModel *WebTabsFacade::model() const
{
    return m_tabs;
}

/**
 * @brief 取激活标签的 id
 *
 * @return 激活标签 id；无激活标签时返回空串
 */
QString WebTabsFacade::activeTabId() const
{
    return m_tabs->activeTabId();
}

/**
 * @brief 取当前标签数
 *
 * @return 模型行数
 */
int WebTabsFacade::tabCount() const
{
    return m_tabs->rowCount();
}

/**
 * @brief 取激活标签的状态机取值
 *
 * @return 激活标签的 state；无激活标签时返回空串
 */
QString WebTabsFacade::activeState() const
{
    const WebTab *tab = tabForId(activeTabId());
    return tab ? tab->state() : QString();
}

/**
 * @brief 取 id 对应的标签对象（QML 动态属性读取用）
 *
 * 返回活的 QObject 指针，QML 对 zoom、state 等 Q_PROPERTY 的读取因此
 * 保持响应式（快照式 QVariantMap 做不到）。
 *
 * @param id 标签 id
 * @return 对应的 WebTab；标签已不存在时返回 nullptr
 */
QObject *WebTabsFacade::tabObject(const QString &id) const
{
    return tabForId(id);
}

/**
 * @brief 查询是否启用 DevTools 入口
 *
 * @return 仅 Debug 构建（QT_DEBUG）返回 true；Release 不提供入口
 */
bool WebTabsFacade::devToolsEnabled() const
{
#ifdef QT_DEBUG
    return true;
#else
    return false;
#endif
}

/**
 * @brief 按 id 取活标签
 *
 * @param id 标签 id
 * @return 对应的 WebTab；不存在时返回 nullptr
 */
WebTab *WebTabsFacade::tabForId(const QString &id) const
{
    return m_tabs->tabById(id);
}

/**
 * @brief 登记一个表面组件
 *
 * 纯转发到 WebSurfaceRegistry；awb_web_webengine 启动时为 "embedded"
 * 调用，workbench 的插件服务也经它登记。
 *
 * @param kind 表面标识
 * @param componentUrl 该表面页面的 QML 组件 URL
 */
void WebTabsFacade::registerSurface(const QString &kind,
                                    const QString &componentUrl)
{
    m_registry->registerSurface(kind, componentUrl);
}

/**
 * @brief 查询表面 kind 的 QML 组件 URL
 *
 * @param kind 表面标识
 * @return 组件 URL；kind 未注册时返回空串
 */
QString WebTabsFacade::surfaceUrl(const QString &kind) const
{
    return m_registry->surfaceUrl(kind);
}

/**
 * @brief 取该 agent 当前标签的快照
 *
 * @param agentId agent id
 * @return 含 id / agentId / url / title / state 的 QVariantMap；
 *         该 agent 未打开标签时返回空 map
 */
QVariantMap WebTabsFacade::tabForAgent(const QString &agentId) const
{
    QVariantMap map;
    const WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab) {
        return map;
    }
    map[QStringLiteral("id")] = tab->id();
    map[QStringLiteral("agentId")] = tab->agentId();
    map[QStringLiteral("url")] = tab->url().toString();
    map[QStringLiteral("title")] = tab->title();
    map[QStringLiteral("state")] = tab->state();
    return map;
}

/**
 * @brief 打开（或激活）一个 agent 的标签
 *
 * 表面策略：`external` 把 URL 交给系统浏览器、不建标签（发
 * externalOpened，由 workbench 转 toast）；`embedded` 要求 WebEngine
 * 表面已注册，否则记一条警告并降级为 external。同一 agent 已有标签时
 * 激活它而不是再开一个。
 *
 * @param fields 标签字段：agentId、url、title、icon、color
 * @return 新建/激活的标签 id；URL 无效或走 external 路径时返回空串
 * @sa openDetachedTab
 */
QString WebTabsFacade::openTab(const QVariantMap &fields)
{
    const QString agentId = fields.value(QStringLiteral("agentId")).toString();
    const QUrl url(fields.value(QStringLiteral("url")).toString());
    if (!url.isValid() || url.isEmpty()) {
        return {};
    }

    // 表面策略：`external` 把 URL 交给系统浏览器、不建标签；`embedded`
    // 需要 WebEngine 表面已注册，否则同样降级为 external。
    QString kind = m_settings->webOptions().surface;
    if (kind == QStringLiteral("embedded")
        && !m_registry->hasSurface(QStringLiteral("embedded"))) {
        qWarning().noquote() << QStringLiteral(
            "WebTabs: the embedded surface is not available (built without "
            "WebEngine?); falling back to the system browser");
        kind = QStringLiteral("external");
    }
    if (kind == QStringLiteral("external")) {
        if (QDesktopServices::openUrl(url)) {
            qInfo().noquote() << QStringLiteral(
                "WebTabs: opened %1 in the system browser")
                                     .arg(redactedUrl(url));
            Q_EMIT externalOpened(redactedUrl(url));
        } else {
            qWarning().noquote() << QStringLiteral(
                "WebTabs: no handler accepted %1").arg(redactedUrl(url));
        }
        return {};
    }

    // 同一 agent 已有标签 -> 激活，而不是重复开。
    if (WebTab *existing = m_tabs->tabForAgent(agentId)) {
        activateTab(existing->id());
        qInfo().noquote() << QStringLiteral(
            "WebTabs: activated the existing tab %1 for %2")
            .arg(existing->id(), agentId);
        return existing->id();
    }

    const QString id = createTab(agentId, url, fields);
    qInfo().noquote() << QStringLiteral(
        "WebTabs: opened tab %1 for agent %2 (%3, surface=%4)")
        .arg(id, agentId, redactedUrl(url), kind);
    return id;
}

/**
 * @brief 创建并追加一个标签
 *
 * 生成 id、构造 WebTab、追加进模型并激活，最后施行内存策略（超上限的
 * 旧标签可能立刻被释放）。surfaceKind 恒为 "embedded"——external 路径
 * 在 openTab 里就分流了，不会走到这里。
 *
 * @param agentId 所属 agent 的 id
 * @param url 初始 URL
 * @param fields 标签字段（title / icon / color；缺失时取空值）
 * @return 新标签的 id
 */
QString WebTabsFacade::createTab(const QString &agentId, const QUrl &url,
                                 const QVariantMap &fields)
{
    const QString id = QStringLiteral("tab-%1").arg(m_nextTabId++);
    auto *tab = new WebTab(id, agentId, url,
                           fields.value(QStringLiteral("title")).toString(),
                           fields.value(QStringLiteral("icon")).toString(),
                           fields.value(QStringLiteral("color")).toString(),
                           QStringLiteral("embedded"), this);
    m_tabs->appendTab(tab);
    m_tabs->setActiveIndex(m_tabs->rowCount() - 1);
    tab->touch();
    applyMemoryPolicy();
    return id;
}

/**
 * @brief 无视同 agent 去重，直接开一个新标签
 *
 * 环回弹出窗口（OAuth、同一 agent 的 target=_blank）走这里：那种场景
 * 下 openTab 的去重会把弹窗错误地折进已有标签。仅在嵌入表面可用时开
 * 标签，否则返回空串（弹窗交给系统浏览器的逻辑在 QML 侧决定）。
 *
 * @param agentId 归属的 agent id（profile 与标签颜色按它取）
 * @param url 弹出窗口的 URL
 * @param title 标签标题（通常是 host）
 * @return 新标签的 id；URL 无效或嵌入表面不可用时返回空串
 * @sa openTab
 */
QString WebTabsFacade::openDetachedTab(const QString &agentId,
                                       const QString &url,
                                       const QString &title)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.isEmpty()) {
        return {};
    }
    if (m_settings->webOptions().surface != QStringLiteral("embedded")
        || !m_registry->hasSurface(QStringLiteral("embedded"))) {
        return {};
    }
    QVariantMap fields;
    fields[QStringLiteral("title")] = title;
    return createTab(agentId, parsed, fields);
}

/**
 * @brief 关闭一个标签
 *
 * 关闭会销毁视图，但 agent 进程照常运行——会话数据留在按 agent 区分的
 * profile 里，重开标签即恢复。关的是激活标签时把激活移到原本相邻的
 * 标签（原行号，越界取末行）。
 *
 * @param id 标签 id；未知时无效果
 */
void WebTabsFacade::closeTab(const QString &id)
{
    // 关闭销毁视图——agent 进程继续跑，会话数据留在按 agent 区分的
    // profile 里。
    if (m_tabs->activeTabId() == id) {
        const int row = m_tabs->rowOfTab(id);
        m_tabs->removeTab(id);
        if (m_tabs->rowCount() > 0) {
            m_tabs->setActiveIndex(qMin(row, m_tabs->rowCount() - 1));
        }
    } else {
        m_tabs->removeTab(id);
    }
    Q_EMIT activeTabChanged();
}

/**
 * @brief 激活一个标签
 *
 * @param id 标签 id；未知时无效果（也不发信号）
 */
void WebTabsFacade::activateTab(const QString &id)
{
    const int row = m_tabs->rowOfTab(id);
    if (row < 0) {
        return;
    }
    m_tabs->setActiveIndex(row);
    Q_EMIT activeTabChanged();
}

/**
 * @brief 循环切换激活标签
 *
 * @param delta 步进量（Ctrl+Tab 传 1、反向传 -1）；在 [0, count) 内回绕，
 *              无激活标签时从 0 开始；空列表时无效果
 */
void WebTabsFacade::stepActiveTab(int delta)
{
    const int count = m_tabs->rowCount();
    if (count == 0) {
        return;
    }
    int index = m_tabs->activeIndex();
    if (index < 0) {
        index = 0;
    }
    else {
        index = (index + delta + count) % count;
    }
    m_tabs->setActiveIndex(index);
    Q_EMIT activeTabChanged();
}

/**
 * @brief 重载一个标签
 *
 * released 态的标签先经 reopen() 重建视图；其余情况置回 loading、清零
 * 进度——表面监听状态迁移，loading 触发 (re)load。
 *
 * @param id 标签 id；未知时无效果
 * @sa reopen
 */
void WebTabsFacade::reloadTab(const QString &id)
{
    WebTab *tab = tabForId(id);
    if (!tab) {
        return;
    }
    if (tab->state() == QStringLiteral("released")) {
        reopen(id);
        return;
    }
    // 表面监听状态迁移：loading 触发 (re)load。
    tab->setState(QStringLiteral("loading"));
    tab->setLoadProgress(0);
}

/**
 * @brief 把标签的 URL 交给系统浏览器打开
 *
 * 嵌入式视图坏掉时的逃生口，因此无条件可用。浏览器需要 token 片段，
 * 日志行不能有——日志走 redactedUrl()，打开用原 URL。
 *
 * @param id 标签 id；未知时无效果
 */
void WebTabsFacade::openExternal(const QString &id)
{
    WebTab *tab = tabForId(id);
    if (!tab) {
        return;
    }
    qInfo().noquote() << QStringLiteral(
        "WebTabs: opened %1 in the system browser")
                             .arg(redactedUrl(tab->url()));
    QDesktopServices::openUrl(tab->url());
}

/**
 * @brief 恢复一个 released 标签
 *
 * @param id 标签 id；未知时无效果
 */
void WebTabsFacade::reopen(const QString &id)
{
    WebTab *tab = tabForId(id);
    if (!tab) {
        return;
    }
    tab->setLoadProgress(0);
    tab->setState(QStringLiteral("loading")); // 视图据此重建并加载
}

/**
 * @brief 表面回报：写入标签状态
 *
 * 状态进入 ready 时顺带 touch()——加载完成的标签算「刚用过」，
 * LRU 释放策略按此排序。
 *
 * @param id 标签 id；未知时无效果
 * @param state 新状态（WebEngineSurface.qml 的 loadingChanged 等处回报）
 */
void WebTabsFacade::setTabState(const QString &id, const QString &state)
{
    WebTab *tab = tabForId(id);
    if (!tab) {
        return;
    }
    tab->setState(state);
    if (state == QStringLiteral("ready")) {
        tab->touch();
    }
}

/**
 * @brief 表面回报：写入加载进度
 *
 * @param id 标签 id；未知时无效果
 * @param progress 加载进度（WebTab 侧夹到 0..100）
 */
void WebTabsFacade::setTabProgress(const QString &id, int progress)
{
    if (WebTab *tab = tabForId(id)) {
        tab->setLoadProgress(progress);
    }
}

/**
 * @brief 表面回报：写入标签标题
 *
 * @param id 标签 id；未知时无效果
 * @param title 页面回报的标题
 */
void WebTabsFacade::setTabTitle(const QString &id, const QString &title)
{
    if (WebTab *tab = tabForId(id)) {
        tab->setTitle(title);
    }
}

/**
 * @brief 表面回报：写入最近一次错误说明
 *
 * @param id 标签 id；未知时无效果
 * @param error 已翻译、可直接展示的文案
 */
void WebTabsFacade::setTabLastError(const QString &id, const QString &error)
{
    if (WebTab *tab = tabForId(id)) {
        tab->setLastError(error);
    }
}

/**
 * @brief 表面回报：写入缩放系数
 *
 * @param id 标签 id；未知时无效果
 * @param zoom 新倍率（WebTab 侧夹到 [0.5, 2.0]）
 */
void WebTabsFacade::setTabZoom(const QString &id, double zoom)
{
    if (WebTab *tab = tabForId(id)) {
        tab->setZoom(zoom);
    }
}

/**
 * @brief 表面回报：写入当前 URL
 *
 * onUrlChanged 回报——视图里的导航（重定向、页内跳转）据此同步回标签。
 *
 * @param id 标签 id；未知时无效果
 * @param url 视图当前 URL 的字符串形式
 */
void WebTabsFacade::setTabUrl(const QString &id, const QString &url)
{
    if (WebTab *tab = tabForId(id)) {
        tab->setUrl(QUrl(url));
    }
}

/**
 * @brief 健康检查：agent 掉线，其标签转 offline
 *
 * 由 BuiltinPages 在 runningChanged（true -> false）边沿调用。
 * ready / loading / error 三种状态转入 offline；其余状态保持不变。
 *
 * @param agentId agent id；未开标签时无效果
 * @sa markOnlineForAgent
 */
void WebTabsFacade::markOfflineForAgent(const QString &agentId)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab) {
        return;
    }
    if (tab->state() == QStringLiteral("ready")
        || tab->state() == QStringLiteral("loading")
        // agent 已掉线时看到的 error 页其实是「agent 离线」页：恢复
        // 走正常的 offline -> loading 路径。agent 仍在运行时的加载错误
        // （token 门禁的 HTTP 401）保持 error——每轮探测都重载它永远
        // 不会成功。
        || tab->state() == QStringLiteral("error")) {
        tab->setState(QStringLiteral("offline"));
    }
}

/**
 * @brief 健康检查：agent 恢复，其 offline 标签转 loading
 *
 * 由 BuiltinPages 在 runningChanged（false -> true）边沿调用。
 * 只有 offline 态转入 loading（进度清零，表面重新加载）；其余状态
 * 的取舍原因见下方软注释。
 *
 * @param agentId agent id；未开标签时无效果
 * @sa markOfflineForAgent
 */
void WebTabsFacade::markOnlineForAgent(const QString &agentId)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab) {
        return;
    }
    // offline + agent 回来 -> loading。released 标签保持 released，等
    // 用户手动恢复。error/crashed 不自动重载：agent 是活的（健康检查
    // 已通过），说明加载本身失败——只有 retarget 或手动 Retry 能改变
    // 结局。
    if (tab->state() == QStringLiteral("offline")) {
        tab->setLoadProgress(0);
        tab->setState(QStringLiteral("loading"));
    }
}

/**
 * @brief 把该 agent 的标签指向新 URL（会话 URL 重定向）
 *
 * 启动输出里捕获到带 token 的会话 URL 后由 BuiltinPages 调用：换 URL
 * 触发表面的 url 绑定重新导航；置回 loading 并清掉 lastError，error /
 * offline 覆盖层随之消失。released 标签也接受重定向——恢复时直接加载
 * 新 URL。
 *
 * @param agentId agent id；未开标签时无效果
 * @param url 新的会话 URL；空或无效时无效果
 */
void WebTabsFacade::retargetTabForAgent(const QString &agentId, const QUrl &url)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab || url.isEmpty() || !url.isValid()) {
        return;
    }
    tab->setLoadProgress(0);
    tab->setLastError(QString());
    tab->setUrl(url);
    if (tab->state() != QStringLiteral("loading")) {
        tab->setState(QStringLiteral("loading"));
    }
}

/**
 * @brief 关闭该 agent 的标签
 *
 * @param agentId agent id；未开标签时无效果
 */
void WebTabsFacade::closeTabsForAgent(const QString &agentId)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (tab) {
        closeTab(tab->id());
    }
}

/**
 * @brief 查询非激活标签是否冻结
 *
 * @return web.freezeInactiveTabs 的当前值
 */
bool WebTabsFacade::freezeInactiveTabs() const
{
    return m_settings->webOptions().freezeInactiveTabs;
}

/**
 * @brief 取下载目录
 *
 * @return web.downloadDir 配置值；未配置时回退到系统下载目录
 *         （core::Paths::downloadsDir()）
 */
QString WebTabsFacade::downloadDir() const
{
    const QString configured = m_settings->webOptions().downloadDir;
    if (!configured.isEmpty()) {
        return configured;
    }
    return core::Paths::downloadsDir();
}

/**
 * @brief 查询嵌入表面是否可用
 *
 * @return "embedded" 已注册时返回 true；AWB_ENABLE_WEBENGINE=OFF 的构建
 *         恒为 false
 */
bool WebTabsFacade::engineAvailable() const
{
    return m_registry->hasSurface(QStringLiteral("embedded"));
}

/**
 * @brief 取 Home URL 配置
 *
 * @return web.homeUrl；未配置时为空串（Web 页的 Home 按钮保持回到
 *         agent 列表的空态行为）
 */
QString WebTabsFacade::homeUrl() const
{
    return m_settings->webOptions().homeUrl;
}

/**
 * @brief 写 Home URL 并持久化
 *
 * 设置页输入框的提交入口。同值直接返回。值变化经 Settings 的
 * valueChanged 触发 policyChanged，QML 绑定据此刷新。
 *
 * @param url 新的 Home URL；空串 = 关闭（Home 回到 agent 列表）
 */
void WebTabsFacade::setHomeUrl(const QString &url)
{
    if (url == m_settings->webOptions().homeUrl) {
        return;
    }
    m_settings->setWebHomeUrl(url);
    m_settings->save();
}

/**
 * @brief 打开配置的 Home URL
 *
 * 以保留 agent id "home" 走 openTab：表面策略一致（external 交给系统
 * 浏览器），且同一 id 的既有标签被激活而不是重复开——反复按 Home 不会
 * 累积标签。标题初始为 "Home"，页面加载完成后由表面回报的页面标题
 * 覆盖。
 *
 * @return 打开/激活的标签 id；homeUrl 为空或 URL 无效时返回空串
 * @sa openTab
 */
QString WebTabsFacade::openHome()
{
    const QString url = m_settings->webOptions().homeUrl;
    if (url.isEmpty()) {
        return {};
    }
    QVariantMap fields;
    fields[QStringLiteral("agentId")] = QStringLiteral("home");
    fields[QStringLiteral("url")] = url;
    fields[QStringLiteral("title")] = tr("Home");
    return openTab(fields);
}

/**
 * @brief 接通激活标签的跟踪转发（构造时调用一次）
 *
 * 模型的 activeIndexChanged 同时转发为 facade 的 activeTabChanged 与
 * activeStateChanged；激活行上的任何 dataChanged（state、title、
 * progress…变化）也触发 activeStateChanged——QML 侧的 activeState
 * 绑定因此无需感知模型细节。
 */
void WebTabsFacade::wireActiveTracking()
{
    connect(m_tabs, &WebTabsModel::activeIndexChanged, this,
            &WebTabsFacade::activeTabChanged);
    connect(m_tabs, &WebTabsModel::activeIndexChanged, this,
            &WebTabsFacade::activeStateChanged);
    // 激活行上的任何 dataChanged 都意味着它的某个属性动了
    // （state、title、progress…）——QML 里的 activeState 需要重读。
    connect(m_tabs, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &topLeft, const QModelIndex &,
                   const QVector<int> &) {
                if (topLeft.row() == m_tabs->activeIndex()) {
                    Q_EMIT activeStateChanged();
                }
            });
}

/**
 * @brief 施行 LRU 释放策略，约束同时存活的视图数
 *
 * 超过 web.maxLiveTabs（下限 1）后，按 lastUsedMs 最旧的非激活标签被
 * 释放：视图销毁、标签保留（released 态，等用户恢复）。激活标签的视图
 * 也计入上限——上限约束的是**全部**活视图，所以循环停在总共 maxLive 个
 * 活视图，而不是 maxLive + 1。
 *
 * 不受 freezeInactiveTabs 门控：冻结开关只控制切走视图的 CPU 取舍，
 * 这条上限是内存约束，两种设置下都必须成立。开标签、标签转 ready 与
 * settings.json 里调低上限时都会走到这里。
 *
 * @sa WebTabsFacade::reopen
 */
void WebTabsFacade::applyMemoryPolicy()
{
    const int maxLive = qMax(1, m_settings->webOptions().maxLiveTabs);

    QList<WebTab *> releasable; // 活着但不激活——候选
    int liveCount = 0;          // 当前存在的全部视图
    for (int i = 0; i < m_tabs->rowCount(); ++i) {
        WebTab *tab = m_tabs->tabAt(i);
        if (tab->state() == QStringLiteral("released")) {
            continue;
        }
        ++liveCount;
        if (tab->id() != activeTabId()) {
            releasable.append(tab);
        }
    }
    while (liveCount > maxLive && !releasable.isEmpty()) {
        // 按 lastUsedMs 最旧的先释放（激活标签永远不会出现在候选里）。
        WebTab *oldest = nullptr;
        for (WebTab *tab : std::as_const(releasable)) {
            if (!oldest || tab->lastUsedMs() < oldest->lastUsedMs()) {
                oldest = tab;
            }
        }
        if (!oldest) {
            break;
        }
        oldest->setState(QStringLiteral("released"));
        releasable.removeAll(oldest);
        --liveCount;
    }
}

} // namespace awb::web
