#ifndef AWB_MARKDOWN_MARKDOWNPREVIEWCONTROLLER_H
#define AWB_MARKDOWN_MARKDOWNPREVIEWCONTROLLER_H

#include "markdown/MarkdownChunker.h"
#include "markdown/MarkdownDocument.h"
#include "core/OpResult.h"

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::theme {
class Theme;
} // namespace awb::theme

namespace awb::markdown {

/// QML 单例 `Markdown`（AgentWorkbench.App 上的大写类型名）：Markdown 预览的
/// 全部会话状态与动作，也是页面 JS 与 C++ 之间那唯一一条命令队列的持有者。
///
/// **多会话（M1）**：控制器是一台「会话容器 + 一个渲染面」。每个会话
/// （`Session`，.cpp 内定义）带着自己的文档、块序列、大纲、rev、模式与
/// 导出落点；`docId` 是会话的资源寻址键。同一时刻只有一个**活跃会话**——
/// 单 `WebEngineView` 复用换文档：切换会话发一条 `setDocument` 全量重建，
/// 增量编辑（`updateBlocks`）只发生在活跃会话上。`doc.json` 供应活跃会话，
/// `asset/<docId>/…` 对所有活着的会话解析（换页瞬间旧会话的图片请求还可能
/// 在飞）。草稿是其中一个特殊会话（虚拟文档，`docId` 固定 "draft"），
/// 编辑面在 tools 域的草稿编辑器，这里只做渲染镜像。
///
/// 分工线在这里划开：本类不认识 WebEngine，也不认识 QML 类型——它只负责
/// 「会话们 + 待发送命令」，把命令送进视图、把视图拉取的资源供出去，
/// 都是 src/markdown/webengine/ 适配器的活。这样纯层能在没有 WebEngine
/// 的构建里编译、在 tst_markdown 里没有浏览器地测试。
///
/// 命令队列而不是信号直发：QML 侧的 runJavaScript 只有 WebEngineView 这一个
/// 入口，视图还没加载完时发的命令会全部丢失。所以命令先进队列，窗口在
/// 「视图就绪」和「有新命令」两个时刻各排空一次，队列里只保留同一类的最新
/// 一条（连续五次编辑只会送出最后一次 updateBlocks）。
class MarkdownPreviewController : public QObject
{
    Q_OBJECT

    /// 本构建是否具备预览能力；不具备时窗口与菜单项都不出现
    Q_PROPERTY(bool available READ available NOTIFY capabilitiesChanged)
    /// 不具备时的原因（面向用户）
    Q_PROPERTY(QString unavailableReason READ unavailableReason
                   NOTIFY capabilitiesChanged)
    /// 渲染栈版本串，关于信息与日志用
    Q_PROPERTY(QString rendererVersion READ rendererVersion
                   NOTIFY capabilitiesChanged)
    /// 窗口是否已经打开
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    /// 文件会话的 tab 清单（草稿不在内）：[{docId,title,dirty,active}]
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    /// 活跃会话的资源寻址 id；无活跃会话时为空串
    Q_PROPERTY(QString activeDocId READ activeDocId NOTIFY activeSessionChanged)
    /// 活跃会话是否是草稿（提示词形态的渲染镜像）
    Q_PROPERTY(bool draftActive READ isDraftActive NOTIFY activeSessionChanged)
    /// 活跃草稿的 id（draftActive 为假时空串）
    Q_PROPERTY(QString activeDraftId READ activeDraftId
                   NOTIFY activeSessionChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    Q_PROPERTY(QString fileTitle READ fileTitle NOTIFY documentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(QString readOnlyReason READ readOnlyReason NOTIFY readOnlyChanged)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY readOnlyChanged)
    /// 能否用「编辑 | 分栏 | 源码」三态（受 canEdit 约束）
    Q_PROPERTY(bool splitAvailable READ splitAvailable NOTIFY readOnlyChanged)
    /// 三态模式（每会话一份；新会话取 settings 里的默认值）
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY viewChanged)
    Q_PROPERTY(int fontSizePx READ fontSizePx WRITE setFontSizePx
                   NOTIFY viewChanged)
    Q_PROPERTY(int contentWidthPx READ contentWidthPx WRITE setContentWidthPx
                   NOTIFY viewChanged)
    Q_PROPERTY(bool showOutline READ showOutline WRITE setShowOutline
                   NOTIFY viewChanged)
    /// 启动时自动打开工作台窗口（重度用户开关，默认关）
    Q_PROPERTY(bool autoOpenWorkbench READ autoOpenWorkbench
                   WRITE setAutoOpenWorkbench NOTIFY viewChanged)
    Q_PROPERTY(QVariantList outline READ outline NOTIFY outlineChanged)
    /// 文档字节数（状态栏显示）
    Q_PROPERTY(qint64 byteSize READ byteSize NOTIFY documentChanged)
    /// 外部改动的提示：窗口 QML 据此弹「重载 / 保留我的改动」
    Q_PROPERTY(bool changedOnDisk READ changedOnDisk
                   NOTIFY changedOnDiskChanged)

public:
    /// 文件会话上限：满了再开会被拒绝（提示关 tab，不静默淘汰）。
    static constexpr int kMaxFileSessions = 8;

    explicit MarkdownPreviewController(core::Settings *settings,
                                       theme::Theme *theme,
                                       QObject *parent = nullptr);
    ~MarkdownPreviewController() override;

    /// 路径是否认作 Markdown（纯后缀判定，与能力无关）。命令行参数的
    /// 文件关联扫描用它——argv 里挑出 .md 后能不能真打开交给 openFile()
    /// 的能力判定与报因，这里只回答「这像是 Markdown 吗」。
    static bool isMarkdownPath(const QString &path);

    bool available() const;
    QString unavailableReason() const;
    QString rendererVersion() const;

    /// 预览表面（awbmd:// 的 scheme handler 与它的 profile）装好了没有，
    /// 由 src/markdown/webengine/ 的 provider 在构造时置位。这里判的不是
    /// 「这个二进制链接了 WebEngine」而是「页面真的能被供出去」：scheme
    /// 注册顺序错了的时候前者照样为真，而页面的 fetch 全废——正文在、公式
    /// 与图表出不来。只给适配器调用——QML 侧一律读 available 属性。
    void setSurfaceReady(bool ready);
    bool active() const { return m_windowOpen; }
    QString filePath() const;
    QString fileTitle() const;
    bool dirty() const;
    bool readOnly() const;
    QString readOnlyReason() const;
    bool canEdit() const;
    bool splitAvailable() const;
    bool changedOnDisk() const;
    qint64 byteSize() const;

    // tab 清单（M4 起草稿 tab 与文件 tab 混排，草稿排最前按创建序）：
    // [{docId,title,path,mode,dirty,active,draft}]，draft 为真表示草稿
    // tab（path 为空串）。
    QVariantList tabs() const;
    // 活跃会话的 docId；无活跃会话时为空串
    QString activeDocId() const;
    // 活跃会话是否是草稿会话
    bool isDraftActive() const;
    // 活跃草稿的 id（tools.json 的键）；非草稿活跃时空串
    QString activeDraftId() const;

    // 视图设置的四组读写（WRITE 入口即 Q_INVOKABLE 的等价物）。
    // 模式是每会话一份；字号/栏宽/大纲是全局偏好（settings.json）。
    QString mode() const;
    int fontSizePx() const { return m_fontSizePx; }
    int contentWidthPx() const { return m_contentWidthPx; }
    bool showOutline() const { return m_showOutline; }
    QVariantList outline() const;

    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void setFontSizePx(int pixels);
    Q_INVOKABLE void setContentWidthPx(int pixels);
    Q_INVOKABLE void setShowOutline(bool show);
    Q_INVOKABLE void setAutoOpenWorkbench(bool open);
    bool autoOpenWorkbench() const { return m_autoOpenWorkbench; }

    /// 预览页面的地址（awbmd://runtime/app/shell.html）。视图只经它拿 URL：
    /// scheme 名、host、资源前缀三处拼一次的地方多了，就会有一处拼错而页面
    /// 只在运行时 404。
    Q_INVOKABLE QUrl pageUrl() const;

    // --- 会话管理（M1） ----------------------------------------------
    /// 打开一份 Markdown：同路径已有会话则激活它（tab 已在），否则建新会话
    /// 并激活。成功后发 windowRequested()。失败不开窗、不动既有会话。
    Q_INVOKABLE awb::core::OpResult openFile(const QString &path);

    /// 激活一个会话（文件或草稿）。切换 = 页面整篇重建（setDocument）。
    Q_INVOKABLE void activateSession(const QString &docId);

    /// 关掉一个文件会话（关 tab ≠ 删文档：磁盘文件不动）。关的是活跃会话时，
    /// 顺位激活最后打开的文件会话，没有文件会话则退到草稿。
    Q_INVOKABLE void closeSession(const QString &docId);

    // --- 多草稿会话（M4） --------------------------------------------------
    /// 建一个草稿会话（幂等）：draftId 是 tools.json 里那条草稿的 id，
    /// docId 与之相同（资源寻址共用一个键，不需要第二套名字）。正文与
    /// 标题由 tools 域持有，这里只做渲染镜像。返回 docId；draftId 空时
    /// 返回空串。
    Q_INVOKABLE QString openDraftSession(const QString &draftId,
                                         const QString &title,
                                         const QString &body);

    /// 草稿标题变了（重命名后刷新 tab 与渲染标题）。不存在则忽略。
    Q_INVOKABLE void setDraftTitle(const QString &draftId,
                                   const QString &title);

    /// 关掉一个草稿会话（关 tab ≠ 删草稿：tools.json 的记录不动——
    /// 「关标签 ≠ 删草稿」与文件 tab 同一语义）。关的是活跃会话时顺位。
    Q_INVOKABLE void closeDraftSession(const QString &draftId);

    /// 草稿正文回写（窗口的草稿编辑器每敲一键调一次；持久化在 tools 域）。
    /// draftId 空时忽略。
    Q_INVOKABLE void setDraftText(const QString &draftId,
                                  const QString &text);

    // ------------------------------------------------------------------
    /// 这个路径是否归预览管（后缀判定与 openFile() 同一处，见 .cpp）。
    /// 文件树右键菜单用它决定条目出不出——后缀清单不是 QML 该知道的事。
    Q_INVOKABLE bool canPreviewPath(const QString &path) const;

    // 窗口 QML 的生命周期播报。关窗**不清会话**：会话是应用运行期的记忆，
    // 重开窗口经 doc.json 直接取回活跃会话；跨启动的恢复走 workbench 域
    // 的会话文件（MarkdownSessionStore）。
    Q_INVOKABLE void notifyWindowOpened();
    Q_INVOKABLE void notifyWindowClosed();

    // 编辑器回写与存盘（作用在活跃会话上）
    Q_INVOKABLE void setEditorText(const QString &text);
    Q_INVOKABLE QString editorText() const;
    Q_INVOKABLE awb::core::OpResult saveNow();
    /// 按 id 存某个文件会话，**不改变激活**（关窗守卫批量存盘用）。
    Q_INVOKABLE awb::core::OpResult saveSession(const QString &docId);
    // 磁盘那份重新读进来（放弃本地改动）
    Q_INVOKABLE awb::core::OpResult reloadFromDisk();
    // 差异提示消失（用户选了「保留我的改动」）
    Q_INVOKABLE void dismissDiskNotice();
    /// 重新比对磁盘形态并刷新 changedOnDisk。文件监视器与窗口激活各调一次：
    /// QFileSystemWatcher 在 Windows 上会静默丢掉「删除后重建」的文件，光靠
    /// 它会有漏报，这条轮询式复查是它的兜底。
    Q_INVOKABLE void refreshDiskState();

    /// 页面 fetch('awbmd://runtime/doc.json') 的响应体：**活跃会话**的
    /// 全部状态。每次调用都按当前状态重新生成，rev 与内容不会错拍。
    Q_INVOKABLE QString documentBodyJson() const;

    /// 随包样式表文本（:/markdown/app/theme.css）：预览面在推入车道
    /// （fetch 不可用的引擎）把样式表喂给页面用；fetch 车道页面自己取。
    Q_INVOKABLE QString pageStylesheetText() const;

    /// 活跃会话的 docId（资源寻址键）；无会话时为空串。
    QString docId() const;
    /// 活跃会话的文档目录（相对图片的解析基准）。
    QString assetRoot() const;
    /// 任意活着会话的文档目录；会话不存在（或草稿）返回空串。适配器按
    /// asset/<docId>/… 寻址时用：换页瞬间旧会话的图片请求还可能在飞。
    QString assetRootFor(const QString &docId) const;

    /// 适配器排空命令队列用：取出队首命令 JSON，空串表示没有。
    Q_INVOKABLE QString takeCommand();
    /// 队列里是否还有未送出的命令
    Q_INVOKABLE bool hasPendingCommand() const { return !m_queue.isEmpty(); }

    // 页面报告（信标太小放不下的一律经 runJavaScript 返回值取回后交这里）
    Q_INVOKABLE void reportReady(const QString &json);
    Q_INVOKABLE void reportFrame(const QString &json);
    Q_INVOKABLE void reportError(const QString &json);
    /// 页面报回整篇序列化的 HTML。**这里就是落盘点**：QML 不碰文件系统，
    /// 所以「写到哪、写不写得成」全在本类决定，结果经 markupWritten /
    /// markupWriteFailed 报给界面。purpose 是 requestMarkup() 记住的那个。
    Q_INVOKABLE void reportMarkup(const QString &html);

    /// 请求把当前渲染结果整篇序列化（导出 / 外部打开），结果从 reportMarkup
    /// 回来。purpose 取 "export" 或 "browser"，决定接手方。
    Q_INVOKABLE void requestMarkup(const QString &purpose);

    /// 导出目录（每会话一份）。空串 = 文档所在目录——导出件里的图片是相对
    /// 引用，只有落在文档旁边才取到，换目录导出是个用户知情选择的降级，
    /// 不是默认。窗口 QML 经原生目录对话框选好这里，再调 requestMarkup。
    Q_INVOKABLE void setExportDirectory(const QString &directory);
    Q_INVOKABLE QString exportDirectory() const;
    /// 导出文件的建议名：文档主名换 .html，没有文档时给 "Markdown.html"
    Q_INVOKABLE QString exportFileName() const;
    // 请求整篇渲染（把懒渲染的图表全部补上），只在导出/外部打开前需要
    Q_INVOKABLE void requestFullRender();
    // 文档内搜索：命令进队列，命中数从 reportFindResult 回来
    Q_INVOKABLE void requestFind(const QString &text, bool forward,
                                bool caseSensitive, bool restart);
    Q_INVOKABLE void reportFindResult(const QString &json);
    // 跳到大纲项
    Q_INVOKABLE void scrollToAnchor(const QString &anchor, int blockIndex);

    // 块序列（测试与适配器都按它工作；无活跃会话时是空表）
    const QVector<MarkdownBlock> &blocks() const;

Q_SIGNALS:
    /// 请求打开预览窗口：MainWindow.qml 里那个 Loader 的唯一激活条件
    void windowRequested();
    /// 有新命令排队，适配器应当来 takeCommand()
    void commandQueued();
    void activeChanged();
    /// 活跃会话换人了：所有反映活跃会话的属性都随此重算
    void activeSessionChanged();
    /// tab 清单或其中某项的状态（标题/脏标记/激活位）变了
    void tabsChanged();
    void documentChanged();
    void dirtyChanged();
    void readOnlyChanged();
    void viewChanged();
    void outlineChanged();
    void capabilitiesChanged();
    void changedOnDiskChanged();
    /// 导出/外部打开的 HTML 已就绪（异步：页面要先补渲染再序列化）
    void markupReady(const QString &html, const QString &purpose);
    /// 序列化件已写盘（purpose："export" 给用户挑的目录或文档目录，
    /// "browser" 给 <dataRoot>/preview 下的临时件——外部浏览器不认识 awbmd://）
    void markupWritten(const QString &path, const QString &purpose);
    /// 序列化件写盘失败
    void markupWriteFailed(const QString &purpose, const QString &message);
    /// 页面报回来的渲染失败
    void renderFailed(const QString &message);
    /// 搜索结果（total/active）
    void findResult(int total, int active);
    /// 保存成功，窗口标题与吐司都要跟进
    void saved();

private:
    // 会话的完整形态（文档、块、视图态、导出态、载荷缓存）；定义在 .cpp。
    struct Session;

    // 按 docId 找会话（文件会话 + 草稿）；找不到返回空 shared_ptr
    std::shared_ptr<Session> sessionById(const QString &docId) const;
    // 这个 docId 是否某个草稿会话
    bool isDraftId(const QString &docId) const;
    // 活跃会话的裸指针；无活跃会话时为空（读侧各属性自带兜底）
    Session *activeSession() const;
    // 建一个新文件会话并接好它的信号线（读盘失败时不入列）
    std::shared_ptr<Session> createFileSession(const QString &path,
                                               core::OpResult *failure);
    // 激活切换的公共路径：换人、发 setDocument、广播全部属性信号
    bool activateSession(const std::shared_ptr<Session> &session);
    // 把一段编辑回写进会话状态（重切块 + 差异），草稿与文件共用
    void applySessionText(Session *session, const QString &text);
    // 某个会话的磁盘差异复查（窗口激活轮询与 watcher 两条路的汇合点）
    void refreshDiskStateFor(Session *session);
    // 活跃会话整体重算（块 + 大纲 + rev），发 documentChanged 家族信号
    void rebuildSession(Session *session);
    // 接好一个会话的文档信号线（活跃时才转发为控制器的属性信号）
    void wireSessionSignals(const std::shared_ptr<Session> &session);
    // 让每个会话的 doc.json 载荷缓存失效（全局视图偏好变化时）
    void invalidateAllSessionBodies();

    // 把一条命令放进队列（同类只留最新）
    void enqueue(const QString &cmd, const QVariantMap &args);
    void enqueue(const QString &cmd, const QJsonObject &args);
    // 视图设置持久化（模式进 settings 的默认值，其余三项本就是全局偏好）
    void persistView(const QString &mode);
    void loadView();

    core::Settings *m_settings;    ///< 视图设置的落点（不拥有）
    theme::Theme *m_theme;         ///< CSS 变量的来源（不拥有）

    // --- 会话状态 -------------------------------------------------------
    QList<std::shared_ptr<Session>> m_sessions;  ///< 文件会话（打开顺序）
    QList<std::shared_ptr<Session>> m_drafts;    ///< 草稿会话（多草稿，创建序）
    std::shared_ptr<Session> m_active;           ///< 活跃会话（可空）
    int m_docSeq = 0;             ///< docId 的自增序号

    int m_seq = 0;                ///< 命令序号，只用于日志对齐
    bool m_surfaceReady = false;  ///< 适配器是否已装好预览表面（见 setSurfaceReady）
    bool m_windowOpen = false;    ///< 窗口是否活着
    QString m_markupPurpose;      ///< 在飞的序列化请求用途

    QList<QPair<QString, QString>>
        m_queue;                   ///< 待发送命令：cmd → 命令 JSON

    QString m_defaultMode;         ///< 新会话的三态默认值（settings 那份）
    int m_fontSizePx = 0;          ///< 0 = 用主题基准
    int m_contentWidthPx = 0;      ///< 0 = 用主题基准
    bool m_showOutline = true;     ///< 大纲侧栏开关
    bool m_autoOpenWorkbench = false;  ///< 启动即开工作台（settings 那份）
};

} // namespace awb::markdown

#endif // AWB_MARKDOWN_MARKDOWNPREVIEWCONTROLLER_H
