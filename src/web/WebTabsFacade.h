#ifndef AWB_WEB_WEBTABSFACADE_H
#define AWB_WEB_WEBTABSFACADE_H

#include <QAbstractItemModel>
#include <QUrl>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::web {

class WebSurfaceRegistry;
class WebTab;
class WebTabsModel;

/// Web 功能面向 QML 的门面：标签生命周期、表面选择与内存策略。
///
/// 内存策略分两层：非激活标签冻结（freezeInactiveTabs）与超过 maxLiveTabs
/// 后按 LRU 释放最旧的非激活视图（released 态，标签保留）。两者取值都来自
/// Settings::webOptions()。表面按异步契约回报状态（set* 系列）；跨域联动
/// （健康检查、会话 URL 重定向）由 workbench 的 BuiltinPages 调用。
class WebTabsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(QString activeTabId READ activeTabId NOTIFY activeTabChanged)
    // 带通知的行数：QML 绑定（空态提示）没法依赖 rowCount() 方法——
    // 它没有 NOTIFY 信号
    Q_PROPERTY(int tabCount READ tabCount NOTIFY tabCountChanged)
    // 激活标签的状态（"loading" | "ready" | …，无标签时为空串）：
    // 驱动工具栏的重载/停止切换
    Q_PROPERTY(QString activeState READ activeState NOTIFY activeStateChanged)
    Q_PROPERTY(bool devToolsEnabled READ devToolsEnabled CONSTANT)
    Q_PROPERTY(bool freezeInactiveTabs READ freezeInactiveTabs NOTIFY
                   policyChanged)
    Q_PROPERTY(QString downloadDir READ downloadDir NOTIFY policyChanged)
    Q_PROPERTY(QString homeUrl READ homeUrl NOTIFY policyChanged)
    Q_PROPERTY(bool engineAvailable READ engineAvailable CONSTANT)

public:
    WebTabsFacade(core::Settings *settings, QObject *parent = nullptr);

    // 暴露给 QML 的标签模型（QAbstractItemModel 接口）
    QAbstractItemModel *model() const;
    // 本域内部的完整模型句柄（BuiltinPages 等同模块调用方用）
    WebTabsModel *tabs() const { return m_tabs; }

    // 激活标签的 id，无激活标签时为空串
    QString activeTabId() const;
    // 当前标签数，带 tabCountChanged 通知
    int tabCount() const;
    // 激活标签的状态机取值，无激活标签时为空串
    QString activeState() const;

    // 是否启用 DevTools 入口：仅 Debug 构建为 true
    bool devToolsEnabled() const;

    // 非激活标签是否冻结（web.freezeInactiveTabs）
    bool freezeInactiveTabs() const;
    // 下载目录：web.downloadDir，未配置时回退系统下载目录
    QString downloadDir() const;
    // Web 页 Home 按钮要打开的 URL（web.homeUrl）；空串 = 维持 agent
    // 列表空态行为
    QString homeUrl() const;
    // 嵌入表面是否存在：AWB_ENABLE_WEBENGINE=OFF 的构建为 false，
    // 设置页据此把该选项置灰
    bool engineAvailable() const;

    // 写 web.homeUrl 并落盘（设置页输入框的提交入口）
    Q_INVOKABLE void setHomeUrl(const QString &url);

    // 打开配置的 Home URL：非空时按保留 agent id "home" 开标签（已有
    // Home 标签则激活），空串/无效 URL 时无效果并返回空串。表面策略与
    // openTab 一致（external 走系统浏览器）
    Q_INVOKABLE QString openHome();

    // 与 openTab 类似但**总是**开新标签：环回弹出窗口用（OAuth 窗口、
    // 同一 agent 的 target=_blank）——那种场景下 openTab 的同 agent
    // 去重会误伤
    Q_INVOKABLE QString openDetachedTab(const QString &agentId,
                                        const QString &url,
                                        const QString &title);

    // 为 {agentId, url, title, icon, color} 打开标签；同一 agent 已有
    // 标签时改为激活它，不重复开。`external` 表面下交给系统浏览器、
    // 不建标签。返回标签 id（external 路径返回空串）
    Q_INVOKABLE QString openTab(const QVariantMap &fields);
    // 关闭并删除标签；id 未知时无效果
    Q_INVOKABLE void closeTab(const QString &id);
    // 激活一个标签；id 未知时无效果
    Q_INVOKABLE void activateTab(const QString &id);
    // 循环切换激活标签（Ctrl+Tab），到头回绕
    Q_INVOKABLE void stepActiveTab(int delta);
    // 重载标签：released 态先转为重建，否则置回 loading 触发表面 reload
    Q_INVOKABLE void reloadTab(const QString &id);
    // 把标签的 URL 交给系统浏览器打开（嵌入式视图坏了也能用的逃生口）
    Q_INVOKABLE void openExternal(const QString &id);
    // 恢复一个 released 标签：state -> loading，表面据此重建视图并加载
    Q_INVOKABLE void reopen(const QString &id);

    // 该 agent 当前标签的快照（id/agentId/url/title/state），未打开时为空
    Q_INVOKABLE QVariantMap tabForAgent(const QString &agentId) const;
    // id 对应的活 WebTab（不在了为 nullptr）：QML 动态读它的 Q_PROPERTY
    // （zoom、state…），缩放与菜单快捷键用
    Q_INVOKABLE QObject *tabObject(const QString &id) const;
    // 表面 kind 的 QML 组件 URL，不可用时为空串
    Q_INVOKABLE QString surfaceUrl(const QString &kind) const;

    // --- 表面 / 健康检查的回报入口 ---------------------------------------
    Q_INVOKABLE void setTabState(const QString &id, const QString &state);
    Q_INVOKABLE void setTabProgress(const QString &id, int progress);
    Q_INVOKABLE void setTabTitle(const QString &id, const QString &title);
    Q_INVOKABLE void setTabLastError(const QString &id, const QString &error);
    Q_INVOKABLE void setTabZoom(const QString &id, double zoom);
    Q_INVOKABLE void setTabUrl(const QString &id, const QString &url);

    // 由 BuiltinPages 接线的跨域规则（健康检查边沿驱动）
    void markOfflineForAgent(const QString &agentId);
    void markOnlineForAgent(const QString &agentId);
    void closeTabsForAgent(const QString &agentId);
    // 把该 agent 已有标签指向新 URL（从启动输出里捕获的会话 URL）：
    // url 绑定重新导航视图，loading 态清掉 error/offline 覆盖层
    void retargetTabForAgent(const QString &agentId, const QUrl &url);

    // 登记一个表面组件（awb_web_webengine 启动时为 "embedded" 调用）
    void registerSurface(const QString &kind, const QString &componentUrl);

Q_SIGNALS:
    /**
     * @brief 激活标签变化时发射
     */
    void activeTabChanged();

    /**
     * @brief 标签数量变化（增删行）时发射
     */
    void tabCountChanged();

    /**
     * @brief 激活标签的状态取值变化时发射
     */
    void activeStateChanged();

    /**
     * @brief web.freezeInactiveTabs / web.downloadDir 在 settings.json 里
     *        被修改时发射
     */
    void policyChanged();

    /**
     * @brief external 表面路径把 URL 交给系统浏览器后发射的提示
     * @param url 已脱敏的 URL（token 已抹除），workbench 把它转成 toast
     *            （接线在 BuiltinPages）
     */
    void externalOpened(const QString &url);

private:
    // 创建并追加标签、激活它、施行内存策略；返回新标签 id
    QString createTab(const QString &agentId, const QUrl &url,
                      const QVariantMap &fields);
    // 接通 activeTabChanged / activeStateChanged 的转发（构造时调用一次）
    void wireActiveTracking();
    // 施行 LRU 释放策略：超过 maxLiveTabs 的非激活标签转 released
    void applyMemoryPolicy();
    // 按 id 取活标签，不在了返回 nullptr
    WebTab *tabForId(const QString &id) const;

    core::Settings *m_settings;           ///< 设置（webOptions 的来源），不持有
    WebTabsModel *m_tabs;                 ///< 标签模型（本类为其父项）
    class WebSurfaceRegistry *m_registry; ///< 表面注册表（本类为其父项）
    int m_nextTabId = 1;                  ///< 自增的标签序号，生成 "tab-<n>" 形式的 id
};

} // namespace awb::web

#endif // AWB_WEB_WEBTABSFACADE_H
