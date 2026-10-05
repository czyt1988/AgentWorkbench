#ifndef AWB_WORKBENCH_WORKBENCHCONTEXT_H
#define AWB_WORKBENCH_WORKBENCHCONTEXT_H

#include "core/OpResult.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace awb::core {
class Settings;
} // namespace awb::core
namespace awb::agentcatalog {
class AgentsFacade;
} // namespace awb::agentcatalog
namespace awb::shell {
class NavigationModel;
class Notifications;
class UiServices;
} // namespace awb::shell
namespace awb::web {
class WebTabsFacade;
} // namespace awb::web
namespace awb::markdown {
class MarkdownPreviewController;
} // namespace awb::markdown

namespace awb::workbench {

class MarkdownSessionStore;

/// QML 全局 `workbench`：跨域意图与通用动作的唯一入口。
///
/// 它是全工程唯一允许同时认识多个领域的层——openWeb 一类动作既需要
/// agentcatalog 的 URL、又要驱动 web/shell；QML 不直接拼接这些，
/// 一律走这里。QML 侧的小写别名 `workbench` 在 MainWindow.qml 根部。
class WorkbenchContext : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentPageId READ currentPageId NOTIFY currentPageChanged)
    /// 一次性 legacy 导入提示（启动弹窗用）；正常启动恒为空串。
    Q_PROPERTY(QString legacyImportNotice READ legacyImportNotice
               NOTIFY legacyImportNoticeChanged)

public:
    // 构造时只接各域的指针；页面注册与规则布线在 BuiltinPages
    WorkbenchContext(shell::NavigationModel *nav, shell::UiServices *ui,
                     shell::Notifications *notifications,
                     agentcatalog::AgentsFacade *agents, web::WebTabsFacade *web,
                     core::Settings *settings, QObject *parent = nullptr);

    // 当前页面 id（转发自 NavigationModel；Q_PROPERTY 的 READ 侧）
    QString currentPageId() const;
    // legacy 导入提示文本；无提示时为空串（Q_PROPERTY 的 READ 侧）
    QString legacyImportNotice() const { return m_legacyImportNotice; }
    // 设置 legacy 导入提示（Q_PROPERTY 的 WRITE 侧；组装层只在启动时设一次）
    void setLegacyImportNotice(const QString &notice)
    {
        if (notice == m_legacyImportNotice) {
            return;
        }
        m_legacyImportNotice = notice;
        Q_EMIT legacyImportNoticeChanged();
    }

    // 导航意图：切换到 id 对应的页面
    Q_INVOKABLE void showPage(const QString &id);

    // 跨域意图（S5）：把 agent 的 WebUI 开成标签页——内嵌/外部的表面
    // 策略在 WebTabsFacade 里；在应用内打开的同时导航到 web 页（启动器
    // 卡片「打开」的默认路径）
    Q_INVOKABLE void openWeb(const QString &agentId);
    // 绕过表面策略直接交给系统浏览器（卡片拆分按钮的备选路径），
    // 不建标签页
    Q_INVOKABLE void openWebExternal(const QString &agentId);
    // 关掉该 agent 的标签页（若有）
    Q_INVOKABLE void closeWeb(const QString &agentId);
    // 重载该 agent 的标签页（若有）
    Q_INVOKABLE void reloadWeb(const QString &agentId);
    // 重启 agent（离线遮罩、启动器卡片）
    Q_INVOKABLE void launchAgent(const QString &agentId);

    // --- Markdown 预览（跨域：Tools 页的文件树 → 预览窗口） ----------------
    // 打开一份 Markdown 的独立预览窗口。成功与失败都给 toast：失败的原因来自
    // 预览域（过大、非 UTF-8、本构建没有预览能力），QML 不重复解释一遍。
    Q_INVOKABLE void previewMarkdown(const QString &path);
    // 文件树右键菜单据此决定「预览」这一项出不出场——没有预览能力的构建里
    // 它恒为假，条目根本不出现。
    Q_INVOKABLE bool canPreviewMarkdown(const QString &path) const;
    // 打开（或唤醒）Markdown 工作台窗口，落在提示词模式。与 previewMarkdown()
    // 是同一扇窗的两条入口：这里不需要预览能力，纯 QML 编辑器照常可用。
    Q_INVOKABLE void openMarkdownWorkbench();

    // --- 会话（E3：关窗重开找回上次的 tab 组） ----------------------------
    // 恢复上次的现场（tab 组/激活位/每 tab 三态/预设开合）。窗口在本轮应用
    // 第一次创建时调用一次；返回 {restored, preset, promptLeftOpen,
    // documentLeftOpen, documentRibbonCollapsed}，restored 为假时窗口按自己
    // 的初值走。
    Q_INVOKABLE QVariantMap restoreMarkdownSession();
    // 把窗口现场写进会话文件（tab 结构/预设/开合变化时调用；tab 组与激活位
    // 从控制器取，MRU 由本类在每次打开文档时维护）。
    Q_INVOKABLE void saveMarkdownSession(const QVariantMap &windowState);
    // 最近文档 MRU（新在前；启动页的「最近文档」列表）。
    Q_INVOKABLE QStringList recentMarkdownDocuments() const;

    // --- .md 文件关联（E2） -----------------------------------------------
    // 「关联 .md / .markdown 文件」：把本程序写进 HKCU 的打开方式列表。
    // 只出现在「打开方式」里——默认应用由用户在系统设置里选（UserChoice
    // 有签名保护，静默抢默认不可靠也不礼貌）。返回失败原因（写注册表被
    // 策略挡住时如实报因）。
    Q_INVOKABLE awb::core::OpResult registerMarkdownFileAssociation();
    // 注册是否还在（command 键指向当前可执行文件）。
    Q_INVOKABLE bool markdownFileAssociationRegistered() const;

    // 单实例 open 消息的编解码（静态便于测试）：消息形如
    // "open:<JSON 路径数组>"；无请求时仍是 "raise"。解码不认识的消息
    // 得到空列表。
    static QByteArray encodeOpenMessage(const QStringList &paths);
    static QStringList decodeOpenMessage(const QByteArray &message);

    // 通用动作
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void notify(const QString &level, const QString &title,
                            const QString &text);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    // 把一个本地文件路径交给系统默认处理器（file:// URL 在 C++ 侧拼，
    // QML 没有跨版本可靠的 fromLocalFile）。
    Q_INVOKABLE void openLocalFileExternal(const QString &path);
    Q_INVOKABLE void openFolder(const QString &path);
    // 打开用户模板目录（不存在则先建）：mkpath 与打开一步做掉——QML
    // 拿不到 QDir，先开一个不存在的目录只会收获一条报因 toast。
    Q_INVOKABLE void openTemplateDirectory();
    Q_INVOKABLE void openConfigDir(const QString &agentId);
    Q_INVOKABLE void quit();

    // --- 插件（实验性） ----------------------------------------
    // 设置页列表用的已发现插件：[{id,name,version,description,enabled}]。
    // 生效时机是「下一次启动」——库只在启动时装载一次
    Q_INVOKABLE QVariantList pluginList() const;
    // 开/关单个插件；写进设置，下一次启动生效
    Q_INVOKABLE void setPluginEnabled(const QString &id, bool enabled);
    // 插件总开关（设置 → Plugins，默认关）
    Q_INVOKABLE bool pluginsEnabled() const;
    Q_INVOKABLE void setPluginsEnabled(bool enabled);
    // 设置页必须展示的信任提示文本
    Q_INVOKABLE QString pluginTrustNotice() const;
    // 组装层灌进来的发现结果快照（main.cpp 扫描后调用）
    void setDiscoveredPlugins(const QVariantList &plugins);
    /// 组装层灌进来的预览控制器（main.cpp 在构造后设一次）。没设过 = 本构建
    /// 没有预览能力，previewMarkdown 只报原因、不开窗。
    void setMarkdownPreview(markdown::MarkdownPreviewController *markdown);
    /// 组装层灌进来的会话存储（main.cpp 在构造后设一次）。没设过 = 不做
    /// 会话持久化（单测里就是这条路径）。
    void setMarkdownSessionStore(MarkdownSessionStore *store);

Q_SIGNALS:
    /**
     * @brief 当前页面变化时发射（转发自 NavigationModel）
     */
    void currentPageChanged();
    /**
     * @brief legacy 导入提示变化时发射
     */
    void legacyImportNoticeChanged();
    /**
     * @brief 请求打开 Markdown 工作台窗口（MainWindow 的 Loader 监听它）
     */
    void markdownWorkbenchRequested();

private:
    shell::NavigationModel *m_nav;          ///< 导航（页面注册与当前页）
    shell::UiServices *m_ui;                ///< 剪贴板、打开 URL/目录等系统能力
    shell::Notifications *m_notifications;  ///< toast 通知
    agentcatalog::AgentsFacade *m_agents;   ///< agent 目录与运行
    web::WebTabsFacade *m_web;              ///< Web 标签页域
    core::Settings *m_settings;             ///< 设置（插件开关等）
    markdown::MarkdownPreviewController
        *m_markdown = nullptr;   ///< Markdown 预览（可空：本构建可能没有）
    MarkdownSessionStore
        *m_sessionStore = nullptr;  ///< 会话文件读写（可空：单测不持久化）
    QStringList m_recentDocuments;          ///< 最近文档 MRU（新在前）
    QString m_legacyImportNotice;           ///< 一次性导入提示；正常启动为空串
    QVariantList m_discoveredPlugins;       ///< 发现的插件快照（设置页列表）

    // 把一个路径触进 MRU（打开文档的两条路共用；不落盘，saveMarkdownSession
    // 时随现场一起写）。
    void touchRecentDocument(const QString &path);
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_WORKBENCHCONTEXT_H
