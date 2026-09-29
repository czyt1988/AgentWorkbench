#ifndef AWB_WORKBENCH_WORKBENCHCONTEXT_H
#define AWB_WORKBENCH_WORKBENCHCONTEXT_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

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

namespace awb::workbench {

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

    // 通用动作
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void notify(const QString &level, const QString &title,
                            const QString &text);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    Q_INVOKABLE void openFolder(const QString &path);
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

Q_SIGNALS:
    /**
     * @brief 当前页面变化时发射（转发自 NavigationModel）
     */
    void currentPageChanged();
    /**
     * @brief legacy 导入提示变化时发射
     */
    void legacyImportNoticeChanged();

private:
    shell::NavigationModel *m_nav;          ///< 导航（页面注册与当前页）
    shell::UiServices *m_ui;                ///< 剪贴板、打开 URL/目录等系统能力
    shell::Notifications *m_notifications;  ///< toast 通知
    agentcatalog::AgentsFacade *m_agents;   ///< agent 目录与运行
    web::WebTabsFacade *m_web;              ///< Web 标签页域
    core::Settings *m_settings;             ///< 设置（插件开关等）
    QString m_legacyImportNotice;           ///< 一次性导入提示；正常启动为空串
    QVariantList m_discoveredPlugins;       ///< 发现的插件快照（设置页列表）
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_WORKBENCHCONTEXT_H
