#ifndef AWB_WORKBENCH_WORKBENCHCONTEXT_H
#define AWB_WORKBENCH_WORKBENCHCONTEXT_H

#include "core/OpResult.h"

#include <QHash>
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
namespace awb::tools {
class ToolsFacade;
} // namespace awb::tools

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
    /// Markdown 编辑器的输入行为（A 批「输入手感」）：设置页写、工作台
    /// 页读，任一键变化都发 markdownEditorSettingsChanged。写一律经
    /// Q_INVOKABLE（改完顺手 save()）。
    Q_PROPERTY(bool markdownEditorIndentWithSpaces
               READ markdownEditorIndentWithSpaces
               NOTIFY markdownEditorSettingsChanged)
    Q_PROPERTY(int markdownEditorIndentWidth
               READ markdownEditorIndentWidth
               NOTIFY markdownEditorSettingsChanged)
    Q_PROPERTY(bool markdownEditorSmartEnter
               READ markdownEditorSmartEnter
               NOTIFY markdownEditorSettingsChanged)
    Q_PROPERTY(bool markdownEditorFormatShortcuts
               READ markdownEditorFormatShortcuts
               NOTIFY markdownEditorSettingsChanged)
    Q_PROPERTY(bool markdownEditorAutoPair
               READ markdownEditorAutoPair
               NOTIFY markdownEditorSettingsChanged)
    Q_PROPERTY(QString markdownEditorImageAssetsDir
               READ markdownEditorImageAssetsDir
               NOTIFY markdownEditorSettingsChanged)

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

    // --- Markdown 工作台（跨域：Tools 页的文件树 → 工作台页） --------------
    // 在工作台页里打开一份 Markdown（装载走预览控制器，随后导航到页）。
    // 成功与失败都给 toast：失败的原因来自预览域（过大、非 UTF-8、本构建
    // 没有预览能力），QML 不重复解释一遍。
    Q_INVOKABLE void previewMarkdown(const QString &path);
    // 文件树右键菜单据此决定「预览」这一项出不出场——没有预览能力的构建里
    // 它恒为假，条目根本不出现。
    Q_INVOKABLE bool canPreviewMarkdown(const QString &path) const;
    // 导航到 Markdown 工作台页（Agent Tools 分组下的 tools 条目）。工作台
    // 是常驻页面而非独立窗口；这里不需要预览能力，纯 QML 编辑器照常可用。
    Q_INVOKABLE void openMarkdownWorkbench();

    // --- 会话（E3：重开找回上次的 tab 组） ----------------------------------
    // 恢复上次的现场（tab 组/激活位/每 tab 三态/预设开合）。工作台页在本轮
    // 应用第一次创建时调用一次；返回 {restored, preset, promptLeftOpen,
    // documentLeftOpen, documentRibbonCollapsed}，restored 为假时页面按自己
    // 的初值走。
    Q_INVOKABLE QVariantMap restoreMarkdownSession();
    // 把页面现场写进会话文件（tab 结构/预设/开合变化时调用；tab 组与激活位
    // 从控制器取，MRU 由本类在每次打开文档时维护）。
    Q_INVOKABLE void saveMarkdownSession(const QVariantMap &windowState);
    // 最近文档 MRU（新在前；会话文件的「最近文档」列表）。
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

    // --- Markdown 编辑器输入行为（Q_PROPERTY 的 READ 侧 + 设置页的写口） --
    bool markdownEditorIndentWithSpaces() const;
    int markdownEditorIndentWidth() const;
    bool markdownEditorSmartEnter() const;
    bool markdownEditorFormatShortcuts() const;
    bool markdownEditorAutoPair() const;
    QString markdownEditorImageAssetsDir() const;
    // 设置页的写口：写进 Settings 并立即持久化；值没变时不落盘。
    Q_INVOKABLE void setMarkdownEditorIndentWithSpaces(bool useSpaces);
    Q_INVOKABLE void setMarkdownEditorIndentWidth(int spaces);
    Q_INVOKABLE void setMarkdownEditorSmartEnter(bool on);
    Q_INVOKABLE void setMarkdownEditorFormatShortcuts(bool on);
    Q_INVOKABLE void setMarkdownEditorAutoPair(bool on);
    // 图片目录的写口：合法才写入；非法值保持原值并返回 false
    Q_INVOKABLE bool setMarkdownEditorImageAssetsDir(const QString &dir);

    // --- Markdown 编辑器图片导入（拖入/粘贴） ------------------------------
    // 返回 {handled, ok, markdown, error}。handled 为假表示这次输入不属于
    // 图片，QML 应继续走原生粘贴或文件引用。
    Q_INVOKABLE QVariantMap importImageFiles(const QStringList &paths);
    Q_INVOKABLE QVariantMap importImageUrls(const QVariantList &urls);
    Q_INVOKABLE QVariantMap importClipboardImage();
    // 草稿的图片资源根：绑定的工作区，或 <dataRoot>/draft-assets/<id>
    Q_INVOKABLE QString draftAssetRoot(const QString &draftId) const;
    // 草稿预览的资源根：已保存过的草稿用 .md 所在目录（相对引用以它为
    // 基准），否则用 draftAssetRoot。
    Q_INVOKABLE QString draftPreviewAssetRoot(const QString &draftId) const;

    // --- 草稿保存到 Markdown 与草稿图片回收 --------------------------------
    // 上次保存到的文件；空串 = 从未保存（关闭草稿时据此问是否清图片）。
    Q_INVOKABLE QString draftSavePath(const QString &draftId) const;
    // 保存对话框的预填值 {path, directory, fileName}；没有工作区时
    // directory 为空、fileName 仍是清洗后的标题。
    Q_INVOKABLE QVariantMap draftSaveDefaults(const QString &draftId) const;
    // 迁移选项 1 的默认图片目录（文档在工作区内 = 工作区/assets）
    Q_INVOKABLE QString draftImageAssetsFolder(
        const QString &markdownPath) const;
    // 两个本地路径是否在同一盘符（跨盘时相对引用不存在，界面据此提示）
    Q_INVOKABLE bool pathsShareDrive(const QString &left,
                                     const QString &right) const;
    Q_INVOKABLE bool pathExists(const QString &path) const;
    // 把草稿正文写成 Markdown 文件并记录保存位置。返回
    // {ok, error, path, imageCount}；imageCount > 0 时界面应弹图片迁移窗。
    Q_INVOKABLE QVariantMap saveDraftMarkdown(const QString &draftId,
                                              const QString &path,
                                              const QString &text);
    // 保存后的图片迁移：mode = assets | folder | absolute。返回
    // {ok, error, warning, text, moved, reused, targets}；text 是改写后的
    // 正文（界面要同步回编辑器）。
    Q_INVOKABLE QVariantMap migrateDraftImages(const QString &draftId,
                                               const QString &markdownPath,
                                               const QString &mode,
                                               const QString &folder);
    // 草稿数据目录下将要清除的资源：[{path, relativePath, size}]
    Q_INVOKABLE QVariantList draftAssetFiles(const QString &draftId) const;
    // 清空 <dataRoot>/draft-assets/<id> 下的全部内容（不碰工作区）
    Q_INVOKABLE QVariantMap clearDraftAssets(const QString &draftId);

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
    /// 组装层灌进来的 tools 门面（main.cpp 在构造后设一次）。会话恢复要
    /// 按 draftId 找回草稿的标题与正文——那是 tools 域的数据，L3 是唯一
    /// 能同时认识两个域的层。没设过 = 草稿 tab 不进会话恢复（单测路径）。
    void setToolsFacade(tools::ToolsFacade *tools);

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
     * @brief Markdown 编辑器任一输入行为键变化时发射
     *
     * 转发自 Settings::valueChanged 的 "markdownEditor.*" 键——工作台页
     * 与设置页据此同步（页面常驻，设置改动要立即生效）。
     */
    void markdownEditorSettingsChanged();

private:
    /// 图片导入的路径基准：baseDir 决定插入路径，assetsRoot 决定复制落点
    struct ImageContext
    {
        QString baseDir;
        QString assetsRoot;
        QString assetsSubdir;
        bool absolutePaths = false;  ///< 草稿：没有磁盘文档，插入绝对路径
    };
    ImageContext imageContext() const;

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
    tools::ToolsFacade
        *m_tools = nullptr;  ///< 草稿数据（可空：草稿 tab 不进会话恢复）
    QStringList m_recentDocuments;          ///< 最近文档 MRU（新在前）
    QString m_legacyImportNotice;           ///< 一次性导入提示；正常启动为空串
    QVariantList m_discoveredPlugins;       ///< 发现的插件快照（设置页列表）
    /// 已由迁移界面处理过的图片（草稿 id → 源路径）。进程内记忆：同一批
    /// 图片不会在每次 Ctrl+S 时重复弹窗，新增图片仍会触发。
    QHash<QString, QStringList> m_migratedImages;
    /// 上次保存目标（草稿 id → 路径）；换文件后重置迁移记忆。
    QHash<QString, QString> m_savedTargets;

    // 把一个路径触进 MRU（打开文档的两条路共用；不落盘，saveMarkdownSession
    // 时随现场一起写）。
    void touchRecentDocument(const QString &path);
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_WORKBENCHCONTEXT_H
