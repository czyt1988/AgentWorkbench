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
#include "workbench/MarkdownSessionStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace awb::workbench {

namespace {

// 文件关联的 ProgId、注册表根与后缀清单。ProgId 是「我们这一族打开器」
// 在注册表里的名字；卸载不清理（残留无害，Windows 惯例）。QSettings 的
// NativeFormat 路径用反斜杠。
constexpr auto kAssocProgId = "AgentWorkbench.Markdown.1";
const char kAssocClassRoot[] = "HKEY_CURRENT_USER\\Software\\Classes";
const char *const kAssocSuffixes[] = { ".md", ".markdown" };

} // namespace

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
        return;
    }
    // 打开成功即触 MRU：会话文件里「最近文档」的唯一来源。
    touchRecentDocument(path);
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
 * @brief 恢复上次的 Markdown 工作台现场
 * @return {restored, preset, promptLeftOpen, documentLeftOpen,
 *          documentRibbonCollapsed, recent}
 *
 * 工作台窗口在本轮应用第一次创建时调用（之后窗口的销毁重建走控制器
 * 内存里的会话，不需要这里）。逐条按路径重开 tab：文件已不存在的跳过
 * （记一条日志，不惊动用户——「找回上次开着的」不该因为一个文件被删
 * 而整组失败）；每条恢复它自己的三态，最后激活记录的激活位。控制器
 * 的命令队列对 setDocument 只留最新一条，逐个激活的中间命令不会
 * 到达页面。
 *
 * 返回的 preset/开合状态给窗口套用；一个 tab 都没恢复时 restored 为
 * 假，窗口按自己的初值走。
 */
QVariantMap WorkbenchContext::restoreMarkdownSession()
{
    QVariantMap result;
    result.insert(QStringLiteral("restored"), false);
    if (!m_markdown || !m_sessionStore) {
        return result;
    }
    const MarkdownSessionState state = m_sessionStore->load();
    if (state.tabs.isEmpty()) {
        return result;
    }

    int restoredCount = 0;
    QString lastDocId;
    for (const MarkdownSessionState::Tab &tab : state.tabs) {
        if (!m_markdown->openFile(tab.path)) {
            qWarning() << "[workbench] session tab skipped (cannot open):"
                       << tab.path;
            continue;
        }
        ++restoredCount;
        lastDocId = m_markdown->activeDocId();
        m_markdown->setMode(tab.mode);
        // 触 MRU：恢复的也是「打开过的文档」。
        touchRecentDocument(tab.path);
    }
    if (restoredCount == 0) {
        return result;
    }

    // 激活位：按恢复后的 tab 顺序找第 activeIndex 个还活着的会话。
    const QVariantList tabs = m_markdown->tabs();
    if (state.activeIndex >= 0 && state.activeIndex < tabs.size()) {
        const QString target = tabs.at(state.activeIndex).toMap()
                .value(QStringLiteral("docId")).toString();
        if (!target.isEmpty()) {
            m_markdown->activateSession(target);
            lastDocId = target;
        }
    }
    else if (!lastDocId.isEmpty()) {
        m_markdown->activateSession(lastDocId);
    }

    result.insert(QStringLiteral("restored"), true);
    result.insert(QStringLiteral("preset"), state.preset);
    result.insert(QStringLiteral("promptLeftOpen"), state.promptLeftOpen);
    result.insert(QStringLiteral("documentLeftOpen"), state.documentLeftOpen);
    result.insert(QStringLiteral("documentRibbonCollapsed"),
                  state.documentRibbonCollapsed);
    result.insert(QStringLiteral("recent"), state.recentDocuments);
    return result;
}

/**
 * @brief 把窗口现场写进会话文件
 * @param windowState 窗口侧的界面状态（preset、左右栏开合、Ribbon 折叠位）
 *
 * tab 组、激活位与三态从控制器取（它的 tabs() 带路径与模式）；MRU 是
 * 本类在每次打开时维护的内存态。窗口在 tab 结构/预设变化时调用——
 * 结构类变更立即写盘（E3 的节奏约定）。
 */
void WorkbenchContext::saveMarkdownSession(const QVariantMap &windowState)
{
    if (!m_sessionStore || !m_markdown) {
        return;
    }
    MarkdownSessionState state;
    const QVariantList tabs = m_markdown->tabs();
    int activeIndex = -1;
    for (int i = 0; i < tabs.size(); ++i) {
        const QVariantMap tab = tabs.at(i).toMap();
        MarkdownSessionState::Tab entry;
        entry.path = tab.value(QStringLiteral("path")).toString();
        entry.mode = tab.value(QStringLiteral("mode")).toString();
        if (entry.path.isEmpty()) {
            continue;
        }
        if (tab.value(QStringLiteral("active")).toBool()) {
            activeIndex = state.tabs.size();
        }
        state.tabs.append(entry);
    }
    state.activeIndex = activeIndex;
    state.preset = windowState.value(
        QStringLiteral("preset"), QStringLiteral("prompt")).toString();
    if (state.preset != QLatin1String("document")) {
        state.preset = QStringLiteral("prompt");
    }
    state.promptLeftOpen = windowState.value(
        QStringLiteral("promptLeftOpen"), true).toBool();
    state.documentLeftOpen = windowState.value(
        QStringLiteral("documentLeftOpen"), false).toBool();
    state.documentRibbonCollapsed = windowState.value(
        QStringLiteral("documentRibbonCollapsed"), true).toBool();
    state.recentDocuments = m_recentDocuments;
    m_sessionStore->save(state);
}

/**
 * @brief 最近文档列表（启动页用）
 * @return MRU 路径（新在前；无记录时为空）
 */
QStringList WorkbenchContext::recentMarkdownDocuments() const
{
    return m_recentDocuments;
}

/**
 * @brief 把一个路径触进 MRU 并落一次会话文件
 * @param path 刚打开的文档路径
 */
void WorkbenchContext::touchRecentDocument(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_recentDocuments = MarkdownSessionStore::touchRecent(
        m_recentDocuments, path);
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
 * @brief 把本程序写进 .md / .markdown 的「打开方式」列表（HKCU）
 * @return 成功，或带原因的失败
 *
 * 写两处：ProgId 的 shell\\open\\command 指向当前可执行文件，以及两个
 * 后缀的 OpenWithProgids 里登记该 ProgId。**不碰 UserChoice**——默认
 * 应用由用户在系统设置里选（Win10+ 的 UserChoice 有签名保护，静默抢
 * 默认既不可靠也不礼貌）；我们的目标是出现在列表里。非 Windows 平台
 * 如实报「不支持」。
 */
core::OpResult WorkbenchContext::registerMarkdownFileAssociation()
{
#ifndef Q_OS_WIN
    return core::OpResult::failure(QCoreApplication::translate(
        "WorkbenchContext",
        "File association is only available on Windows."));
#else
    const QString exe = QCoreApplication::applicationFilePath();
    if (exe.isEmpty()) {
        return core::OpResult::failure(QCoreApplication::translate(
            "WorkbenchContext",
            "The application path could not be determined."));
    }
    QSettings classes(QString::fromLatin1(kAssocClassRoot),
                      QSettings::NativeFormat);
    classes.setValue(QStringLiteral("%1/Default").arg(kAssocProgId),
                     QStringLiteral("Markdown document"));
    classes.setValue(
        QStringLiteral("%1/shell/open/command").arg(kAssocProgId),
        QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1")));
    for (const char *suffix : kAssocSuffixes) {
        classes.setValue(
            QStringLiteral("%1/OpenWithProgids/%2")
                .arg(QString::fromLatin1(suffix), kAssocProgId),
            QString());
    }
    classes.sync();
    if (classes.status() != QSettings::NoError) {
        return core::OpResult::failure(QCoreApplication::translate(
            "WorkbenchContext",
            "The registry could not be written (policy or permission)."));
    }
    return core::OpResult::success();
#endif
}

/**
 * @brief 注册是否还在（command 键指向当前可执行文件）
 * @return 已注册且指向本程序为真
 */
bool WorkbenchContext::markdownFileAssociationRegistered() const
{
#ifdef Q_OS_WIN
    const QString exe = QCoreApplication::applicationFilePath();
    if (exe.isEmpty()) {
        return false;
    }
    const QSettings classes(QString::fromLatin1(kAssocClassRoot),
                            QSettings::NativeFormat);
    const QString command = classes.value(
        QStringLiteral("%1/shell/open/command").arg(kAssocProgId)
    ).toString();
    return command.contains(exe);
#else
    return false;
#endif
}

/**
 * @brief 编码单实例的 open 消息
 * @param paths 要打开的文档路径
 * @return "open:<JSON 数组>"；空列表给 "raise"
 */
QByteArray WorkbenchContext::encodeOpenMessage(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return QByteArrayLiteral("raise");
    }
    const QJsonArray array = QJsonArray::fromStringList(paths);
    return "open:"
            + QJsonDocument(array).toJson(QJsonDocument::Compact);
}

/**
 * @brief 解码单实例消息里的打开请求
 * @param message 第二实例发来的消息
 * @return 路径列表；"raise" 或形状不对的消息得到空列表
 *
 * 坏 JSON 整体作废而不是部分采用：第二实例的消息经过本地 IPC，形状错
 * 只能是版本错位，宁可当一次普通唤醒。
 */
QStringList WorkbenchContext::decodeOpenMessage(const QByteArray &message)
{
    if (!message.startsWith("open:")) {
        return QStringList();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(message.mid(5));
    if (!doc.isArray()) {
        return QStringList();
    }
    QStringList paths;
    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (value.isString() && !value.toString().isEmpty()) {
            paths.append(value.toString());
        }
    }
    return paths;
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
 * @brief 组装层灌进会话存储
 *
 * @param store 会话文件读写器；空指针表示本会话不做持久化（测试）
 */
void WorkbenchContext::setMarkdownSessionStore(MarkdownSessionStore *store)
{
    m_sessionStore = store;
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
