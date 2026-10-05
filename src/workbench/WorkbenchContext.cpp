#include "workbench/WorkbenchContext.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentUrls.h"
#include "core/Settings.h"
#include "agentcatalog/AgentsFacade.h"
#include "markdown/MarkdownPreviewController.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/UiServices.h"
#include "web/WebTabsFacade.h"

#include <QCoreApplication>

namespace awb::workbench {

/**
 * @brief 构造跨域上下文
 *
 * 只把各域的指针接进来并转发当前页信号；页面注册与跨域规则布线
 * 在 BuiltinPages。
 *
 * @param nav 导航模型
 * @param ui 系统能力（剪贴板、打开 URL/目录）
 * @param notifications toast 通知
 * @param agents agent 门面
 * @param web Web 标签页门面
 * @param settings 设置
 * @param parent QObject 父项
 */
WorkbenchContext::WorkbenchContext(shell::NavigationModel *nav,
                                   shell::UiServices *ui,
                                   shell::Notifications *notifications,
                                   agentcatalog::AgentsFacade *agents,
                                   web::WebTabsFacade *web,
                                   core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_nav(nav)
    , m_ui(ui)
    , m_notifications(notifications)
    , m_agents(agents)
    , m_web(web)
    , m_settings(settings)
{
    connect(m_nav, &shell::NavigationModel::currentPageChanged, this,
            &WorkbenchContext::currentPageChanged);
}

/**
 * @brief 取当前页面 id（Q_PROPERTY 的 READ 侧）
 *
 * @return 当前页面 id；转发自 NavigationModel
 */
QString WorkbenchContext::currentPageId() const
{
    return m_nav->currentPageId();
}

/**
 * @brief 导航意图：切换到 id 对应的页面
 *
 * @param id 页面 id（NavigationModel 注册过的）
 */
void WorkbenchContext::showPage(const QString &id)
{
    m_nav->setCurrentPageId(id);
}

namespace {

/**
 * @brief 按 id 取 agent 定义所在行
 *
 * 各 openWeb 意图共用的前置校验。
 *
 * @param agents agent 门面
 * @param agentId agent id
 * @param out 成功时接收定义
 * @return 定义的行号；id 未知或 webUrl 为空时返回 -1
 */
int agentRow(const agentcatalog::AgentsFacade *agents, const QString &agentId,
             agentcatalog::AgentDefinition *out)
{
    const int row = agents->agentModel()->indexOf(agentId);
    if (row < 0 || row >= agents->agentModel()->definitions().size()) {
        return -1;
    }
    *out = agents->agentModel()->definitions().at(row);
    return out->webUrl.isEmpty() ? -1 : row;
}
} // namespace

/**
 * @brief 把 agent 的 WebUI 开成标签页（卡片「打开」的默认路径）
 *
 * URL 的取值优先级：启动输出里捕获的会话 URL > finalUrl()（配置了
 * tokenFile 时带 #token=<value> 片段）。裸 webUrl 不直接用——
 * token 门禁的 harness（dsh）对它回 401，只有打出来的每进程 URL 能过；
 * token 片段则是 Web UI 写操作路由需要的（见 AgentUrls::finalUrl）。
 * 表面策略（内嵌/外部）与去重都在 web 域里决定。
 *
 * @param agentId agent id；未知或无 webUrl 时静默返回
 */
void WorkbenchContext::openWeb(const QString &agentId)
{
    agentcatalog::AgentDefinition def;
    if (agentRow(m_agents, agentId, &def) < 0) {
        return;
    }

    QVariantMap fields;
    fields[QStringLiteral("agentId")] = def.id;
    const QString sessionUrl = m_agents->sessionUrl(def.id);
    fields[QStringLiteral("url")] = sessionUrl.isEmpty()
            ? agentcatalog::AgentUrls::finalUrl(def)
            : sessionUrl;
    fields[QStringLiteral("title")] = def.name;
    fields[QStringLiteral("icon")] = def.icon;
    fields[QStringLiteral("color")] = def.color;
    const QString tabId = m_web->openTab(fields);
    // 有标签页（或刚激活了已有标签）：带用户过去。外部表面路径返回
    // 空串——浏览器那边已经打开了，web 门面也发过 toast。
    if (!tabId.isEmpty()) {
        m_nav->setCurrentPageId(QStringLiteral("web"));
    }
}

/**
 * @brief 绕过表面策略，把 agent 的 WebUI 交给系统浏览器打开
 *
 * 不建标签页。URL 规则与 openWeb 相同（会话 URL 优先、token 片段保留
 * ——浏览器需要它，toast 只显示 agent 名）。
 *
 * @param agentId agent id；未知或无 webUrl 时静默返回
 */
void WorkbenchContext::openWebExternal(const QString &agentId)
{
    agentcatalog::AgentDefinition def;
    if (agentRow(m_agents, agentId, &def) < 0) {
        return;
    }
    const QString sessionUrl = m_agents->sessionUrl(def.id);
    const QUrl url(sessionUrl.isEmpty()
                   ? agentcatalog::AgentUrls::finalUrl(def)
                   : sessionUrl);
    const core::OpResult result = m_ui->openExternalUrl(url);
    if (result.ok) {
        m_notifications->notify(QStringLiteral("info"),
                                tr("Opening in the browser"), def.name);
    }
}

/**
 * @brief 关掉该 agent 的标签页（若有）
 *
 * @param agentId agent id；没有标签页时静默返回
 */
void WorkbenchContext::closeWeb(const QString &agentId)
{
    const QVariantMap tab = m_web->tabForAgent(agentId);
    const QString id = tab.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
        m_web->closeTab(id);
    }
}

/**
 * @brief 重载该 agent 的标签页（若有）
 *
 * @param agentId agent id；没有标签页时静默返回
 */
void WorkbenchContext::reloadWeb(const QString &agentId)
{
    const QVariantMap tab = m_web->tabForAgent(agentId);
    const QString id = tab.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
        m_web->reloadTab(id);
    }
}

/**
 * @brief 重启 agent（离线遮罩、启动器卡片）
 *
 * 直接转发给 AgentsFacade，结果经它自己的信号回报。
 *
 * @param agentId agent id
 */
void WorkbenchContext::launchAgent(const QString &agentId)
{
    m_agents->launch(agentId);
}

/**
 * @brief 跨域意图：把一份 Markdown 开成独立预览窗口
 *
 * @param path 磁盘路径（文件树右键的那一行）
 *
 * 开窗本身在预览域里：MarkdownPreviewController::openFile() 校验、读盘、切块，
 * 成功后发 windowRequested()。这里只把失败原因说给用户一次——同一个原因在
 * 菜单项、控制器与这里各解释一遍，就会变成三个互相漂移的版本。
 */
void WorkbenchContext::previewMarkdown(const QString &path)
{
    if (!m_markdown) {
        // 组装层没接进预览控制器：那是接线漏了，不是用户的构建问题，
        // 但界面仍要给一句话，不能点了什么都没发生。
        m_notifications->notify(
            QStringLiteral("error"),
            QCoreApplication::translate("WorkbenchContext",
                                        "Markdown preview"),
            QCoreApplication::translate("WorkbenchContext",
                                        "Markdown preview is unavailable."));
        return;
    }
    const core::OpResult opened = m_markdown->openFile(path);
    if (!opened) {
        m_notifications->notify(
            QStringLiteral("error"),
            QCoreApplication::translate("WorkbenchContext",
                                        "Cannot preview this file"),
            opened.error);
    }
}

/**
 * @brief 这份路径能不能给「Preview Markdown」菜单项
 *
 * @param path 磁盘路径
 * @return 本构建有预览能力且路径认作 Markdown 时为真
 *
 * 判定全部在预览域里做（后缀清单与 openFile() 的拒绝共用一处），QML 只问结果。
 */
bool WorkbenchContext::canPreviewMarkdown(const QString &path) const
{
    return m_markdown && m_markdown->canPreviewPath(path);
}

/**
 * @brief 打开 Markdown 工作台窗口（提示词模式）
 *
 * 与 previewMarkdown()（文档模式，经预览控制器开文档）互为两条入口：
 * 这里只发 markdownWorkbenchRequested() 意图，窗口本体由 MainWindow 的
 * Loader 按信号创建——跨域动作不经过 QML 拼接。已在开着的窗口由窗口
 * 自己 raise（它同样监听这个信号）。
 */
void WorkbenchContext::openMarkdownWorkbench()
{
    Q_EMIT markdownWorkbenchRequested();
}

/**
 * @brief 组装层灌进预览控制器
 *
 * @param markdown Markdown 预览控制器；空指针表示本会话没有预览能力
 */
void WorkbenchContext::setMarkdownPreview(
    markdown::MarkdownPreviewController *markdown)
{
    m_markdown = markdown;
}

/**
 * @brief 把文本放进剪贴板，成败各发一条 toast
 *
 * @param text 要复制的文本
 */
void WorkbenchContext::copyText(const QString &text)
{
    const core::OpResult result = m_ui->copyText(text);
    if (result.ok) {
        m_notifications->notify(QStringLiteral("success"),
                                QCoreApplication::translate("WorkbenchContext",
                                                            "Copied"),
                                text);
    } else {
        m_notifications->notify(QStringLiteral("error"),
                                QCoreApplication::translate("WorkbenchContext",
                                                            "Copy failed"),
                                result.error);
    }
}

/**
 * @brief 发一条 toast 通知（QML 侧通用入口）
 *
 * @param level 级别（success / info / warning / error）
 * @param title 标题
 * @param text 正文
 */
void WorkbenchContext::notify(const QString &level, const QString &title,
                              const QString &text)
{
    m_notifications->notify(level, title, text);
}

/**
 * @brief 用系统默认程序打开一个 URL，失败发错误 toast
 *
 * @param url 要打开的 URL
 */
void WorkbenchContext::openExternalUrl(const QUrl &url)
{
    const core::OpResult result = m_ui->openExternalUrl(url);
    if (!result.ok) {
        m_notifications->notify(QStringLiteral("error"),
                                QCoreApplication::translate(
                                    "WorkbenchContext", "Cannot open link"),
                                result.error);
    }
}

/**
 * @brief 用系统默认程序打开一个本地文件（file:// URL 在这里拼）
 *
 * @param path 文件路径
 *
 * QML 侧没有跨 Qt 大版本可靠的 fromLocalFile（旧预览窗里的 `Url.fromLocalFile`
 * 依赖一个不保证存在的 QML 全局名，浏览器路径因此可能一直是静默断的），
 * 转换收进 C++。
 */
void WorkbenchContext::openLocalFileExternal(const QString &path)
{
    openExternalUrl(QUrl::fromLocalFile(path));
}

/**
 * @brief 在系统文件管理器里打开一个目录，失败发错误 toast
 *
 * @param path 目录路径
 */
void WorkbenchContext::openFolder(const QString &path)
{
    const core::OpResult result = m_ui->openFolder(path);
    if (!result.ok) {
        m_notifications->notify(QStringLiteral("error"),
                                QCoreApplication::translate(
                                    "WorkbenchContext", "Cannot open folder"),
                                result.error);
    }
}

/**
 * @brief 打开 agent 的配置目录（转发给 AgentsFacade）
 *
 * @param agentId agent id
 */
void WorkbenchContext::openConfigDir(const QString &agentId)
{
    m_agents->openConfigDir(agentId);
}

/**
 * @brief 取插件列表快照（Q_INVOKABLE，设置页绑定）
 *
 * @return main.cpp 启动时灌进来的发现结果：[{id,name,version,
 *         description,enabled}]
 */
QVariantList WorkbenchContext::pluginList() const
{
    return m_discoveredPlugins;
}

/**
 * @brief 灌入插件发现结果快照（组装层调用）
 *
 * 设置页的列表数据；开关状态由 setPluginEnabled() 同步维护。
 *
 * @param plugins 发现的插件条目列表
 */
void WorkbenchContext::setDiscoveredPlugins(const QVariantList &plugins)
{
    m_discoveredPlugins = plugins;
}

/**
 * @brief 开/关单个插件
 *
 * 写进设置（下一次启动生效——库只在启动时装载），同时更新快照里
 * 该条的 enabled，让设置页立即反映。
 *
 * @param id 插件 id
 * @param enabled 是否启用
 */
void WorkbenchContext::setPluginEnabled(const QString &id, bool enabled)
{
    QStringList disabled = m_settings->pluginsOptions().disabledIds;
    if (enabled) {
        disabled.removeAll(id);
    }
    else if (!disabled.contains(id)) {
        disabled.append(id);
    }

    // 经 Settings 对象落盘（类型化访问只在 core）
    m_settings->setPluginsDisabledIds(disabled);
    m_settings->save();

    // 快照同步翻标记，交给设置页的列表立即生效。
    for (QVariant &entry : m_discoveredPlugins) {
        QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            map[QStringLiteral("enabled")] = enabled;
            entry = map;
        }
    }
}

/**
 * @brief 插件总开关的取值（Q_INVOKABLE）
 *
 * @return 设置里的 plugins.enabled（默认 false）
 */
bool WorkbenchContext::pluginsEnabled() const
{
    return m_settings->pluginsOptions().enabled;
}

/**
 * @brief 翻插件总开关并立即落盘
 *
 * 值没变时不写盘。生效时机同样是下一次启动。
 *
 * @param enabled 是否启用插件
 */
void WorkbenchContext::setPluginsEnabled(bool enabled)
{
    if (m_settings->pluginsOptions().enabled == enabled) {
        return;
    }
    m_settings->setPluginsGloballyEnabled(enabled);
    m_settings->save();
}

/**
 * @brief 设置页必须展示的插件信任提示
 *
 * @return 提示文本（tr() 源串）：插件跑在应用进程里、信任级别与应用
 *         相同、重启后生效
 */
QString WorkbenchContext::pluginTrustNotice() const
{
    return tr("Plugins run inside this application's process. Their trust "
              "level is the same as the application itself — only enable "
              "plugins you trust. Changes take effect after a restart.");
}

/**
 * @brief 退出应用
 */
void WorkbenchContext::quit()
{
    QCoreApplication::quit();
}

} // namespace awb::workbench
